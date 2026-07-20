#include "steam_message_handler.h"
#include "integrations/steam/steam_reliable_channel.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <isteamnetworkingsockets.h>
#include <steam_api.h>

using connecttool::network::MultiplexSession;

SteamMessageHandler::SteamMessageHandler(Dependencies dependencies)
    : io_context_(dependencies.ioContext), m_pInterface_(&dependencies.networking),
      connections_(dependencies.connections), connectionsMutex_(dependencies.connectionsMutex),
      g_isHost_(dependencies.isHost), localPort_(dependencies.localPort), running_(false),
      currentPollInterval_(0) {}

SteamMessageHandler::~SteamMessageHandler() { stop(); }

void SteamMessageHandler::start() {
  if (running_.exchange(true)) {
    return;
  }
  timer_ = std::make_unique<boost::asio::steady_timer>(io_context_);
  boost::asio::dispatch(io_context_, [this]() { startAsyncPoll(); });
}

void SteamMessageHandler::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (timer_) {
    boost::asio::dispatch(io_context_, [timer = timer_.get()]() {
      try {
        timer->cancel();
      } catch (const boost::system::system_error &) {
        // The shared io_context may already be shutting down.
      }
    });
  }
  clearMultiplexSessions();
}

std::shared_ptr<MultiplexSession>
SteamMessageHandler::getMultiplexSession(HSteamNetConnection conn) {
  std::lock_guard<std::mutex> lock(multiplexSessionMutex_);
  const auto [position, inserted] = multiplexSessions_.try_emplace(conn);
  if (inserted) {
    position->second = std::make_shared<MultiplexSession>(
        MultiplexSession::Dependencies{.channel = std::make_shared<
                                           connecttool::steam::SteamReliableChannel>(
                                           *m_pInterface_, conn),
                                       .ioContext = io_context_,
                                       .isHost = g_isHost_,
                                       .localPort = localPort_});
  }
  return position->second;
}

void SteamMessageHandler::clearMultiplexSessions() {
  std::map<HSteamNetConnection, std::shared_ptr<MultiplexSession>> sessions;
  {
    std::lock_guard<std::mutex> lock(multiplexSessionMutex_);
    sessions.swap(multiplexSessions_);
  }
}

void SteamMessageHandler::startAsyncPoll() {
  if (!running_)
    return;

  // Poll networking callbacks
  m_pInterface_->RunCallbacks();

  // Receive messages and check if any were received
  int totalMessages = 0;
  std::vector<HSteamNetConnection> currentConnections;
  {
    std::lock_guard<std::mutex> lockConn(connectionsMutex_);
    currentConnections = connections_;
  }
  {
    std::lock_guard<std::mutex> lock(multiplexSessionMutex_);
    std::erase_if(multiplexSessions_, [&currentConnections](const auto &entry) {
      return std::ranges::find(currentConnections, entry.first) == currentConnections.end();
    });
  }
  for (auto conn : currentConnections) {
    ISteamNetworkingMessage *pIncomingMsgs[256]; // larger batch for throughput
    int numMsgs = m_pInterface_->ReceiveMessagesOnConnection(conn, pIncomingMsgs, 256);
    totalMessages += numMsgs;
    for (int i = 0; i < numMsgs; ++i) {
      ISteamNetworkingMessage *pIncomingMsg = pIncomingMsgs[i];
      const char *data = (const char *)pIncomingMsg->m_pData;
      size_t size = pIncomingMsg->m_cbSize;
      // Handle tunnel packets with multiplexing
      getMultiplexSession(conn)->handleTunnelPacket(data, size);
      pIncomingMsg->Release();
    }
  }

  // Adaptive polling: if messages received, poll immediately; otherwise
  // increase interval (keep small to avoid backlog)
  if (totalMessages > 0) {
    currentPollInterval_ = 0; // 有消息，立即轮询
  } else {
    // 无消息，逐渐增加间隔，最大2ms，优先低延迟
    currentPollInterval_ = std::min(currentPollInterval_ + 1, 2);
  }

  // Schedule next poll
  timer_->expires_after(std::chrono::milliseconds(currentPollInterval_));
  timer_->async_wait([this](const boost::system::error_code &error) {
    if (!error && running_) {
      startAsyncPoll();
    }
  });
}
