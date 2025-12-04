#include "steam_vpn_bridge.h"
#include "steam_vpn_networking_manager.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <steam_api.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace {
constexpr const char *kDefaultTunName = "SteamVPN";
constexpr const char *kDefaultSubnet = "10.0.0.0";
constexpr const char *kDefaultSubnetMask = "255.0.0.0";
constexpr int kDefaultMtu = 1400;
} // namespace

SteamVpnBridge::SteamVpnBridge(SteamVpnNetworkingManager *steamManager)
    : steamManager_(steamManager), running_(false), baseIP_(0), subnetMask_(0),
      localIP_(0) {
  std::memset(&stats_, 0, sizeof(stats_));
}

SteamVpnBridge::~SteamVpnBridge() { stop(); }

bool SteamVpnBridge::start(const std::string &tunDeviceName,
                           const std::string &virtualSubnet,
                           const std::string &subnetMask, int mtu) {
  if (running_) {
    std::cerr << "VPN bridge already running" << std::endl;
    return false;
  }
  ipNegotiator_.reset();
  heartbeatManager_.reset();
  if (!steamManager_) {
    std::cerr << "Steam manager missing, cannot start VPN bridge"
              << std::endl;
    return false;
  }

  const int mtuToUse = mtu > 0 ? mtu : kDefaultMtu;

  tunDevice_ = tun::create_tun();
  if (!tunDevice_) {
    std::cerr << "Failed to create TUN device" << std::endl;
    return false;
  }
  if (!tunDevice_->open(
          tunDeviceName.empty() ? kDefaultTunName : tunDeviceName, mtuToUse)) {
    std::cerr << "Failed to open TUN device: " << tunDevice_->get_last_error()
              << std::endl;
    return false;
  }

  baseIP_ = stringToIp(virtualSubnet.empty() ? kDefaultSubnet : virtualSubnet);
  if (baseIP_ == 0) {
    std::cerr << "Invalid virtual subnet: " << virtualSubnet << std::endl;
    return false;
  }
  subnetMask_ =
      stringToIp(subnetMask.empty() ? kDefaultSubnetMask : subnetMask);

  const CSteamID mySteamID = SteamUser()->GetSteamID();
  ipNegotiator_.initialize(mySteamID, baseIP_, subnetMask_);
  ipNegotiator_.setSendCallback(
      [this](VpnMessageType type, const uint8_t *payload, size_t len,
             CSteamID targetSteamID, bool reliable) {
        sendVpnMessage(type, payload, len, targetSteamID, reliable);
      },
      [this](VpnMessageType type, const uint8_t *payload, size_t len,
             bool reliable) {
        broadcastVpnMessage(type, payload, len, reliable);
      });

  ipNegotiator_.setSuccessCallback(
      [this](uint32_t ip, const NodeID &nodeId) {
        onNegotiationSuccess(ip, nodeId);
      });

  heartbeatManager_.setSendCallback(
      [this](VpnMessageType type, const uint8_t *payload, size_t len,
             bool reliable) {
        broadcastVpnMessage(type, payload, len, reliable);
      });
  heartbeatManager_.setNodeExpiredCallback(
      [this](const NodeID &nodeId, uint32_t ip) {
        onNodeExpired(nodeId, ip);
      });

  ipNegotiator_.startNegotiation();
  tunDevice_->set_non_blocking(true);

  running_ = true;
  tunReadThread_ =
      std::make_unique<std::thread>(&SteamVpnBridge::tunReadThread, this);
  std::cout << "Steam VPN bridge started successfully" << std::endl;
  return true;
}

void SteamVpnBridge::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  heartbeatManager_.stop();
  if (tunDevice_) {
    tunDevice_->close(); // wake blocking reads
  }
  if (tunReadThread_ && tunReadThread_->joinable()) {
    tunReadThread_->join();
  }
  {
    std::lock_guard<std::mutex> lock(routingMutex_);
    routingTable_.clear();
  }
  ipNegotiator_.reset();
  heartbeatManager_.reset();
  localIP_ = 0;
  std::cout << "Steam VPN bridge stopped" << std::endl;
}

std::string SteamVpnBridge::getLocalIP() const {
  if (localIP_ == 0) {
    return {};
  }
  return ipToString(localIP_);
}

std::string SteamVpnBridge::getTunDeviceName() const {
  if (tunDevice_ && tunDevice_->is_open()) {
    return tunDevice_->get_device_name();
  }
  return {};
}

