#include "steam_vpn_bridge.h"
#include "integrations/steam/steam_id.h"
#include "network/protocol/wire_codec.h"
#include "steam_vpn_networking_manager.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>
#include <steam_api.h>
#include <thread>
#include <utility>

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
    : steamManager_(steamManager), running_(false), baseIP_(0), subnetMask_(0), localIP_(0) {
  std::memset(&stats_, 0, sizeof(stats_));
}

SteamVpnBridge::~SteamVpnBridge() { stop(); }

bool SteamVpnBridge::start() { return start(Configuration{}); }

bool SteamVpnBridge::start(Configuration configuration) {
  if (running_) {
    std::cerr << "VPN bridge already running" << std::endl;
    return false;
  }
  if (tunReadThread_.joinable() || tunDevice_) {
    stop();
  }
  {
    std::lock_guard lock(failureMutex_);
    lastFailure_.clear();
  }

  const auto failStart = [this](std::string message) {
    recordFailure(std::move(message));
    if (tunDevice_) {
      tunDevice_->close();
      tunDevice_.reset();
    }
    return false;
  };

  ipNegotiator_.reset();
  heartbeatManager_.reset();
  if (!steamManager_) {
    return failStart("Steam manager missing, cannot start VPN bridge");
  }

  const int mtuToUse = configuration.mtu > 0 ? configuration.mtu : kDefaultMtu;

  tunDevice_ = tun::create_tun();
  if (!tunDevice_) {
    return failStart("Failed to create TUN device");
  }
  if (!tunDevice_->open(configuration.deviceName.empty() ? kDefaultTunName
                                                         : configuration.deviceName,
                        mtuToUse)) {
    return failStart("Failed to open TUN device: " + tunDevice_->get_last_error());
  }

  baseIP_ = stringToIp(configuration.virtualSubnet.empty() ? kDefaultSubnet
                                                           : configuration.virtualSubnet);
  if (baseIP_ == 0) {
    return failStart("Invalid virtual subnet: " + configuration.virtualSubnet);
  }
  subnetMask_ =
      stringToIp(configuration.subnetMask.empty() ? kDefaultSubnetMask : configuration.subnetMask);

  const auto myPeerId = connecttool::steam::toPeerId(SteamUser()->GetSteamID());
  ipNegotiator_.initialize(myPeerId, baseIP_, subnetMask_);
  ipNegotiator_.setSendCallback(
      [this](VpnMessageType type, const uint8_t *payload, size_t len,
             connecttool::domain::PeerId targetPeerId,
             bool reliable) {
        sendVpnMessage({type, std::span{payload, len}, reliable}, targetPeerId);
      },
      [this](VpnMessageType type, const uint8_t *payload, size_t len, bool reliable) {
        broadcastVpnMessage({type, std::span{payload, len}, reliable});
      });

  ipNegotiator_.setSuccessCallback(
      [this](uint32_t ip, const NodeID &nodeId) { onNegotiationSuccess(ip, nodeId); });

  heartbeatManager_.setSendCallback(
      [this](VpnMessageType type, const uint8_t *payload, size_t len, bool reliable) {
        broadcastVpnMessage({type, std::span{payload, len}, reliable});
      });
  heartbeatManager_.setNodeExpiredCallback(
      [this](const NodeID &nodeId, uint32_t ip) { onNodeExpired(nodeId, ip); });

  ipNegotiator_.startNegotiation();
  tunDevice_->set_non_blocking(true);

  running_ = true;
  tunReadThread_ = std::jthread([this](std::stop_token stopToken) {
    try {
      tunReadLoop(stopToken);
    } catch (const std::exception &exception) {
      recordFailure("TUN worker failed: " + std::string{exception.what()});
    } catch (...) {
      recordFailure("TUN worker failed with an unknown exception");
    }
  });
  std::cout << "Steam VPN bridge started successfully" << std::endl;
  return true;
}

