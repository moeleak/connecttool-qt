#include "network/tcp/multiplex_session.h"
#include "network/protocol/multiplex_protocol.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>

namespace connecttool::network {

namespace {
// Keep chunks close to path MTU to reduce Steam UDP fragmentation/lock pressure
constexpr std::size_t kTunnelChunkBytes = 1100; // slightly larger chunks to reduce fragment count
constexpr std::size_t kHighWaterBytes = 512 * 1024; // tighter throttling
constexpr std::size_t kLowWaterBytes = 256 * 1024;
constexpr std::size_t kClientReadBufferBytes = 64 * 1024;

[[nodiscard]] auto clockTicks() noexcept {
  return std::chrono::steady_clock::now().time_since_epoch().count();
}

// Simple, local ID generator to avoid pulling in the full nanoid dependency
std::string generateId(std::size_t length = 6) {
  static constexpr char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  static thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<std::size_t> dist(0, sizeof(chars) - 2);

  std::string id;
  id.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    id.push_back(chars[dist(rng)]);
  }
  return id;
}
} // namespace

struct MultiplexSession::ClientState {
  explicit ClientState(std::shared_ptr<tcp::socket> clientSocket,
                       ClientClosedCallback closedCallback = {})
      : socket(std::move(clientSocket)), readBuffer(kClientReadBufferBytes),
        onClosed(std::move(closedCallback)) {}

  std::shared_ptr<tcp::socket> socket;
  std::vector<char> readBuffer;
  ClientClosedCallback onClosed;
  std::mutex writeMutex;
  std::deque<std::shared_ptr<std::vector<char>>> writeQueue;
  bool writeInProgress = false;
};

MultiplexSession::MultiplexSession(Dependencies dependencies)
    : channel_(std::move(dependencies.channel)), io_context_(dependencies.ioContext),
      isHost_(dependencies.isHost),
      localPort_(dependencies.localPort) {
  sendTimer_ = std::make_unique<boost::asio::steady_timer>(io_context_);
}

MultiplexSession::~MultiplexSession() {
  if (sendTimer_) {
    try {
      sendTimer_->cancel();
    } catch (const boost::system::system_error &) {
      // The io_context may already be shutting down.
    }
  }

  std::vector<std::shared_ptr<ClientState>> clients;
  {
    std::lock_guard<std::mutex> lock(mapMutex_);
    clients.reserve(clients_.size());
    for (auto &entry : clients_) {
      clients.push_back(std::move(entry.second));
    }
    clients_.clear();
    missingClients_.clear();
  }
  for (const auto &client : clients) {
    boost::system::error_code ignored;
    client->socket->close(ignored);
    if (client->onClosed) {
      client->onClosed();
    }
  }
}

std::string MultiplexSession::addClient(std::shared_ptr<tcp::socket> socket,
                                        ClientClosedCallback onClosed) {
  std::string id;
  {
    std::lock_guard<std::mutex> lock(mapMutex_);
    do {
      id = generateId(6);
    } while (clients_.contains(id));

    clients_.emplace(id, std::make_shared<ClientState>(std::move(socket), std::move(onClosed)));
    missingClients_.erase(id);
  }
  startAsyncRead(id);
  std::cout << "Added client with id " << id << std::endl;
  return id;
}

bool MultiplexSession::removeClient(const std::string &id) {
  std::shared_ptr<ClientState> client;
  {
    std::lock_guard<std::mutex> lock(mapMutex_);
    const auto found = clients_.find(id);
    if (found != clients_.end()) {
      client = std::move(found->second);
      clients_.erase(found);
    }
    missingClients_.erase(id);
  }

  if (client) {
    {
      std::lock_guard<std::mutex> lock(client->writeMutex);
      client->writeQueue.clear();
      client->writeInProgress = false;
    }
    boost::system::error_code ignored;
    client->socket->close(ignored);
  }
  {
    std::lock_guard<std::mutex> lock(pausedMutex_);
    pausedReads_.erase(id);
  }

  if (client) {
    std::cout << "Removed client with id " << id << std::endl;
  }
  bool shouldResume = false;
  {
    std::lock_guard<std::mutex> queueLock(queueMutex_);
    pendingPackets_.erase(id);
    removeFromOrder(id);
    if (pendingPackets_.empty()) {
      sendBlocked_.store(false, std::memory_order_relaxed);
      shouldResume = true;
    }
  }
  if (shouldResume) {
    resumePausedReads();
  }
  if (client && client->onClosed) {
    client->onClosed();
  }
  return client != nullptr;
}

std::shared_ptr<MultiplexSession::ClientState>
MultiplexSession::getClientState(const std::string &id) {
  std::lock_guard<std::mutex> lock(mapMutex_);
  const auto it = clients_.find(id);
  if (it != clients_.end()) {
    return it->second;
  }
  return nullptr;
}

