#include "multiplex_manager.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>

namespace {
constexpr std::size_t kTunnelChunkBytes = 32 * 1024;
constexpr std::size_t kSendBufferBytes = 8 * 1024 * 1024;
constexpr std::size_t kHighWaterBytes = 6 * 1024 * 1024; // throttle before Steam limits
constexpr std::size_t kLowWaterBytes = 4 * 1024 * 1024;

// Simple, local ID generator to avoid pulling in the full nanoid dependency
std::string generateId(std::size_t length = 6) {
  static constexpr char chars[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
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

MultiplexManager::MultiplexManager(ISteamNetworkingSockets *steamInterface,
                                   HSteamNetConnection steamConn,
                                   boost::asio::io_context &io_context,
                                   bool &isHost, int &localPort)
    : steamInterface_(steamInterface), steamConn_(steamConn),
      io_context_(io_context), isHost_(isHost), localPort_(localPort) {
  sendTimer_ = std::make_unique<boost::asio::steady_timer>(io_context_);
}

MultiplexManager::~MultiplexManager() {
  // Close all sockets
  std::lock_guard<std::mutex> lock(mapMutex_);
  for (auto &pair : clientMap_) {
    pair.second->close();
  }
  clientMap_.clear();
}

std::string MultiplexManager::addClient(std::shared_ptr<tcp::socket> socket) {
  std::string id;
  {
    std::lock_guard<std::mutex> lock(mapMutex_);
    do {
      id = generateId(6);
    } while (clientMap_.find(id) != clientMap_.end());

    clientMap_[id] = socket;
    readBuffers_[id].resize(1048576);
    missingClients_.erase(id);
  }
  startAsyncRead(id);
  std::cout << "Added client with id " << id << std::endl;
  return id;
}

bool MultiplexManager::removeClient(const std::string &id) {
  bool removed = false;
  std::lock_guard<std::mutex> lock(mapMutex_);
  auto it = clientMap_.find(id);
  if (it != clientMap_.end()) {
    it->second->close();
    clientMap_.erase(it);
    removed = true;
  }
  readBuffers_.erase(id);
  missingClients_.erase(id);
  {
    std::lock_guard<std::mutex> lock(pausedMutex_);
    pausedReads_.erase(id);
  }

  if (removed) {
    std::cout << "Removed client with id " << id << std::endl;
  }
  bool shouldResume = false;
  {
    std::lock_guard<std::mutex> queueLock(queueMutex_);
    pendingPackets_.erase(id);
    if (pendingPackets_.empty()) {
      sendBlocked_.store(false, std::memory_order_relaxed);
      shouldResume = true;
    }
  }
  if (shouldResume) {
    resumePausedReads();
  }
  return removed;
}

std::shared_ptr<tcp::socket>
MultiplexManager::getClient(const std::string &id) {
  std::lock_guard<std::mutex> lock(mapMutex_);
  auto it = clientMap_.find(id);
  if (it != clientMap_.end()) {
    return it->second;
  }
  return nullptr;
}

std::vector<char> MultiplexManager::buildPacket(const std::string &id,
                                                const char *data, size_t len,
                                                int type) const {
  const size_t idLen = id.size() + 1;
  const size_t payloadLen = (type == 0 ? len : 0);
  const size_t packetSize = idLen + sizeof(uint32_t) + payloadLen;
  std::vector<char> packet(packetSize);
  std::memcpy(packet.data(), id.c_str(), idLen);
  auto *pType = reinterpret_cast<uint32_t *>(packet.data() + idLen);
  *pType = type;
  if (payloadLen > 0 && data) {
    std::memcpy(packet.data() + idLen + sizeof(uint32_t), data, payloadLen);
  }
  return packet;
}

bool MultiplexManager::trySendPacket(const std::vector<char> &packet) {
  if (packet.empty()) {
    return true;
  }
  if (isSendSaturated()) {
    return false;
  }
  EResult result = steamInterface_->SendMessageToConnection(
      steamConn_, packet.data(), static_cast<uint32>(packet.size()),
      k_nSteamNetworkingSend_Reliable, nullptr);
  if (result == k_EResultOK) {
    backoffMs_.store(5, std::memory_order_relaxed);
    return true;
  }
  if (result == k_EResultLimitExceeded) {
    lastBlocked_ = std::chrono::steady_clock::now();
    int current = backoffMs_.load(std::memory_order_relaxed);
    int next = std::min(current * 2, 100);
    backoffMs_.store(next, std::memory_order_relaxed);
    sendBlocked_.store(true, std::memory_order_relaxed);
    return false;
  }

  if (result != k_EResultNoConnection && result != k_EResultInvalidParam) {
    std::cerr << "[Multiplex] SendMessageToConnection failed with result "
              << static_cast<int>(result) << std::endl;
  }
  return true;
}

void MultiplexManager::enqueuePacket(const std::string &id,
                                     std::vector<char> packet) {
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    pendingPackets_[id].push_back(std::move(packet));
  }
  scheduleFlush();
}

void MultiplexManager::flushPendingPackets() {
  if (isSendSaturated()) {
    return;
  }

  std::unique_lock<std::mutex> lock(queueMutex_);
  for (auto it = pendingPackets_.begin(); it != pendingPackets_.end();) {
    auto &queue = it->second;
    while (!queue.empty()) {
      const std::vector<char> &packet = queue.front();
      lock.unlock();
      const bool sent = trySendPacket(packet);
      lock.lock();
      if (!sent) {
        sendBlocked_.store(true, std::memory_order_relaxed);
        return;
      }
      queue.pop_front();
    }
    if (queue.empty()) {
      it = pendingPackets_.erase(it);
    } else {
      ++it;
    }
  }
  sendBlocked_.store(false, std::memory_order_relaxed);
  lock.unlock();
  resumePausedReads();
}

void MultiplexManager::scheduleFlush(std::chrono::milliseconds delay) {
  bool needSchedule = false;
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (!flushScheduled_ && !pendingPackets_.empty()) {
      flushScheduled_ = true;
      needSchedule = true;
    }
  }
  if (!needSchedule) {
    return;
  }

  auto nextDelay = delay;
  if (sendBlocked_.load(std::memory_order_relaxed)) {
    nextDelay = std::max(nextDelay,
                         std::chrono::milliseconds(backoffMs_.load()));
  }

  sendTimer_->expires_after(nextDelay);
  sendTimer_->async_wait([this](const boost::system::error_code &ec) {
    if (!ec) {
      flushPendingPackets();
    }
    bool shouldReschedule = false;
    auto rescheduleDelay = std::chrono::milliseconds(5);
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      flushScheduled_ = false;
      shouldReschedule = !pendingPackets_.empty();
      if (sendBlocked_.load(std::memory_order_relaxed)) {
        rescheduleDelay =
            std::chrono::milliseconds(backoffMs_.load(std::memory_order_relaxed));
      }
    }
    if (shouldReschedule) {
      scheduleFlush(rescheduleDelay);
    }
  });
}

