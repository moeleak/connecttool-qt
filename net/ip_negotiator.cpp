#include "ip_negotiator.h"
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

IpNegotiator::IpNegotiator()
    : localIP_(0), baseIP_(0), subnetMask_(0), state_(NegotiationState::IDLE),
      candidateIP_(0), probeOffset_(0) {
  localNodeId_.fill(0);
}

void IpNegotiator::initialize(CSteamID localSteamID, uint32_t baseIP,
                              uint32_t subnetMask) {
  localSteamID_ = localSteamID;
  baseIP_ = baseIP;
  subnetMask_ = subnetMask;
  localNodeId_ = NodeIdentity::generate(localSteamID);
  std::cout << "Generated Node ID: " << NodeIdentity::toString(localNodeId_)
            << std::endl;
}

void IpNegotiator::reset() {
  {
    std::lock_guard<std::mutex> lock(usedIPsMutex_);
    usedIPs_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(conflictsMutex_);
    collectedConflicts_.clear();
  }
  state_ = NegotiationState::IDLE;
  candidateIP_ = 0;
  probeOffset_ = 0;
  localIP_ = 0;
}

void IpNegotiator::setSendCallback(VpnSendMessageCallback sendCb,
                                   VpnBroadcastMessageCallback broadcastCb) {
  sendCallback_ = std::move(sendCb);
  broadcastCallback_ = std::move(broadcastCb);
}

void IpNegotiator::setSuccessCallback(NegotiationSuccessCallback callback) {
  successCallback_ = std::move(callback);
}

void IpNegotiator::startNegotiation() {
  {
    std::lock_guard<std::mutex> lock(conflictsMutex_);
    collectedConflicts_.clear();
  }

  candidateIP_ = generateCandidateIP(probeOffset_);
  candidateIP_ = findNextAvailableIP(candidateIP_);
  state_ = NegotiationState::PROBING;

  std::cout << "Probing IP: " << ((candidateIP_ >> 24) & 0xFF) << "."
            << ((candidateIP_ >> 16) & 0xFF) << "."
            << ((candidateIP_ >> 8) & 0xFF) << "." << (candidateIP_ & 0xFF)
            << " (offset=" << probeOffset_ << ")" << std::endl;

  sendProbeRequest();
  probeStartTime_ = std::chrono::steady_clock::now();
}

uint32_t IpNegotiator::generateCandidateIP(uint32_t offset) {
  uint32_t hash = (static_cast<uint32_t>(localNodeId_[NODE_ID_SIZE - 1]) |
                   (static_cast<uint32_t>(localNodeId_[NODE_ID_SIZE - 2]) << 8) |
                   (static_cast<uint32_t>(localNodeId_[NODE_ID_SIZE - 3])
                    << 16));

  hash = (hash + offset) & 0x00FFFFFF;

  const uint32_t hostMask = ~subnetMask_;
  uint32_t maxHosts = hostMask - 1;
  if (maxHosts == 0) {
    maxHosts = 1;
  }

  const uint32_t hostPart = (hash % maxHosts) + 1;
  const uint32_t ip = (baseIP_ & subnetMask_) | hostPart;
  return ip;
}

uint32_t IpNegotiator::findNextAvailableIP(uint32_t startIP) {
  std::lock_guard<std::mutex> lock(usedIPsMutex_);

  const uint32_t hostMask = ~subnetMask_;
  uint32_t maxHosts = hostMask - 1;
  if (maxHosts == 0) {
    maxHosts = 1;
  }

  uint32_t hostPart = startIP & hostMask;
  if (hostPart == 0 || hostPart >= hostMask) {
    hostPart = 1;
  }

  uint32_t potentialIP = (baseIP_ & subnetMask_) | hostPart;
  uint32_t attempts = 0;
  while (usedIPs_.count(potentialIP) && attempts < maxHosts) {
    hostPart++;
    if (hostPart >= hostMask) {
      hostPart = 1;
    }
    potentialIP = (baseIP_ & subnetMask_) | hostPart;
    attempts++;
  }
  return potentialIP;
}

