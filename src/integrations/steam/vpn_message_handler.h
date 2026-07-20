#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <isteamnetworkingmessages.h>
#include <memory>
#include <steamnetworkingtypes.h>
#include <thread>

class SteamVpnNetworkingManager;

class VpnMessageHandler {
public:
  VpnMessageHandler(ISteamNetworkingMessages *interface, SteamVpnNetworkingManager *manager);
  ~VpnMessageHandler();

  void start();
  void stop();

private:
  void schedulePoll();
  void pollMessages();
  void runInternalLoop(std::stop_token stopToken);

  ISteamNetworkingMessages *interface_;
  SteamVpnNetworkingManager *manager_;

  boost::asio::io_context ioContext_;
  std::unique_ptr<boost::asio::steady_timer> pollTimer_;
  std::jthread ioThread_;

  std::atomic<bool> running_;
  std::chrono::microseconds currentPollInterval_;

  static constexpr int VPN_CHANNEL = 0;
  static constexpr std::chrono::microseconds MIN_POLL_INTERVAL{100};
  static constexpr std::chrono::microseconds MAX_POLL_INTERVAL{1000};
  static constexpr std::chrono::microseconds POLL_INCREMENT{100};
};