std::map<uint32_t, RouteEntry> SteamVpnBridge::getRoutingTable() const {
  std::lock_guard<std::mutex> lock(routingMutex_);
  return routingTable_;
}

void SteamVpnBridge::tunReadThread() {
  std::cout << "TUN read thread started" << std::endl;
  uint8_t buffer[2048];
  auto lastTimeoutCheck = std::chrono::steady_clock::now();

  while (running_) {
    const int bytesRead = tunDevice_ ? tunDevice_->read(buffer, sizeof(buffer))
                                     : -1;
    if (bytesRead > 0 && steamManager_) {
      const uint32_t destIP = extractDestIP(buffer, bytesRead);
      const uint32_t srcIP = extractSourceIP(buffer, bytesRead);
      uint8_t vpnPacket[2048 + sizeof(VpnMessageHeader) +
                        sizeof(VpnPacketWrapper)];
      auto *header = reinterpret_cast<VpnMessageHeader *>(vpnPacket);
      header->type = VpnMessageType::IP_PACKET;

      auto *wrapper = reinterpret_cast<VpnPacketWrapper *>(
          vpnPacket + sizeof(VpnMessageHeader));
      wrapper->senderNodeId = ipNegotiator_.getLocalNodeID();
      wrapper->sourceIP = htonl(srcIP);

      const size_t totalPayloadSize =
          sizeof(VpnPacketWrapper) + static_cast<size_t>(bytesRead);
      header->length = htons(static_cast<uint16_t>(totalPayloadSize));
      std::memcpy(vpnPacket + sizeof(VpnMessageHeader) +
                      sizeof(VpnPacketWrapper),
                  buffer, static_cast<size_t>(bytesRead));
      const uint32_t vpnPacketSize =
          static_cast<uint32_t>(sizeof(VpnMessageHeader) + totalPayloadSize);

      if (destIP == localIP_) {
        // Loopback traffic destined to our own TUN IP back into the stack.
        tunDevice_->write(buffer, static_cast<size_t>(bytesRead));
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.packetsReceived++;
        stats_.bytesReceived += static_cast<uint64_t>(bytesRead);
        std::cout << "[SteamVPN] Local loopback " << ipToString(srcIP) << " -> "
                  << ipToString(destIP) << " (" << bytesRead << " bytes)"
                  << std::endl;
      } else if (isBroadcastAddress(destIP)) {
        steamManager_->broadcastMessage(
            vpnPacket, vpnPacketSize,
            k_nSteamNetworkingSend_UnreliableNoNagle |
                k_nSteamNetworkingSend_NoDelay);
        const auto peers = steamManager_->getPeers();
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.packetsSent += peers.size();
        stats_.bytesSent += static_cast<uint64_t>(bytesRead) * peers.size();
        std::cout << "[SteamVPN] Broadcast " << ipToString(srcIP) << " -> "
                  << ipToString(destIP) << " to " << peers.size()
                  << " peers (" << bytesRead << " bytes)" << std::endl;
      } else {
        CSteamID targetSteamID;
        bool found = false;
        {
          std::lock_guard<std::mutex> lock(routingMutex_);
          auto it = routingTable_.find(destIP);
          if (it != routingTable_.end() && !it->second.isLocal) {
            targetSteamID = it->second.steamID;
            found = true;
          } else if (it != routingTable_.end() && it->second.isLocal) {
            // Target is ourselves; loop back.
            tunDevice_->write(buffer, static_cast<size_t>(bytesRead));
            std::lock_guard<std::mutex> lock2(statsMutex_);
            stats_.packetsReceived++;
            stats_.bytesReceived += static_cast<uint64_t>(bytesRead);
            std::cout << "[SteamVPN] Route loopback " << ipToString(srcIP)
                      << " -> " << ipToString(destIP) << " (" << bytesRead
                      << " bytes)" << std::endl;
          }
        }
        if (found) {
          steamManager_->sendMessageToUser(
              targetSteamID, vpnPacket, vpnPacketSize,
              k_nSteamNetworkingSend_UnreliableNoNagle |
                  k_nSteamNetworkingSend_NoDelay);
          std::lock_guard<std::mutex> lock(statsMutex_);
          stats_.packetsSent++;
          stats_.bytesSent += static_cast<uint64_t>(bytesRead);
          std::cout << "[SteamVPN] Sent " << ipToString(srcIP) << " -> "
                    << ipToString(destIP) << " (" << bytesRead
                    << " bytes) to " << targetSteamID.ConvertToUint64()
                    << std::endl;
        }
      }
    }

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                              lastTimeoutCheck)
            .count() >= 50) {
      lastTimeoutCheck = now;
      ipNegotiator_.checkTimeout();
    }
  }
  std::cout << "TUN read thread stopped" << std::endl;
}

