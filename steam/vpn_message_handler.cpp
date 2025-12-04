#include "vpn_message_handler.h"
#include "steam_vpn_networking_manager.h"
#include "steam_vpn_bridge.h"
#include "net/vpn_protocol.h"
#include <algorithm>
#include <iostream>
#include <steam_api.h>
#include <isteamnetworkingmessages.h>

VpnMessageHandler::VpnMessageHandler(ISteamNetworkingMessages *interface,
                                     SteamVpnNetworkingManager *manager)
    : interface_(interface), manager_(manager),
      internalIoContext_(std::make_unique<boost::asio::io_context>()),
      ioContext_(internalIoContext_.get()), running_(false),
      currentPollInterval_(MIN_POLL_INTERVAL) {}

VpnMessageHandler::~VpnMessageHandler() { stop(); }

void VpnMessageHandler::setIoContext(boost::asio::io_context *externalContext) {
  if (!running_ && externalContext) {
    ioContext_ = externalContext;
  }
}

void VpnMessageHandler::start() {
  if (running_) {
    return;
  }
  running_ = true;
  if (ioContext_ == internalIoContext_.get() && internalIoContext_) {
    internalIoContext_->restart();
  }
  pollTimer_ = std::make_unique<boost::asio::steady_timer>(*ioContext_);
  schedulePoll();
  if (ioContext_ == internalIoContext_.get()) {
    ioThread_ =
        std::make_unique<std::thread>(&VpnMessageHandler::runInternalLoop, this);
  }
}

void VpnMessageHandler::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  if (pollTimer_) {
    pollTimer_->cancel();
    // If we're using an external io_context (shared with the app), drain the
    // canceled handler now so it doesn't fire after this object is destroyed.
    if (ioContext_ && ioContext_ != internalIoContext_.get()) {
      ioContext_->poll();
    }
  }
  if (ioContext_ == internalIoContext_.get() && internalIoContext_) {
    internalIoContext_->stop();
  }
  if (ioThread_ && ioThread_->joinable()) {
    ioThread_->join();
  }
  pollTimer_.reset();
  ioThread_.reset();
}

void VpnMessageHandler::runInternalLoop() {
  auto workGuard =
      boost::asio::make_work_guard(*internalIoContext_);
  while (running_) {
    try {
      internalIoContext_->run();
      break;
    } catch (const std::exception &e) {
      std::cerr << "Exception in VPN message handler loop: " << e.what()
                << std::endl;
      if (running_) {
        internalIoContext_->restart();
      }
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
  const int numMsgs =
      interface_->ReceiveMessagesOnChannel(VPN_CHANNEL, incoming, 64);
  for (int i = 0; i < numMsgs; ++i) {
    ISteamNetworkingMessage *msg = incoming[i];
    const uint8_t *data = static_cast<const uint8_t *>(msg->m_pData);
    const size_t size = msg->m_cbSize;
    const CSteamID sender = msg->m_identityPeer.GetSteamID();
    if (size >= sizeof(VpnMessageHeader) &&
        static_cast<VpnMessageType>(data[0]) == VpnMessageType::SESSION_HELLO) {
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
    currentPollInterval_ =
        std::min(currentPollInterval_ + POLL_INCREMENT, MAX_POLL_INTERVAL);
  }
}