void SteamVpnBridge::stop() {
  const bool hadResources = running_.exchange(false) || tunReadThread_.joinable() || tunDevice_;
  if (!hadResources) {
    return;
  }

  heartbeatManager_.stop();
  if (tunReadThread_.joinable()) {
    tunReadThread_.request_stop();
    if (tunReadThread_.get_id() == std::this_thread::get_id()) {
      recordFailure("TUN cleanup was requested from its own worker thread");
      return;
    }
  }
  if (tunDevice_) {
    // Explicit best-effort route cleanup before closing the device. Windows
    // routes persist after adapter teardown; on macOS/Linux the kernel
    // reclaims interface routes on close, so failures are only logged.
    if (baseIP_ != 0 && subnetMask_ != 0) {
      const std::string subnetMaskStr = ipToString(subnetMask_);
      const std::string networkStr = ipToString(baseIP_ & subnetMask_);
      if (!tunDevice_->remove_route("224.0.0.0", "240.0.0.0")) {
        std::cerr << "[SteamVPN] Failed to remove the multicast route: "
                  << tunDevice_->get_last_error() << std::endl;
      }
      if (!tunDevice_->remove_route(networkStr, subnetMaskStr)) {
        std::cerr << "[SteamVPN] Failed to remove the subnet route: "
                  << tunDevice_->get_last_error() << std::endl;
      }
    }
    tunDevice_->close(); // wake blocking reads
  }
  if (tunReadThread_.joinable()) {
    tunReadThread_.join();
  }
  tunDevice_.reset();
  {
    std::lock_guard<std::mutex> lock(routingMutex_);
    routingTable_.clear();
  }
  ipNegotiator_.reset();
  heartbeatManager_.reset();
  localIP_.store(0, std::memory_order_relaxed);
  std::cout << "Steam VPN bridge stopped" << std::endl;
}