void SteamVpnBridge::handleVpnMessage(const uint8_t *data, size_t length,
                                      CSteamID senderSteamID) {
  if (length < sizeof(VpnMessageHeader)) {
    return;
  }
  VpnMessageHeader header;
  std::memcpy(&header, data, sizeof(VpnMessageHeader));
  const uint16_t payloadLength = ntohs(header.length);
  if (length < sizeof(VpnMessageHeader) + payloadLength) {
    return;
  }
  const uint8_t *payload = data + sizeof(VpnMessageHeader);
  const std::string peerName = SteamFriends()
                                   ? SteamFriends()->GetFriendPersonaName(
                                         senderSteamID)
                                   : "";

  if (header.type == VpnMessageType::IP_PACKET) {
    if (tunDevice_ && payloadLength > sizeof(VpnPacketWrapper)) {
      VpnPacketWrapper wrapper{};
      std::memcpy(&wrapper, payload, sizeof(VpnPacketWrapper));
      const uint8_t *ipPacket = payload + sizeof(VpnPacketWrapper);
      const size_t ipPacketLen = payloadLength - sizeof(VpnPacketWrapper);
      const uint32_t destIP = extractDestIP(ipPacket, ipPacketLen);
      const uint32_t senderIP = ntohl(wrapper.sourceIP);

      CSteamID conflicting;
      const uint32_t conflictIP = senderIP != 0 ? senderIP : destIP;
      if (heartbeatManager_.detectConflict(conflictIP, wrapper.senderNodeId,
                                           conflicting) &&
          conflicting != senderSteamID) {
        sendVpnMessage(VpnMessageType::FORCED_RELEASE, payload, payloadLength,
                       conflicting, true);
      }

      if (destIP == localIP_ || isBroadcastAddress(destIP)) {
        tunDevice_->write(ipPacket, ipPacketLen);
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.packetsReceived++;
        stats_.bytesReceived += ipPacketLen;
      } else {
        CSteamID targetSteamID;
        bool found = false;
        {
          std::lock_guard<std::mutex> lock(routingMutex_);
          auto it = routingTable_.find(destIP);
          if (it != routingTable_.end() && !it->second.isLocal) {
            targetSteamID = it->second.steamID;
            found = true;
          }
        }
        if (found && targetSteamID != senderSteamID) {
          sendVpnMessage(VpnMessageType::IP_PACKET, payload, payloadLength,
                         targetSteamID, false);
        }
      }
    }
    return;
  }

  switch (header.type) {
  case VpnMessageType::ROUTE_UPDATE: {
    size_t offset = 0;
    while (offset + 12 <= payloadLength) {
      uint64_t steamID = 0;
      uint32_t ipAddress = 0;
      std::memcpy(&steamID, payload + offset, 8);
      std::memcpy(&ipAddress, payload + offset + 8, 4);
      ipAddress = ntohl(ipAddress);
      offset += 12;

      CSteamID csteamID(static_cast<uint64>(steamID));
      if (SteamUser() && csteamID == SteamUser()->GetSteamID()) {
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(routingMutex_);
        if (routingTable_.find(ipAddress) != routingTable_.end()) {
          continue;
        }
      }
      if ((ipAddress & subnetMask_) == (baseIP_ & subnetMask_)) {
        NodeID nodeId = NodeIdentity::generate(csteamID);
        updateRoute(nodeId, csteamID, ipAddress,
                    SteamFriends() ? SteamFriends()->GetFriendPersonaName(
                                         csteamID)
                                   : "");
      }
    }
    break;
  }
  case VpnMessageType::PROBE_REQUEST: {
    if (payloadLength >= sizeof(ProbeRequestPayload)) {
      ProbeRequestPayload request{};
      std::memcpy(&request, payload, sizeof(ProbeRequestPayload));
      ipNegotiator_.handleProbeRequest(request, senderSteamID);
    }
    break;
  }
  case VpnMessageType::PROBE_RESPONSE: {
    if (payloadLength >= sizeof(ProbeResponsePayload)) {
      ProbeResponsePayload response{};
      std::memcpy(&response, payload, sizeof(ProbeResponsePayload));
      ipNegotiator_.handleProbeResponse(response, senderSteamID);
    }
    break;
  }
  case VpnMessageType::ADDRESS_ANNOUNCE: {
    if (payloadLength >= sizeof(AddressAnnouncePayload)) {
      AddressAnnouncePayload announce{};
      std::memcpy(&announce, payload, sizeof(AddressAnnouncePayload));
      const uint32_t announcedIP = ntohl(announce.ipAddress);
      bool isNewRoute = false;
      {
        std::lock_guard<std::mutex> lock(routingMutex_);
        isNewRoute = routingTable_.find(announcedIP) == routingTable_.end();
      }
      ipNegotiator_.handleAddressAnnounce(announce, senderSteamID, peerName);
      updateRoute(announce.nodeId, senderSteamID, announcedIP, peerName);
      if (isNewRoute) {
        broadcastRouteUpdate();
      }
    }
    break;
  }
  case VpnMessageType::FORCED_RELEASE: {
    if (payloadLength >= sizeof(ForcedReleasePayload)) {
      ForcedReleasePayload release{};
      std::memcpy(&release, payload, sizeof(ForcedReleasePayload));
      ipNegotiator_.handleForcedRelease(release, senderSteamID);
    }
    break;
  }
  case VpnMessageType::HEARTBEAT: {
    if (payloadLength >= sizeof(HeartbeatPayload)) {
      HeartbeatPayload heartbeat{};
      std::memcpy(&heartbeat, payload, sizeof(HeartbeatPayload));
      heartbeatManager_.handleHeartbeat(heartbeat, senderSteamID, peerName);
    }
    break;
  }
  default:
    break;
  }
}