std::shared_ptr<tcp::socket> MultiplexSession::getClient(const std::string &id) {
  const auto client = getClientState(id);
  return client ? client->socket : nullptr;
}

std::vector<char> MultiplexSession::buildPacket(const std::string &id, const char *data, size_t len,
                                                int type) const {
  const auto connectionId = connecttool::multiplex::ConnectionId::parse(id);
  if (!connectionId) {
    return {};
  }

  const auto packetType = type == 0 ? connecttool::multiplex::PacketType::Data
                                    : connecttool::multiplex::PacketType::Disconnect;
  const auto payload = data && packetType == connecttool::multiplex::PacketType::Data
                           ? std::as_bytes(std::span{data, len})
                           : std::span<const std::byte>{};
  const auto encoded = connecttool::multiplex::encodeFrame(*connectionId, packetType, payload);
  std::vector<char> packet(encoded.size());
  std::memcpy(packet.data(), encoded.data(), encoded.size());
  return packet;
}

bool MultiplexSession::trySendPacket(const std::vector<char> &packet) {
  if (packet.empty()) {
    return true;
  }

  if (isSendSaturated()) {
    return false;
  }
  const auto bytes = std::as_bytes(std::span{packet});
  const auto result = channel_->send(bytes);
  if (result == connecttool::network::SendStatus::Sent) {
    backoffMs_.store(5, std::memory_order_relaxed);
    return true;
  }
  if (result == connecttool::network::SendStatus::Backpressured) {
    lastBlockedTicks_.store(clockTicks(), std::memory_order_relaxed);
    int current = backoffMs_.load(std::memory_order_relaxed);
    int next = std::min(current * 2, 100);
    backoffMs_.store(next, std::memory_order_relaxed);
    sendBlocked_.store(true, std::memory_order_relaxed);
    return false;
  }

  return true;
}

void MultiplexSession::enqueuePacket(const std::string &id, std::vector<char> packet) {
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    auto &queue = pendingPackets_[id];
    const bool wasEmpty = queue.empty();
    queue.push_back(std::move(packet));
    if (wasEmpty) {
      if (sendOrderSet_.insert(id).second) {
        sendOrder_.push_back(id);
      }
    }
  }
  scheduleFlush();
}

void MultiplexSession::flushPendingPackets() {
  if (isSendSaturated()) {
    return;
  }

  std::unique_lock<std::mutex> lock(queueMutex_);
  while (!sendOrder_.empty()) {
    const std::string id = sendOrder_.front();
    sendOrder_.pop_front();
    auto it = pendingPackets_.find(id);
    if (it == pendingPackets_.end()) {
      sendOrderSet_.erase(id);
      continue;
    }
    auto &queue = it->second;
    if (queue.empty()) {
      pendingPackets_.erase(it);
      sendOrderSet_.erase(id);
      continue;
    }

    if (!trySendPacket(queue.front())) {
      sendBlocked_.store(true, std::memory_order_relaxed);
      sendOrder_.push_front(id); // retry this id first when unblocked
      return;
    }
    queue.pop_front();
    if (!queue.empty()) {
      sendOrder_.push_back(id);
    } else {
      pendingPackets_.erase(it);
      sendOrderSet_.erase(id);
    }
  }
  sendBlocked_.store(false, std::memory_order_relaxed);
  lock.unlock();
  resumePausedReads();
}

void MultiplexSession::scheduleFlush(std::chrono::milliseconds delay) {
  bool needSchedule = false;
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (!flushScheduled_ && !sendOrder_.empty()) {
      flushScheduled_ = true;
      needSchedule = true;
    }
  }
  if (!needSchedule) {
    return;
  }

  auto nextDelay = delay;
  if (sendBlocked_.load(std::memory_order_relaxed)) {
    nextDelay = std::max(nextDelay, std::chrono::milliseconds(backoffMs_.load()));
  }

  sendTimer_->expires_after(nextDelay);
  const std::weak_ptr weakSelf = weak_from_this();
  sendTimer_->async_wait([weakSelf](const boost::system::error_code &ec) {
    const auto self = weakSelf.lock();
    if (!self) {
      return;
    }
    if (!ec) {
      self->flushPendingPackets();
    }
    bool shouldReschedule = false;
    auto rescheduleDelay = std::chrono::milliseconds(5);
    {
      std::lock_guard<std::mutex> lock(self->queueMutex_);
      self->flushScheduled_ = false;
      shouldReschedule = !self->sendOrder_.empty();
      if (self->sendBlocked_.load(std::memory_order_relaxed)) {
        rescheduleDelay =
            std::chrono::milliseconds(self->backoffMs_.load(std::memory_order_relaxed));
      }
    }
    if (shouldReschedule) {
      self->scheduleFlush(rescheduleDelay);
    }
  });
}

