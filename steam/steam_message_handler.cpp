#include "steam_message_handler.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <isteamnetworkingsockets.h>
#include <steam_api.h>

SteamMessageHandler::SteamMessageHandler(
    boost::asio::io_context &io_context, ISteamNetworkingSockets *interface,
    std::vector<HSteamNetConnection> &connections, std::mutex &connectionsMutex,
    bool &g_isHost, int &localPort)
    : io_context_(io_context), m_pInterface_(interface),
      connections_(connections), connectionsMutex_(connectionsMutex),
      g_isHost_(g_isHost), localPort_(localPort), running_(false),
      currentPollInterval_(0) {}

SteamMessageHandler::~SteamMessageHandler() { stop(); }

void SteamMessageHandler::start() {
  if (running_)
    return;
  running_ = true;
  timer_ = std::make_unique<boost::asio::steady_timer>(io_context_);
  startAsyncPoll();
}

void SteamMessageHandler::stop() {
  if (!running_)
    return;
  running_ = false;
  if (timer_) {
    timer_->cancel();
  }
}

std::shared_ptr<MultiplexManager>
SteamMessageHandler::getMultiplexManager(HSteamNetConnection conn) {
  auto it = multiplexManagers_.find(conn);
  if (it == multiplexManagers_.end()) {
    auto manager = std::make_shared<MultiplexManager>(
        m_pInterface_, conn, io_context_, g_isHost_, localPort_);
    multiplexManagers_[conn] = manager;
    return manager;
  }
  return it->second;
}

std::shared_ptr<UdpDiscoveryBridge>
SteamMessageHandler::getUdpBridge(HSteamNetConnection conn) {
  auto it = udpBridges_.find(conn);
  if (it == udpBridges_.end()) {
    auto bridge = std::make_shared<UdpDiscoveryBridge>(
        io_context_, m_pInterface_, conn, g_isHost_);
    bridge->start();
    udpBridges_[conn] = bridge;
    return bridge;
  }
  return it->second;
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
  for (auto conn : currentConnections) {
    // Ensure UDP discovery bridge exists and is running for this connection
    getUdpBridge(conn);

    ISteamNetworkingMessage *pIncomingMsgs[10];
    int numMsgs =
        m_pInterface_->ReceiveMessagesOnConnection(conn, pIncomingMsgs, 10);
    totalMessages += numMsgs;
    for (int i = 0; i < numMsgs; ++i) {
      ISteamNetworkingMessage *pIncomingMsg = pIncomingMsgs[i];
      const char *data = (const char *)pIncomingMsg->m_pData;
      size_t size = pIncomingMsg->m_cbSize;
      // UDP LAN discovery bridge
      if (size >= 4 && std::memcmp(data, "UDPB", 4) == 0) {
        getUdpBridge(conn)->handleFromSteam(data, size);
      } else {
        // Handle tunnel packets with multiplexing
        getMultiplexManager(conn)->handleTunnelPacket(data, size);
      }
      pIncomingMsg->Release();
    }
  }

  // Adaptive polling: if messages received, poll immediately; otherwise
  // increase interval
  if (totalMessages > 0) {
    currentPollInterval_ = 0; // 有消息，立即轮询
  } else {
    // 无消息，逐渐增加间隔，最大10ms
    currentPollInterval_ = std::min(currentPollInterval_ + 1, 10);
  }

  // Schedule next poll
  timer_->expires_after(std::chrono::milliseconds(currentPollInterval_));
  timer_->async_wait([this](const boost::system::error_code &error) {
    if (!error && running_) {
      startAsyncPoll();
    }
  });
}