void SteamVpnBridge::onUserJoined(CSteamID steamID) {
  if (ipNegotiator_.getState() == NegotiationState::STABLE) {
    std::cout << "[SteamVPN] New peer joined, sending address/route: "
              << steamID.ConvertToUint64() << std::endl;
    ipNegotiator_.sendAddressAnnounceTo(steamID);
    sendRouteUpdateTo(steamID);
  }
}

void SteamVpnBridge::onUserLeft(CSteamID steamID) {
  std::lock_guard<std::mutex> lock(routingMutex_);
  for (auto it = routingTable_.begin(); it != routingTable_.end();) {
    if (it->second.steamID == steamID) {
      heartbeatManager_.unregisterNode(it->second.nodeId);
      ipNegotiator_.markIPUnused(it->first);
      it = routingTable_.erase(it);
    } else {
      ++it;
    }
  }
  if (SteamUser() && steamID == SteamUser()->GetSteamID()) {
    running_ = false;
    heartbeatManager_.stop();
    if (tunDevice_) {
      tunDevice_->close();
    }
    localIP_ = 0;
  }
}

SteamVpnBridge::Statistics SteamVpnBridge::getStatistics() const {
  std::lock_guard<std::mutex> lock(statsMutex_);
  return stats_;
}

void SteamVpnBridge::rebroadcastState() {
  if (ipNegotiator_.getState() != NegotiationState::STABLE) {
    return;
  }
  std::cout << "[SteamVPN] Rebroadcasting address and routes" << std::endl;
  ipNegotiator_.sendAddressAnnounce();
  broadcastRouteUpdate();
}

void SteamVpnBridge::onNegotiationSuccess(uint32_t ipAddress,
                                          const NodeID &nodeId) {
  localIP_ = ipAddress;
  const std::string localIPStr = ipToString(localIP_);
  const std::string subnetMaskStr = ipToString(subnetMask_);
  if (tunDevice_->set_ip(localIPStr, subnetMaskStr) &&
      tunDevice_->set_up(true)) {
    // Install a connected route for the virtual subnet so the OS sends traffic
    // into the TUN device.
    const uint32_t networkIp = baseIP_ & subnetMask_;
    const std::string networkStr = ipToString(networkIp);
    if (!tunDevice_->add_route(networkStr, subnetMaskStr)) {
      std::cerr << "Failed to add route to subnet " << networkStr << "/"
                << subnetMaskStr << " via " << tunDevice_->get_device_name()
                << std::endl;
    }

    const CSteamID mySteamID = SteamUser()->GetSteamID();
    updateRoute(nodeId, mySteamID, localIP_, SteamFriends()
                                                ? SteamFriends()->GetPersonaName()
                                                : "");
    heartbeatManager_.initialize(nodeId, localIP_);
    heartbeatManager_.registerNode(nodeId, mySteamID, localIP_,
                                   SteamFriends()
                                       ? SteamFriends()->GetPersonaName()
                                       : "");
    heartbeatManager_.start();
    broadcastRouteUpdate();
  } else {
    std::cerr << "Failed to configure TUN device IP." << std::endl;
    stop();
  }
}