void MultiplexSession::sendTunnelPacket(const std::string &id, const char *data, size_t len,
                                        int type) {
  bool blocked = false;
  auto pushPacket = [this, &id, &blocked](const char *ptr, size_t amount, int packetType) {
    auto packet = buildPacket(id, ptr, amount, packetType);
    if (blocked || isSendSaturated()) {
      blocked = true;
      enqueuePacket(id, std::move(packet));
      return;
    }
    if (!trySendPacket(packet)) {
      blocked = true;
      enqueuePacket(id, std::move(packet));
    }
  };

  if (type == 0 && data && len > kTunnelChunkBytes) {
    size_t offset = 0;
    while (offset < len) {
      const size_t chunk = std::min(kTunnelChunkBytes, len - offset);
      pushPacket(data + offset, chunk, 0);
      offset += chunk;
    }
  } else {
    pushPacket(data, len, type);
  }

  if (blocked) {
    sendBlocked_.store(true, std::memory_order_relaxed);
    lastBlockedTicks_.store(clockTicks(), std::memory_order_relaxed);
  }
}

void MultiplexSession::handleTunnelPacket(const char *data, size_t len) {
  if (!data) {
    return;
  }
  const auto decoded = connecttool::multiplex::decodeFrame(std::as_bytes(std::span{data, len}));
  if (!decoded) {
    std::cerr << "Invalid tunnel packet size" << std::endl;
    return;
  }
  const std::string id = decoded->connectionId.toString();
  if (decoded->type == connecttool::multiplex::PacketType::Data) {
    // Data packet
    const size_t dataLen = decoded->payload.size();
    const char *packetData = reinterpret_cast<const char *>(decoded->payload.data());
    auto client = getClientState(id);
    if (!client && isHost_ && localPort_ > 0) {
      // 如果是主持且没有对应的 TCP Client，创建一个连接到本地端口
      std::cout << "Creating new TCP client for id " << id
                << " connecting to localhost:" << localPort_ << std::endl;
      try {
        const auto now = std::chrono::steady_clock::now();
        {
          std::lock_guard<std::mutex> lock(mapMutex_);
          const auto it = recentConnectFail_.find(id);
          if (it != recentConnectFail_.end() && now - it->second < std::chrono::seconds(1)) {
            return; // 最近失败过，避免频繁重试占用 CPU
          }
        }
        auto newSocket = std::make_shared<tcp::socket>(io_context_);
        boost::system::error_code ec;
        newSocket->set_option(tcp::no_delay(true), ec);
        tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve("127.0.0.1", std::to_string(localPort_));
        boost::asio::connect(*newSocket, endpoints);

        const auto newClient = std::make_shared<ClientState>(newSocket);
        bool inserted = false;
        {
          std::lock_guard<std::mutex> lock(mapMutex_);
          const auto [position, wasInserted] = clients_.try_emplace(id, newClient);
          client = position->second;
          inserted = wasInserted;
        }
        std::cout << "Successfully created TCP client for id " << id << std::endl;
        if (inserted) {
          startAsyncRead(id);
        }
        {
          std::lock_guard<std::mutex> lock(mapMutex_);
          recentConnectFail_.erase(id);
        }
      } catch (const std::exception &e) {
        std::cerr << "Failed to create TCP client for id " << id << ": " << e.what() << std::endl;
        {
          std::lock_guard<std::mutex> lock(mapMutex_);
          recentConnectFail_[id] = std::chrono::steady_clock::now();
        }
        sendTunnelPacket(id, nullptr, 0, 1);
        return;
      }
    }
    if (client) {
      {
        std::lock_guard<std::mutex> lock(mapMutex_);
        missingClients_.erase(id);
      }
      enqueueClientWrite(id, packetData, dataLen);
    } else {
      bool firstMiss = false;
      {
        std::lock_guard<std::mutex> lock(mapMutex_);
        firstMiss = missingClients_.insert(id).second;
      }
      if (firstMiss) {
        std::cerr << "No client found for id " << id << std::endl;
      }
      sendTunnelPacket(id, nullptr, 0, 1);
    }
  } else if (decoded->type == connecttool::multiplex::PacketType::Disconnect) {
    // Disconnect packet
    if (removeClient(id)) {
      std::cout << "Client " << id << " disconnected" << std::endl;
    }
  }
}

void MultiplexSession::enqueueClientWrite(const std::string &id, const char *data,
                                          std::size_t length) {
  if (!data || length == 0) {
    return;
  }
  const auto client = getClientState(id);
  if (!client) {
    return;
  }

  auto payload = std::make_shared<std::vector<char>>(data, data + length);
  bool startWrite = false;
  {
    std::lock_guard<std::mutex> lock(client->writeMutex);
    client->writeQueue.push_back(std::move(payload));
    if (!client->writeInProgress) {
      client->writeInProgress = true;
      startWrite = true;
    }
  }
  if (startWrite) {
    startClientWrite(id, client);
  }
}

