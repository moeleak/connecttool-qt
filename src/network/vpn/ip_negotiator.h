#pragma once

#include "node_identity.h"
#include "vpn_protocol.h"
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <vector>

enum class NegotiationState { IDLE, PROBING, STABLE };

struct ConflictInfo {
  NodeID nodeId;
  int64_t lastHeartbeatMs;
  connecttool::domain::PeerId senderPeerId;
};

using VpnSendMessageCallback =
    std::function<void(VpnMessageType type, const uint8_t *payload,
                       size_t length, connecttool::domain::PeerId targetPeerId, bool reliable)>;
using VpnBroadcastMessageCallback =
    std::function<void(VpnMessageType type, const uint8_t *payload,
                       size_t length, bool reliable)>;
using NegotiationSuccessCallback =
    std::function<void(uint32_t ipAddress, const NodeID &nodeId)>;

class IpNegotiator {
public:
  IpNegotiator();

  void initialize(connecttool::domain::PeerId localPeerId, uint32_t baseIP,
                  uint32_t subnetMask);
  void setSendCallback(VpnSendMessageCallback sendCb,
                       VpnBroadcastMessageCallback broadcastCb);
  void setSuccessCallback(NegotiationSuccessCallback callback);
  void reset();
  void startNegotiation();
  void checkTimeout();
  void handleProbeRequest(const ProbeRequestPayload &request,
                          connecttool::domain::PeerId senderPeerId);
  void handleProbeResponse(const ProbeResponsePayload &response,
                           connecttool::domain::PeerId senderPeerId);
  void handleAddressAnnounce(const AddressAnnouncePayload &announce,
                             connecttool::domain::PeerId peerId,
                             const std::string &peerName);
  void handleForcedRelease(const ForcedReleasePayload &release,
                           connecttool::domain::PeerId senderPeerId);

  NegotiationState getState() const { return state_; }
  uint32_t getLocalIP() const { return localIP_; }
  const NodeID &getLocalNodeID() const { return localNodeId_; }
  uint32_t getCandidateIP() const { return candidateIP_; }

  void sendAddressAnnounce();
  void sendAddressAnnounceTo(connecttool::domain::PeerId targetPeerId);
  void markIPUsed(uint32_t ip);
  void markIPUnused(uint32_t ip);

private:
  uint32_t generateCandidateIP(uint32_t offset);
  uint32_t findNextAvailableIP(uint32_t startIP);
  void sendProbeRequest();
  void sendForcedRelease(uint32_t ipAddress, connecttool::domain::PeerId targetPeerId);

  NodeID localNodeId_;
  connecttool::domain::PeerId localPeerId_;
  uint32_t localIP_;
  uint32_t baseIP_;
  uint32_t subnetMask_;

  NegotiationState state_;
  uint32_t candidateIP_;
  uint32_t probeOffset_;
  std::chrono::steady_clock::time_point probeStartTime_;

  std::vector<ConflictInfo> collectedConflicts_;
  std::mutex conflictsMutex_;
  std::set<uint32_t> usedIPs_;
  std::mutex usedIPsMutex_;

  VpnSendMessageCallback sendCallback_;
  VpnBroadcastMessageCallback broadcastCallback_;
  NegotiationSuccessCallback successCallback_;
};