void IpNegotiator::sendProbeRequest() {
  if (!broadcastCallback_) {
    return;
  }
  ProbeRequestPayload payload;
  payload.ipAddress = htonl(candidateIP_);
  payload.nodeId = localNodeId_;

  broadcastCallback_(VpnMessageType::PROBE_REQUEST,
                     reinterpret_cast<const uint8_t *>(&payload),
                     sizeof(payload), true);
}

void IpNegotiator::checkTimeout() {
  if (state_ != NegotiationState::PROBING) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - probeStartTime_)
                           .count();
  if (elapsed < PROBE_TIMEOUT_MS) {
    return;
  }

  std::vector<ConflictInfo> conflicts;
  {
    std::lock_guard<std::mutex> lock(conflictsMutex_);
    conflicts = std::move(collectedConflicts_);
    collectedConflicts_.clear();
  }

  bool canClaim = true;
  std::vector<CSteamID> nodesToForceRelease;

  for (const auto &conflict : conflicts) {
    const auto currentMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now.time_since_epoch())
                               .count();
    const int64_t heartbeatAge = currentMs - conflict.lastHeartbeatMs;
    if (heartbeatAge >= HEARTBEAT_EXPIRY_MS) {
      std::cout << "Ignoring stale node (heartbeat age: " << heartbeatAge
                << "ms)" << std::endl;
      continue;
    }

    if (NodeIdentity::hasPriority(localNodeId_, conflict.nodeId)) {
      nodesToForceRelease.push_back(conflict.senderSteamID);
    } else {
      canClaim = false;
      break;
    }
  }

  if (canClaim) {
    for (auto steamID : nodesToForceRelease) {
      sendForcedRelease(candidateIP_, steamID);
    }

    std::cout << "IP negotiation success. Local IP: "
              << ((candidateIP_ >> 24) & 0xFF) << "."
              << ((candidateIP_ >> 16) & 0xFF) << "."
              << ((candidateIP_ >> 8) & 0xFF) << "." << (candidateIP_ & 0xFF)
              << std::endl;

    state_ = NegotiationState::STABLE;
    localIP_ = candidateIP_;
    sendAddressAnnounce();

    if (successCallback_) {
      successCallback_(localIP_, localNodeId_);
    }
  } else {
    std::cout << "Lost IP arbitration, reselecting with new offset..."
              << std::endl;
    probeOffset_++;
    startNegotiation();
  }
}

void IpNegotiator::handleProbeRequest(const ProbeRequestPayload &request,
                                      CSteamID senderSteamID) {
  const uint32_t requestedIP = ntohl(request.ipAddress);
  bool shouldRespond = false;

  if (state_ == NegotiationState::STABLE && requestedIP == localIP_) {
    shouldRespond = true;
  } else if (state_ == NegotiationState::PROBING &&
             requestedIP == candidateIP_) {
    if (NodeIdentity::hasPriority(localNodeId_, request.nodeId)) {
      shouldRespond = true;
    } else {
      std::cout << "Lost probe contention, reselecting..." << std::endl;
      probeOffset_++;
      startNegotiation();
      return;
    }
  }

  if (shouldRespond && sendCallback_) {
    ProbeResponsePayload response;
    response.ipAddress = htonl(requestedIP);
    response.nodeId = localNodeId_;
    const auto now = std::chrono::steady_clock::now();
    response.lastHeartbeatMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   now.time_since_epoch())
                                   .count();

    sendCallback_(VpnMessageType::PROBE_RESPONSE,
                  reinterpret_cast<const uint8_t *>(&response),
                  sizeof(response), senderSteamID, true);
    std::cout << "Sent conflict response for IP" << std::endl;
  }
}

void IpNegotiator::handleProbeResponse(const ProbeResponsePayload &response,
                                       CSteamID senderSteamID) {
  if (state_ != NegotiationState::PROBING) {
    return;
  }
  const uint32_t conflictIP = ntohl(response.ipAddress);
  if (conflictIP != candidateIP_) {
    return;
  }
  std::lock_guard<std::mutex> lock(conflictsMutex_);
  ConflictInfo info;
  info.nodeId = response.nodeId;
  info.lastHeartbeatMs = response.lastHeartbeatMs;
  info.senderSteamID = senderSteamID;
  collectedConflicts_.push_back(info);
  std::cout << "Received conflict response from node "
            << NodeIdentity::toString(response.nodeId) << std::endl;
}

