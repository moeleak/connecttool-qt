#include "vpn_message_handler.h"
#include "net/vpn_protocol.h"
#include "net/wire_codec.h"
#include "steam_vpn_bridge.h"
#include "steam_vpn_networking_manager.h"
#include <algorithm>
#include <iostream>
#include <isteamnetworkingmessages.h>
#include <steam_api.h>

VpnMessageHandler::VpnMessageHandler(ISteamNetworkingMessages *interface,
                                     SteamVpnNetworkingManager *manager)
    : interface_(interface), manager_(manager), running_(false),
      currentPollInterval_(MIN_POLL_INTERVAL) {}

VpnMessageHandler::~VpnMessageHandler() { stop(); }

void VpnMessageHandler::start() {
  if (running_.exchange(true)) {
    return;
  }
  ioContext_.restart();
  currentPollInterval_ = MIN_POLL_INTERVAL;
  pollTimer_ = std::make_unique<boost::asio::steady_timer>(ioContext_);
  schedulePoll();
  ioThread_ = std::jthread([this](std::stop_token stopToken) { runInternalLoop(stopToken); });
}

void VpnMessageHandler::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  ioContext_.stop();
  if (ioThread_.joinable()) {
    ioThread_.request_stop();
    ioThread_.join();
  }
  pollTimer_.reset();
}

void VpnMessageHandler::runInternalLoop(std::stop_token stopToken) {
  while (running_ && !stopToken.stop_requested()) {
    try {
      ioContext_.run();
      break;
    } catch (const std::exception &e) {
      std::cerr << "Exception in VPN message handler loop: " << e.what() << std::endl;
    }
  }
}

void VpnMessageHandler::schedulePoll() {
  if (!running_ || !pollTimer_) {
    return;
  }
  pollTimer_->expires_after(currentPollInterval_);
  pollTimer_->async_wait([this](const boost::system::error_code &ec) {
    if (!ec && running_) {
      pollMessages();
      schedulePoll();
    }
  });
}

void VpnMessageHandler::pollMessages() {
  if (!interface_) {
    return;
  }
  ISteamNetworkingMessage *incoming[64];
  const int numMsgs = interface_->ReceiveMessagesOnChannel(VPN_CHANNEL, incoming, 64);
  for (int i = 0; i < numMsgs; ++i) {
    ISteamNetworkingMessage *msg = incoming[i];
    const uint8_t *data = static_cast<const uint8_t *>(msg->m_pData);
    const size_t size = msg->m_cbSize;
    const CSteamID sender = msg->m_identityPeer.GetSteamID();
    const auto envelope = connecttool::wire::decodeEnvelope(std::as_bytes(std::span{data, size}));
    if (envelope && envelope->type == VpnMessageType::SESSION_HELLO) {
      msg->Release();
      continue;
    }
    if (manager_) {
      manager_->handleIncomingVpnMessage(data, size, sender);
    }
    msg->Release();
  }
  if (numMsgs > 0) {
    currentPollInterval_ = MIN_POLL_INTERVAL;
  } else {
    currentPollInterval_ = std::min(currentPollInterval_ + POLL_INCREMENT, MAX_POLL_INTERVAL);
  }
}