void MultiplexManager::sendTunnelPacket(const std::string &id, const char *data,
                                        size_t len, int type) {
  bool blocked = false;
  auto pushPacket = [this, &id, &blocked](const char *ptr, size_t amount,
                                          int packetType) {
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
    lastBlocked_ = std::chrono::steady_clock::now();
  }
}

void MultiplexManager::handleTunnelPacket(const char *data, size_t len) {
  size_t idLen = 7; // 6 + null
  if (len < idLen + sizeof(uint32_t)) {
    std::cerr << "Invalid tunnel packet size" << std::endl;
    return;
  }
  std::string id(data, 6);
  uint32_t type = *reinterpret_cast<const uint32_t *>(data + idLen);
  if (type == 0) {
    // Data packet
    size_t dataLen = len - idLen - sizeof(uint32_t);
    const char *packetData = data + idLen + sizeof(uint32_t);
    auto socket = getClient(id);
    if (!socket && isHost_ && localPort_ > 0) {
      // 如果是主持且没有对应的 TCP Client，创建一个连接到本地端口
      std::cout << "Creating new TCP client for id " << id
                << " connecting to localhost:" << localPort_ << std::endl;
      try {
        auto newSocket = std::make_shared<tcp::socket>(io_context_);
        tcp::resolver resolver(io_context_);
        auto endpoints =
            resolver.resolve("127.0.0.1", std::to_string(localPort_));
        boost::asio::connect(*newSocket, endpoints);

        std::string tempId = id;
        {
          std::lock_guard<std::mutex> lock(mapMutex_);
          clientMap_[id] = newSocket;
          readBuffers_[id].resize(1048576);
          socket = newSocket;
        }
        std::cout << "Successfully created TCP client for id " << id
                  << std::endl;
        startAsyncRead(tempId);
      } catch (const std::exception &e) {
        std::cerr << "Failed to create TCP client for id " << id << ": "
                  << e.what() << std::endl;
        sendTunnelPacket(id, nullptr, 0, 1);
        return;
      }
    }
    if (socket) {
      missingClients_.erase(id);
      auto payload =
          std::make_shared<std::vector<char>>(packetData, packetData + dataLen);
      boost::asio::async_write(
          *socket, boost::asio::buffer(*payload),
          [this, id, payload](const boost::system::error_code &writeEc,
                              std::size_t) {
            if (writeEc) {
              std::cout << "Error writing to TCP client " << id << ": "
                        << writeEc.message() << std::endl;
              removeClient(id);
            }
          });
    } else {
      if (missingClients_.insert(id).second) {
        std::cerr << "No client found for id " << id << std::endl;
      }
      sendTunnelPacket(id, nullptr, 0, 1);
    }
  } else if (type == 1) {
    // Disconnect packet
    if (removeClient(id)) {
      std::cout << "Client " << id << " disconnected" << std::endl;
    }
  } else {
    std::cerr << "Unknown packet type " << type << std::endl;
  }
}

