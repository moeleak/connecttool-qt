#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

#include <boost/asio.hpp>
#include <steamnetworkingtypes.h>

class ISteamNetworkingSockets;

using boost::asio::ip::udp;

class UdpDiscoveryBridge {
public:
  UdpDiscoveryBridge(boost::asio::io_context &io_context,
                     ISteamNetworkingSockets *steamInterface,
                     HSteamNetConnection steamConn, bool isHost);
  ~UdpDiscoveryBridge();

  void start();
  void stop();

  // Handle payload coming from the remote peer (over Steam).
  void handleFromSteam(const char *data, std::size_t len);

private:
  void startReceive();
  void onReceive(const boost::system::error_code &ec, std::size_t bytes);

  void sendToSteam(uint8_t type, uint16_t id, const char *payload,
                   std::size_t len);
  void forwardToBroadcast(const char *payload, std::size_t len,
                          uint16_t requestId);
  void forwardResponseToLocal(uint16_t id, const char *payload,
                              std::size_t len);

  boost::asio::io_context &io_context_;
  ISteamNetworkingSockets *steamInterface_;
  HSteamNetConnection steamConn_;
  bool isHost_;

  udp::socket socket_;
  std::array<char, 2048> recvBuffer_{};
  udp::endpoint remoteEndpoint_;

  uint16_t nextRequestId_{1};
  std::unordered_map<uint16_t, udp::endpoint> pendingEndpoints_;
  std::optional<uint16_t> activeRequestId_;
  bool running_{false};
};