void IpNegotiator::handleAddressAnnounce(
    const AddressAnnouncePayload &announce, CSteamID peerSteamID,
    const std::string &peerName) {
  const uint32_t announcedIP = ntohl(announce.ipAddress);
  std::cout << "Received address announce: " << ((announcedIP >> 24) & 0xFF)
            << "." << ((announcedIP >> 16) & 0xFF) << "."
            << ((announcedIP >> 8) & 0xFF) << "." << (announcedIP & 0xFF)
            << " from node " << NodeIdentity::toString(announce.nodeId)
            << std::endl;

  if (announcedIP == localIP_ && state_ == NegotiationState::STABLE) {
    if (!NodeIdentity::hasPriority(localNodeId_, announce.nodeId)) {
      std::cout << "Address conflict detected, reselecting..." << std::endl;
      probeOffset_++;
      startNegotiation();
      return;
    }
    sendForcedRelease(announcedIP, peerSteamID);
    return;
  }

  markIPUsed(announcedIP);
}

void IpNegotiator::handleForcedRelease(const ForcedReleasePayload &release,
                                       CSteamID senderSteamID) {
  const uint32_t releasedIP = ntohl(release.ipAddress);
  bool shouldRelease = false;

  if (releasedIP == localIP_ && state_ == NegotiationState::STABLE) {
    if (!NodeIdentity::hasPriority(localNodeId_, release.winnerNodeId)) {
      shouldRelease = true;
    }
  } else if (releasedIP == candidateIP_ && state_ == NegotiationState::PROBING) {
    if (!NodeIdentity::hasPriority(localNodeId_, release.winnerNodeId)) {
      shouldRelease = true;
    }
  }

  if (shouldRelease) {
    std::cout << "Received forced release, reselecting..." << std::endl;
    probeOffset_++;
    state_ = NegotiationState::IDLE;
    startNegotiation();
  }
}

void IpNegotiator::sendAddressAnnounce() {
  if (!broadcastCallback_) {
    return;
  }
  AddressAnnouncePayload payload;
  payload.ipAddress = htonl(localIP_);
  payload.nodeId = localNodeId_;
  broadcastCallback_(VpnMessageType::ADDRESS_ANNOUNCE,
                     reinterpret_cast<const uint8_t *>(&payload),
                     sizeof(payload), true);
}

void IpNegotiator::sendAddressAnnounceTo(CSteamID targetSteamID) {
  if (!sendCallback_ || state_ != NegotiationState::STABLE || localIP_ == 0) {
    return;
  }
  AddressAnnouncePayload payload;
  payload.ipAddress = htonl(localIP_);
  payload.nodeId = localNodeId_;
  sendCallback_(VpnMessageType::ADDRESS_ANNOUNCE,
                reinterpret_cast<const uint8_t *>(&payload), sizeof(payload),
                targetSteamID, true);
}

void IpNegotiator::sendForcedRelease(uint32_t ipAddress,
                                     CSteamID targetSteamID) {
  if (!sendCallback_) {
    return;
  }
  ForcedReleasePayload payload;
  payload.ipAddress = htonl(ipAddress);
  payload.winnerNodeId = localNodeId_;
  sendCallback_(VpnMessageType::FORCED_RELEASE,
                reinterpret_cast<const uint8_t *>(&payload), sizeof(payload),
                targetSteamID, true);
  std::cout << "Sent forced release" << std::endl;
}

void IpNegotiator::markIPUsed(uint32_t ip) {
  std::lock_guard<std::mutex> lock(usedIPsMutex_);
  usedIPs_.insert(ip);
}

void IpNegotiator::markIPUnused(uint32_t ip) {
  std::lock_guard<std::mutex> lock(usedIPsMutex_);
  usedIPs_.erase(ip);
}