void MultiplexManager::startAsyncRead(const std::string &id) {
  auto socket = getClient(id);
  if (!socket) {
    std::cout << "Error: Socket is null for id " << id << std::endl;
    return;
  }
  socket->async_read_some(
      boost::asio::buffer(readBuffers_[id]),
      [this, id](const boost::system::error_code &ec,
                 std::size_t bytes_transferred) {
        if (!ec) {
          if (bytes_transferred > 0) {
            sendTunnelPacket(id, readBuffers_[id].data(), bytes_transferred, 0);
            if (sendBlocked_.load(std::memory_order_relaxed)) {
              std::lock_guard<std::mutex> lock(pausedMutex_);
              pausedReads_.insert(id);
              return;
            }
          }
          startAsyncRead(id);
        } else {
          std::cout << "Error reading from TCP client " << id << ": "
                    << ec.message() << std::endl;
          removeClient(id);
        }
      });
}

void MultiplexManager::resumePausedReads() {
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

bool MultiplexManager::isSendSaturated() {
  if (sendBlocked_.load(std::memory_order_relaxed)) {
    auto elapsed = std::chrono::steady_clock::now() - lastBlocked_;
    if (elapsed < std::chrono::milliseconds(backoffMs_.load())) {
      return true;
    }
    // Time to retry; keep going but do not clear the flag yet until we send.
  }

  SteamNetConnectionRealTimeStatus_t status{};
  if (steamInterface_->GetConnectionRealTimeStatus(steamConn_, &status, 0,
                                                   nullptr)) {
    const std::size_t pending =
        static_cast<std::size_t>(status.m_cbPendingReliable);
    if (pending >= kHighWaterBytes) {
      lastBlocked_ = std::chrono::steady_clock::now();
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
  }

  return sendBlocked_.load(std::memory_order_relaxed);
}