std::string SteamVpnBridge::getLocalIP() const {
  const uint32_t localIP = localIP_.load(std::memory_order_relaxed);
  if (localIP == 0) {
    return {};
  }
  return ipToString(localIP);
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

std::optional<std::string> SteamVpnBridge::takeFailure() {
  std::lock_guard lock(failureMutex_);
  if (lastFailure_.empty()) {
    return std::nullopt;
  }
  std::string failure = std::move(lastFailure_);
  lastFailure_.clear();
  return failure;
}

void SteamVpnBridge::tunReadLoop(std::stop_token stopToken) {
  std::cout << "TUN read thread started" << std::endl;
  uint8_t buffer[2048];
  auto lastTimeoutCheck = std::chrono::steady_clock::now();

  while (running_ && !stopToken.stop_requested()) {
    const int bytesRead = tunDevice_ ? tunDevice_->read(buffer, sizeof(buffer)) : -1;
    if (bytesRead > 0 && steamManager_) {
      const uint32_t destIP = extractDestIP(buffer, bytesRead);
      const uint32_t srcIP = extractSourceIP(buffer, bytesRead);
      const VpnPacketWrapper wrapper{
          .senderNodeId = ipNegotiator_.getLocalNodeID(),
          .sourceIP = htonl(srcIP),
      };
      std::vector<std::byte> payload(sizeof(wrapper) + static_cast<std::size_t>(bytesRead));
      std::memcpy(payload.data(), &wrapper, sizeof(wrapper));
      std::memcpy(payload.data() + sizeof(wrapper), buffer, static_cast<std::size_t>(bytesRead));
      const auto encoded = connecttool::wire::encodeEnvelope(VpnMessageType::IP_PACKET, payload);
      if (!encoded) {
        continue;
      }

      if (destIP == localIP_.load(std::memory_order_relaxed)) {
        // Loopback traffic destined to our own TUN IP back into the stack.
        tunDevice_->write(buffer, static_cast<size_t>(bytesRead));
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.packetsReceived++;
        stats_.bytesReceived += static_cast<uint64_t>(bytesRead);
        std::cout << "[SteamVPN] Local loopback " << ipToString(srcIP) << " -> "
                  << ipToString(destIP) << " (" << bytesRead << " bytes)" << std::endl;
      } else if (isBroadcastAddress(destIP)) {
        steamManager_->broadcastMessage(
            encoded->data(), static_cast<std::uint32_t>(encoded->size()),
            k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_NoDelay);
        const auto peers = steamManager_->getPeers();
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.packetsSent += peers.size();
        stats_.bytesSent += static_cast<uint64_t>(bytesRead) * peers.size();
        std::cout << "[SteamVPN] Broadcast " << ipToString(srcIP) << " -> " << ipToString(destIP)
                  << " to " << peers.size() << " peers (" << bytesRead << " bytes)" << std::endl;
      } else {
        connecttool::domain::PeerId targetPeerId;
        bool found = false;
        {
          std::lock_guard<std::mutex> lock(routingMutex_);
          auto it = routingTable_.find(destIP);
          if (it != routingTable_.end() && !it->second.isLocal) {
            targetPeerId = it->second.peerId;
            found = true;
          } else if (it != routingTable_.end() && it->second.isLocal) {
            // Target is ourselves; loop back.
            tunDevice_->write(buffer, static_cast<size_t>(bytesRead));
            std::lock_guard<std::mutex> lock2(statsMutex_);
            stats_.packetsReceived++;
            stats_.bytesReceived += static_cast<uint64_t>(bytesRead);
            std::cout << "[SteamVPN] Route loopback " << ipToString(srcIP) << " -> "
                      << ipToString(destIP) << " (" << bytesRead << " bytes)" << std::endl;
          }
        }
        if (found) {
          steamManager_->sendMessageToUser(
              connecttool::steam::toSteamId(targetPeerId), encoded->data(),
              static_cast<std::uint32_t>(encoded->size()),
              k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_NoDelay);
          std::lock_guard<std::mutex> lock(statsMutex_);
          stats_.packetsSent++;
          stats_.bytesSent += static_cast<uint64_t>(bytesRead);
          // std::cout << "[SteamVPN] Sent " << ipToString(srcIP) << " -> "
          //           << ipToString(destIP) << " (" << bytesRead
          //           << " bytes) to " << targetSteamID.ConvertToUint64()
          //           << std::endl;
        }
      }
    }
    if (bytesRead <= 0) {
      // No packet ready; yield briefly to avoid spinning a full core.
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTimeoutCheck).count() >=
        50) {
      lastTimeoutCheck = now;
      ipNegotiator_.checkTimeout();
    }
  }
  std::cout << "TUN read thread stopped" << std::endl;
}