void SteamVpnBridge::onNodeExpired(const NodeID &nodeId, uint32_t ipAddress) {
  removeRoute(ipAddress);
  ipNegotiator_.markIPUnused(ipAddress);
}

void SteamVpnBridge::updateRoute(const NodeID &nodeId, CSteamID steamId,
                                 uint32_t ipAddress, const std::string &name) {
  RouteEntry entry;
  entry.steamID = steamId;
  entry.ipAddress = ipAddress;
  entry.name = name;
  entry.isLocal = (SteamUser() && steamId == SteamUser()->GetSteamID());
  entry.nodeId = nodeId;

  {
    std::lock_guard<std::mutex> lock(routingMutex_);
    for (auto it = routingTable_.begin(); it != routingTable_.end();) {
      if (it->second.steamID == steamId && it->first != ipAddress) {
        it = routingTable_.erase(it);
      } else {
        ++it;
      }
    }
    routingTable_[ipAddress] = entry;
  }
  ipNegotiator_.markIPUsed(ipAddress);
  std::cout << "Route updated: " << ipToString(ipAddress) << " -> " << name
            << std::endl;
}

void SteamVpnBridge::removeRoute(uint32_t ipAddress) {
  std::lock_guard<std::mutex> lock(routingMutex_);
  routingTable_.erase(ipAddress);
}

void SteamVpnBridge::broadcastRouteUpdate() {
  std::vector<uint8_t> message;
  std::vector<uint8_t> routeData;

  {
    std::lock_guard<std::mutex> lock(routingMutex_);
    for (const auto &entry : routingTable_) {
      const uint64_t steamID = entry.second.steamID.ConvertToUint64();
      const uint32_t ipAddress = htonl(entry.second.ipAddress);
      const size_t offset = routeData.size();
      routeData.resize(offset + 12);
      std::memcpy(routeData.data() + offset, &steamID, 8);
      std::memcpy(routeData.data() + offset + 8, &ipAddress, 4);
    }
  }

  VpnMessageHeader header{};
  header.type = VpnMessageType::ROUTE_UPDATE;
  header.length = htons(static_cast<uint16_t>(routeData.size()));

  message.resize(sizeof(VpnMessageHeader) + routeData.size());
  std::memcpy(message.data(), &header, sizeof(VpnMessageHeader));
  if (!routeData.empty()) {
    std::memcpy(message.data() + sizeof(VpnMessageHeader), routeData.data(),
                routeData.size());
  }
  std::cout << "[SteamVPN] Broadcasting route update with "
            << (routeData.size() / 12) << " entries" << std::endl;
  steamManager_->broadcastMessage(message.data(),
                                  static_cast<uint32_t>(message.size()),
                                  k_nSteamNetworkingSend_Reliable);
}

void SteamVpnBridge::sendRouteUpdateTo(CSteamID targetSteamID) {
  std::vector<uint8_t> message;
  std::vector<uint8_t> routeData;
  {
    std::lock_guard<std::mutex> lock(routingMutex_);
    for (const auto &entry : routingTable_) {
      const uint64_t steamID = entry.second.steamID.ConvertToUint64();
      const uint32_t ipAddress = htonl(entry.second.ipAddress);
      const size_t offset = routeData.size();
      routeData.resize(offset + 12);
      std::memcpy(routeData.data() + offset, &steamID, 8);
      std::memcpy(routeData.data() + offset + 8, &ipAddress, 4);
    }
  }

  VpnMessageHeader header{};
  header.type = VpnMessageType::ROUTE_UPDATE;
  header.length = htons(static_cast<uint16_t>(routeData.size()));

  message.resize(sizeof(VpnMessageHeader) + routeData.size());
  std::memcpy(message.data(), &header, sizeof(VpnMessageHeader));
  if (!routeData.empty()) {
    std::memcpy(message.data() + sizeof(VpnMessageHeader), routeData.data(),
                routeData.size());
  }
  std::cout << "[SteamVPN] Sending route update to "
            << targetSteamID.ConvertToUint64() << " with "
            << (routeData.size() / 12) << " entries" << std::endl;
  steamManager_->sendMessageToUser(targetSteamID, message.data(),
                                   static_cast<uint32_t>(message.size()),
                                   k_nSteamNetworkingSend_Reliable);
}

