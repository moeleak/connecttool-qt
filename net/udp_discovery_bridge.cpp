#include "udp_discovery_bridge.h"

#include <cstring>
#include <iostream>
#include <vector>

#include <isteamnetworkingsockets.h>

namespace {
constexpr uint16_t kMcLanPort = 4445;
constexpr std::size_t kHeaderSize = 9; // "UDPB" + type + id + len

// Packet format:
// 0-3: 'U' 'D' 'P' 'B'
// 4:   type (0=request, 1=response)
// 5-6: request id (uint16 little endian)
// 7-8: payload length (uint16 little endian)
} // namespace

UdpDiscoveryBridge::UdpDiscoveryBridge(
    boost::asio::io_context &io_context, ISteamNetworkingSockets *steamInterface,
    HSteamNetConnection steamConn, bool isHost)
    : io_context_(io_context), steamInterface_(steamInterface),
      steamConn_(steamConn), isHost_(isHost),
      socket_(io_context, udp::endpoint(udp::v4(), kMcLanPort)) {
  boost::system::error_code ec;
  socket_.set_option(boost::asio::socket_base::reuse_address(true), ec);
  socket_.set_option(boost::asio::socket_base::broadcast(true), ec);
}

UdpDiscoveryBridge::~UdpDiscoveryBridge() { stop(); }

void UdpDiscoveryBridge::start() {
  if (running_) {
    return;
  }
  running_ = true;
  startReceive();
  std::cout << "[UDPBridge] Listening for LAN discovery on udp/4445 as "
            << (isHost_ ? "host" : "client") << std::endl;
}

void UdpDiscoveryBridge::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  boost::system::error_code ec;
  socket_.close(ec);
}

void UdpDiscoveryBridge::startReceive() {
  socket_.async_receive_from(
      boost::asio::buffer(recvBuffer_), remoteEndpoint_,
      [this](const boost::system::error_code &ec, std::size_t bytes) {
        onReceive(ec, bytes);
      });
}

void UdpDiscoveryBridge::onReceive(const boost::system::error_code &ec,
                                   std::size_t bytes) {
  if (!running_) {
    return;
  }
  if (!ec && bytes > 0) {
    if (!isHost_) {
      // Remote client: forward local broadcast to host
      uint16_t id = nextRequestId_++;
      pendingEndpoints_[id] = remoteEndpoint_;
      sendToSteam(0, id, recvBuffer_.data(), bytes);
    } else {
      // Host: treat as response from local LAN server
      if (activeRequestId_) {
        sendToSteam(1, *activeRequestId_, recvBuffer_.data(), bytes);
      }
    }
  }
  startReceive();
}

void UdpDiscoveryBridge::handleFromSteam(const char *data, std::size_t len) {
  if (len < kHeaderSize) {
    return;
  }
  if (std::memcmp(data, "UDPB", 4) != 0) {
    return;
  }
  const uint8_t type = static_cast<uint8_t>(data[4]);
  uint16_t id = 0;
  uint16_t payloadLen = 0;
  std::memcpy(&id, data + 5, sizeof(uint16_t));
  std::memcpy(&payloadLen, data + 7, sizeof(uint16_t));
  if (len < kHeaderSize + payloadLen) {
    return;
  }
  const char *payload = data + kHeaderSize;

  if (type == 0 && isHost_) {
    // Forward request into local LAN as broadcast
    forwardToBroadcast(payload, payloadLen, id);
  } else if (type == 1 && !isHost_) {
    forwardResponseToLocal(id, payload, payloadLen);
  }
}

void UdpDiscoveryBridge::sendToSteam(uint8_t type, uint16_t id,
                                     const char *payload, std::size_t len) {
  std::vector<char> packet(kHeaderSize + len);
  std::memcpy(packet.data(), "UDPB", 4);
  packet[4] = static_cast<char>(type);
  std::memcpy(packet.data() + 5, &id, sizeof(uint16_t));
  uint16_t payloadLen = static_cast<uint16_t>(len);
  std::memcpy(packet.data() + 7, &payloadLen, sizeof(uint16_t));
  if (len > 0 && payload) {
    std::memcpy(packet.data() + kHeaderSize, payload, len);
  }

  steamInterface_->SendMessageToConnection(
      steamConn_, packet.data(), static_cast<uint32>(packet.size()),
      k_nSteamNetworkingSend_Reliable, nullptr);
}

void UdpDiscoveryBridge::forwardToBroadcast(const char *payload,
                                            std::size_t len,
                                            uint16_t requestId) {
  activeRequestId_ = requestId;
  udp::endpoint broadcast(boost::asio::ip::address_v4::broadcast(),
                          kMcLanPort);
  socket_.async_send_to(
      boost::asio::buffer(payload, len), broadcast,
      [](const boost::system::error_code &sendEc, std::size_t) {
        if (sendEc) {
          std::cerr << "[UDPBridge] Failed to broadcast LAN request: "
                    << sendEc.message() << std::endl;
        }
      });
}

void UdpDiscoveryBridge::forwardResponseToLocal(uint16_t id,
                                                const char *payload,
                                                std::size_t len) {
  auto it = pendingEndpoints_.find(id);
  if (it == pendingEndpoints_.end()) {
    return;
  }
  const udp::endpoint endpoint = it->second;
  socket_.async_send_to(
      boost::asio::buffer(payload, len), endpoint,
      [endpoint](const boost::system::error_code &sendEc, std::size_t) {
        if (sendEc) {
          std::cerr << "[UDPBridge] Failed to send LAN response to "
                    << endpoint << ": " << sendEc.message() << std::endl;
        }
      });
}