void SteamVpnBridge::handleVpnMessage(const uint8_t *data, size_t length, CSteamID senderSteamID) {
  if (!data) {
    return;
  }
  const auto decoded = connecttool::wire::decodeEnvelope(std::as_bytes(std::span{data, length}));
  if (!decoded) {
    return;
  }
  const auto payloadBytes = decoded->payload;
  const auto payloadLength = payloadBytes.size();
  const auto *payload = reinterpret_cast<const std::uint8_t *>(payloadBytes.data());
  const std::string peerName =
      SteamFriends() ? SteamFriends()->GetFriendPersonaName(senderSteamID) : "";
  const auto senderPeerId = connecttool::steam::toPeerId(senderSteamID);

  if (decoded->type == VpnMessageType::IP_PACKET) {
    if (tunDevice_ && payloadLength > sizeof(VpnPacketWrapper)) {
      const auto wrapperResult = connecttool::wire::decodePayload<VpnPacketWrapper>(
          payloadBytes.first(sizeof(VpnPacketWrapper)));
      if (!wrapperResult) {
        return;
      }
      const auto &wrapper = *wrapperResult;
      const uint8_t *ipPacket = payload + sizeof(VpnPacketWrapper);
      const size_t ipPacketLen = payloadLength - sizeof(VpnPacketWrapper);
      const uint32_t destIP = extractDestIP(ipPacket, ipPacketLen);
      const uint32_t senderIP = ntohl(wrapper.sourceIP);

      const uint32_t conflictIP = senderIP != 0 ? senderIP : destIP;
      if (const auto conflict =
              heartbeatManager_.detectConflict(conflictIP, wrapper.senderNodeId)) {
        const ForcedReleasePayload release{
            .ipAddress = htonl(conflictIP),
            .winnerNodeId = conflict->winnerNodeId,
        };
        sendVpnMessage({VpnMessageType::FORCED_RELEASE,
                        std::span<const std::uint8_t>{
                            reinterpret_cast<const std::uint8_t *>(&release), sizeof(release)},
                        true},
                       conflict->loserPeerId);
      }

      if (destIP == localIP_.load(std::memory_order_relaxed) || isBroadcastAddress(destIP)) {
        tunDevice_->write(ipPacket, ipPacketLen);
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.packetsReceived++;
        stats_.bytesReceived += ipPacketLen;
      } else {
        connecttool::domain::PeerId targetPeerId;
        bool found = false;
        {
          std::lock_guard<std::mutex> lock(routingMutex_);
          auto it = routingTable_.find(destIP);
          if (it != routingTable_.end() && !it->second.isLocal) {
            targetPeerId = it->second.peerId;
            found = true;
          }
        }
        if (found && targetPeerId != senderPeerId) {
          sendVpnMessage({VpnMessageType::IP_PACKET, std::span{payload, payloadLength}, false},
                         targetPeerId);
        }
      }
    }
    return;
  }

  switch (decoded->type) {
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
        const auto peerId = connecttool::steam::toPeerId(csteamID);
        NodeID nodeId = NodeIdentity::generate(peerId);
        updateRoute({nodeId, peerId, ipAddress,
                     SteamFriends() ? SteamFriends()->GetFriendPersonaName(csteamID) : ""});
      }
    }
    break;
  }
  case VpnMessageType::PROBE_REQUEST: {
    if (const auto request =
            connecttool::wire::decodePayloadPrefix<ProbeRequestPayload>(payloadBytes)) {
      ipNegotiator_.handleProbeRequest(*request, senderPeerId);
    }
    break;
  }
  case VpnMessageType::PROBE_RESPONSE: {
    if (const auto response =
            connecttool::wire::decodePayloadPrefix<ProbeResponsePayload>(payloadBytes)) {
      ipNegotiator_.handleProbeResponse(*response, senderPeerId);
    }
    break;
  }
  case VpnMessageType::ADDRESS_ANNOUNCE: {
    if (const auto decodedAnnounce =
            connecttool::wire::decodePayloadPrefix<AddressAnnouncePayload>(payloadBytes)) {
      const auto &announce = *decodedAnnounce;
      const uint32_t announcedIP = ntohl(announce.ipAddress);
      bool isNewRoute = false;
      {
        std::lock_guard<std::mutex> lock(routingMutex_);
        isNewRoute = routingTable_.find(announcedIP) == routingTable_.end();
      }
      ipNegotiator_.handleAddressAnnounce(announce, senderPeerId, peerName);
      updateRoute({announce.nodeId, senderPeerId, announcedIP, peerName});
      if (isNewRoute) {
        broadcastRouteUpdate();
      }
    }
    break;
  }
  case VpnMessageType::FORCED_RELEASE: {
    if (const auto release =
            connecttool::wire::decodePayloadPrefix<ForcedReleasePayload>(payloadBytes)) {
      ipNegotiator_.handleForcedRelease(*release, senderPeerId);
    }
    break;
  }
  case VpnMessageType::HEARTBEAT: {
    if (const auto heartbeat =
            connecttool::wire::decodePayloadPrefix<HeartbeatPayload>(payloadBytes)) {
      heartbeatManager_.handleHeartbeat(*heartbeat, senderPeerId, peerName);
    }
    break;
  }
  default:
    break;
  }
}