void MultiplexSession::startClientWrite(const std::string &id,
                                        const std::shared_ptr<ClientState> &client) {
  std::shared_ptr<std::vector<char>> payload;
  {
    std::lock_guard<std::mutex> lock(client->writeMutex);
    if (client->writeQueue.empty()) {
      client->writeInProgress = false;
      return;
    }
    payload = client->writeQueue.front();
  }

  const std::weak_ptr weakSelf = weak_from_this();
  boost::asio::async_write(
      *client->socket, boost::asio::buffer(*payload),
      [weakSelf, id, client, payload](const boost::system::error_code &error, std::size_t) {
        const auto self = weakSelf.lock();
        if (!self) {
          return;
        }
        if (error) {
          {
            std::lock_guard<std::mutex> lock(client->writeMutex);
            client->writeQueue.clear();
            client->writeInProgress = false;
          }
          if (error != boost::asio::error::operation_aborted) {
            std::cout << "Error writing to TCP client " << id << ": " << error.message()
                      << std::endl;
          }
          self->removeClient(id);
          return;
        }

        bool hasNext = false;
        {
          std::lock_guard<std::mutex> lock(client->writeMutex);
          if (!client->writeQueue.empty() && client->writeQueue.front() == payload) {
            client->writeQueue.pop_front();
          }
          hasNext = !client->writeQueue.empty();
          client->writeInProgress = hasNext;
        }
        if (hasNext && self->getClientState(id) == client) {
          self->startClientWrite(id, client);
        }
      });
}

void MultiplexSession::startAsyncRead(const std::string &id) {
  const auto client = getClientState(id);
  if (!client) {
    std::cout << "Error: Socket is null for id " << id << std::endl;
    return;
  }

  const std::weak_ptr weakSelf = weak_from_this();
  client->socket->async_read_some(
      boost::asio::buffer(client->readBuffer),
      [weakSelf, id, client](const boost::system::error_code &error, std::size_t bytesTransferred) {
        const auto self = weakSelf.lock();
        if (!self) {
          return;
        }
        if (!error) {
          if (bytesTransferred > 0) {
            self->sendTunnelPacket(id, client->readBuffer.data(), bytesTransferred, 0);
            if (self->sendBlocked_.load(std::memory_order_relaxed)) {
              std::lock_guard<std::mutex> lock(self->pausedMutex_);
              self->pausedReads_.insert(id);
              return;
            }
          }
          self->startAsyncRead(id);
        } else {
          if (error != boost::asio::error::operation_aborted) {
            std::cout << "Error reading from TCP client " << id << ": " << error.message()
                      << std::endl;
          }
          self->removeClient(id);
        }
      });
}

void MultiplexSession::resumePausedReads() {
  std::vector<std::string> toResume;
  {
    std::lock_guard<std::mutex> lock(pausedMutex_);
    toResume.assign(pausedReads_.begin(), pausedReads_.end());
    pausedReads_.clear();
  }
  for (const auto &pausedId : toResume) {
    startAsyncRead(pausedId);
  }
}

bool MultiplexSession::isSendSaturated() {
  if (sendBlocked_.load(std::memory_order_relaxed)) {
    const auto elapsed = std::chrono::steady_clock::duration{
        clockTicks() - lastBlockedTicks_.load(std::memory_order_relaxed)};
    if (elapsed < std::chrono::milliseconds(backoffMs_.load())) {
      return true;
    }
    // Time to retry; keep going but do not clear the flag yet until we send.
  }

  const std::size_t pending = channel_->pendingReliableBytes();
  if (pending >= kHighWaterBytes) {
      lastBlockedTicks_.store(clockTicks(), std::memory_order_relaxed);
      int current = backoffMs_.load(std::memory_order_relaxed);
      int next = std::min(current * 2, 200);
      backoffMs_.store(next, std::memory_order_relaxed);
      sendBlocked_.store(true, std::memory_order_relaxed);
      return true;
  }
  if (pending <= kLowWaterBytes) {
      sendBlocked_.store(false, std::memory_order_relaxed);
      backoffMs_.store(5, std::memory_order_relaxed);
      return false;
  }

  return sendBlocked_.load(std::memory_order_relaxed);
}

void MultiplexSession::removeFromOrder(const std::string &id) {
  sendOrderSet_.erase(id);
  sendOrder_.erase(std::remove(sendOrder_.begin(), sendOrder_.end(), id), sendOrder_.end());
}

} // namespace connecttool::network