void SteamVpnBridge::sendVpnMessage(VpnMessageType type,
                                    const uint8_t *payload,
                                    size_t payloadLength,
                                    CSteamID targetSteamID, bool reliable) {
  if (!steamManager_) {
    return;
  }
  std::vector<uint8_t> message;
  VpnMessageHeader header{};
  header.type = type;
  header.length = htons(static_cast<uint16_t>(payloadLength));
  message.resize(sizeof(VpnMessageHeader) + payloadLength);
  std::memcpy(message.data(), &header, sizeof(VpnMessageHeader));
  if (payloadLength > 0 && payload) {
    std::memcpy(message.data() + sizeof(VpnMessageHeader), payload,
                payloadLength);
  }
  const int flags =
      reliable ? k_nSteamNetworkingSend_Reliable
               : (k_nSteamNetworkingSend_UnreliableNoNagle |
                  k_nSteamNetworkingSend_NoDelay);
  steamManager_->sendMessageToUser(targetSteamID, message.data(),
                                   static_cast<uint32_t>(message.size()),
                                   flags);
}

void SteamVpnBridge::broadcastVpnMessage(VpnMessageType type,
                                         const uint8_t *payload,
                                         size_t payloadLength, bool reliable) {
  if (!steamManager_) {
    return;
  }
  std::vector<uint8_t> message;
  VpnMessageHeader header{};
  header.type = type;
  header.length = htons(static_cast<uint16_t>(payloadLength));
  message.resize(sizeof(VpnMessageHeader) + payloadLength);
  std::memcpy(message.data(), &header, sizeof(VpnMessageHeader));
  if (payloadLength > 0 && payload) {
    std::memcpy(message.data() + sizeof(VpnMessageHeader), payload,
                payloadLength);
  }
  const int flags =
      reliable ? k_nSteamNetworkingSend_Reliable
               : (k_nSteamNetworkingSend_UnreliableNoNagle |
                  k_nSteamNetworkingSend_NoDelay);
  steamManager_->broadcastMessage(message.data(),
                                  static_cast<uint32_t>(message.size()),
                                  flags);
}

std::string SteamVpnBridge::ipToString(uint32_t ip) {
  char buffer[INET_ADDRSTRLEN];
  in_addr addr{};
  addr.s_addr = htonl(ip);
  inet_ntop(AF_INET, &addr, buffer, INET_ADDRSTRLEN);
  return std::string(buffer);
}

uint32_t SteamVpnBridge::stringToIp(const std::string &ipStr) {
  in_addr addr{};
  if (inet_pton(AF_INET, ipStr.c_str(), &addr) == 1) {
    return ntohl(addr.s_addr);
  }
  return 0;
}

uint32_t SteamVpnBridge::extractDestIP(const uint8_t *packet, size_t length) {
  if (length < 20) {
    return 0;
  }
  const uint8_t version = (packet[0] >> 4) & 0x0F;
  if (version != 4) {
    return 0;
  }
  uint32_t destIP = 0;
  std::memcpy(&destIP, packet + 16, 4);
  return ntohl(destIP);
}

uint32_t SteamVpnBridge::extractSourceIP(const uint8_t *packet, size_t length) {
  if (length < 20) {
    return 0;
  }
  const uint8_t version = (packet[0] >> 4) & 0x0F;
  if (version != 4) {
    return 0;
  }
  uint32_t srcIP = 0;
  std::memcpy(&srcIP, packet + 12, 4);
  return ntohl(srcIP);
}

bool SteamVpnBridge::isBroadcastAddress(uint32_t ip) const {
  if (ip == 0xFFFFFFFF) {
    return true;
  }
  const uint32_t subnetBroadcast = (baseIP_ & subnetMask_) | (~subnetMask_);
  if (ip == subnetBroadcast) {
    return true;
  }
  const uint8_t firstOctet = (ip >> 24) & 0xFF;
  return firstOctet >= 224 && firstOctet <= 239;
}