void SteamVpnBridge::onUserJoined(CSteamID steamID) {
  if (ipNegotiator_.getState() == NegotiationState::STABLE) {
    std::cout << "[SteamVPN] New peer joined, sending address/route: " << steamID.ConvertToUint64()
              << std::endl;
    ipNegotiator_.sendAddressAnnounceTo(connecttool::steam::toPeerId(steamID));
    sendRouteUpdateTo(steamID);
  }
}

void SteamVpnBridge::onUserLeft(CSteamID steamID) {
  std::lock_guard<std::mutex> lock(routingMutex_);
  for (auto it = routingTable_.begin(); it != routingTable_.end();) {
    if (it->second.peerId == connecttool::steam::toPeerId(steamID)) {
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
    localIP_.store(0, std::memory_order_relaxed);
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

void SteamVpnBridge::onNegotiationSuccess(uint32_t ipAddress, const NodeID &nodeId) {
  if (!tunDevice_) {
    recordFailure("TUN device disappeared during IP negotiation");
    return;
  }

  const std::string localIPStr = ipToString(ipAddress);
  const std::string subnetMaskStr = ipToString(subnetMask_);
  if (!tunDevice_->set_ip(localIPStr, subnetMaskStr)) {
    recordFailure("Failed to configure the TUN address: " + tunDevice_->get_last_error());
    return;
  }
  if (!tunDevice_->set_up(true)) {
    recordFailure("Failed to enable the TUN interface: " + tunDevice_->get_last_error());
    return;
  }

  // Install a connected route for the virtual subnet so the OS sends traffic
  // into the TUN device.
  const uint32_t networkIp = baseIP_ & subnetMask_;
  const std::string networkStr = ipToString(networkIp);
  if (!tunDevice_->add_route(networkStr, subnetMaskStr)) {
    recordFailure("Failed to add the TUN route: " + tunDevice_->get_last_error());
    return;
  }

  // Install the multicast route so LAN-discovery traffic (e.g. Minecraft
  // 224.0.2.60:4445) is routed into the TUN device. Best-effort: failure only
  // degrades LAN discovery and must not abort the VPN.
  if (!tunDevice_->add_route("224.0.0.0", "240.0.0.0")) {
    std::cerr << "[SteamVPN] Failed to add the multicast route (LAN discovery "
                 "degraded): "
              << tunDevice_->get_last_error() << std::endl;
  }

  localIP_.store(ipAddress, std::memory_order_relaxed);
  const auto myPeerId = connecttool::steam::toPeerId(SteamUser()->GetSteamID());
  updateRoute(
      {nodeId, myPeerId, ipAddress, SteamFriends() ? SteamFriends()->GetPersonaName() : ""});
  heartbeatManager_.initialize(nodeId, ipAddress);
  heartbeatManager_.registerNode(nodeId, myPeerId, ipAddress,
                                 SteamFriends() ? SteamFriends()->GetPersonaName() : "");
  heartbeatManager_.start();
  broadcastRouteUpdate();
}

void SteamVpnBridge::recordFailure(std::string message) {
  running_ = false;
  std::lock_guard lock(failureMutex_);
  if (lastFailure_.empty()) {
    lastFailure_ = std::move(message);
  }
}

void SteamVpnBridge::onNodeExpired(const NodeID &, uint32_t ipAddress) {
  removeRoute(ipAddress);
  ipNegotiator_.markIPUnused(ipAddress);
}

void SteamVpnBridge::updateRoute(PeerRoute route) {
  RouteEntry entry;
  entry.peerId = route.peerId;
  entry.ipAddress = route.ipAddress;
  entry.name = std::move(route.name);
  entry.isLocal =
      (SteamUser() && route.peerId == connecttool::steam::toPeerId(SteamUser()->GetSteamID()));
  entry.nodeId = route.nodeId;

  {
    std::lock_guard<std::mutex> lock(routingMutex_);
    for (auto it = routingTable_.begin(); it != routingTable_.end();) {
      if (it->second.peerId == route.peerId && it->first != route.ipAddress) {
        it = routingTable_.erase(it);
      } else {
        ++it;
      }
    }
    routingTable_[route.ipAddress] = entry;
  }
  ipNegotiator_.markIPUsed(route.ipAddress);
  std::cout << "Route updated: " << ipToString(route.ipAddress) << " -> " << entry.name
            << std::endl;
}

void SteamVpnBridge::removeRoute(uint32_t ipAddress) {
  std::lock_guard<std::mutex> lock(routingMutex_);
  routingTable_.erase(ipAddress);
}

void SteamVpnBridge::broadcastRouteUpdate() {
  std::vector<uint8_t> routeData;

  {
    std::lock_guard<std::mutex> lock(routingMutex_);
    for (const auto &entry : routingTable_) {
      const uint64_t steamID = entry.second.peerId.value();
      const uint32_t ipAddress = htonl(entry.second.ipAddress);
      const size_t offset = routeData.size();
      routeData.resize(offset + 12);
      std::memcpy(routeData.data() + offset, &steamID, 8);
      std::memcpy(routeData.data() + offset + 8, &ipAddress, 4);
    }
  }

  const auto message = connecttool::wire::encodeEnvelope(VpnMessageType::ROUTE_UPDATE,
                                                         std::as_bytes(std::span{routeData}));
  if (!message) {
    return;
  }
  std::cout << "[SteamVPN] Broadcasting route update with " << (routeData.size() / 12) << " entries"
            << std::endl;
  steamManager_->broadcastMessage(message->data(), static_cast<uint32_t>(message->size()),
                                  k_nSteamNetworkingSend_Reliable);
}

void SteamVpnBridge::sendRouteUpdateTo(CSteamID targetSteamID) {
  std::vector<uint8_t> routeData;
  {
    std::lock_guard<std::mutex> lock(routingMutex_);
    for (const auto &entry : routingTable_) {
      const uint64_t steamID = entry.second.peerId.value();
      const uint32_t ipAddress = htonl(entry.second.ipAddress);
      const size_t offset = routeData.size();
      routeData.resize(offset + 12);
      std::memcpy(routeData.data() + offset, &steamID, 8);
      std::memcpy(routeData.data() + offset + 8, &ipAddress, 4);
    }
  }

  const auto message = connecttool::wire::encodeEnvelope(VpnMessageType::ROUTE_UPDATE,
                                                         std::as_bytes(std::span{routeData}));
  if (!message) {
    return;
  }
  std::cout << "[SteamVPN] Sending route update to " << targetSteamID.ConvertToUint64() << " with "
            << (routeData.size() / 12) << " entries" << std::endl;
  steamManager_->sendMessageToUser(targetSteamID, message->data(),
                                   static_cast<uint32_t>(message->size()),
                                   k_nSteamNetworkingSend_Reliable);
}

void SteamVpnBridge::sendVpnMessage(OutboundMessage outbound,
                                    connecttool::domain::PeerId targetPeerId) {
  if (!steamManager_) {
    return;
  }
  const auto message =
      connecttool::wire::encodeEnvelope(outbound.type, std::as_bytes(outbound.payload));
  if (!message) {
    return;
  }
  const int flags =
      outbound.reliable
          ? k_nSteamNetworkingSend_Reliable
          : (k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_NoDelay);
  steamManager_->sendMessageToUser(connecttool::steam::toSteamId(targetPeerId), message->data(),
                                   static_cast<uint32_t>(message->size()), flags);
}

void SteamVpnBridge::broadcastVpnMessage(OutboundMessage outbound) {
  if (!steamManager_) {
    return;
  }
  const auto message =
      connecttool::wire::encodeEnvelope(outbound.type, std::as_bytes(outbound.payload));
  if (!message) {
    return;
  }
  const int flags =
      outbound.reliable
          ? k_nSteamNetworkingSend_Reliable
          : (k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_NoDelay);
  steamManager_->broadcastMessage(message->data(), static_cast<uint32_t>(message->size()), flags);
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
