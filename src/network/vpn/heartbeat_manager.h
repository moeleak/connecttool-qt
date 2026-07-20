#pragma once

#include "node_identity.h"
#include "vpn_protocol.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

using HeartbeatSendCallback =
    std::function<void(VpnMessageType type, const uint8_t *payload, size_t length, bool reliable)>;
using NodeExpiredCallback = std::function<void(const NodeID &nodeId, uint32_t ipAddress)>;

class HeartbeatManager {
public:
  struct ConflictResolution {
    connecttool::domain::PeerId loserPeerId;
    NodeID winnerNodeId;
  };

  HeartbeatManager();
  ~HeartbeatManager();

  void initialize(const NodeID &localNodeId, uint32_t localIP);
  void setSendCallback(HeartbeatSendCallback callback);
  void setNodeExpiredCallback(NodeExpiredCallback callback);
  void start();
  void stop();
  void reset();
  void updateLocalIP(uint32_t ip);

  void handleHeartbeat(const HeartbeatPayload &heartbeat, connecttool::domain::PeerId peerId,
                       const std::string &peerName);
  void registerNode(const NodeID &nodeId, connecttool::domain::PeerId peerId, uint32_t ipAddress,
                    const std::string &name);
  void unregisterNode(const NodeID &nodeId);
  bool findNodeByIP(uint32_t ip, NodeID &outNodeId) const;
  std::map<NodeID, NodeInfo> getAllNodes() const;
  [[nodiscard]] std::optional<ConflictResolution> detectConflict(uint32_t sourceIP,
                                                                 const NodeID &senderNodeId);

private:
  void heartbeatLoop(std::stop_token stopToken);
  void sendHeartbeat();
  void checkExpiredLeases();

  NodeID localNodeId_;
  std::atomic<uint32_t> localIP_;
  std::chrono::steady_clock::time_point lastHeartbeatSent_;

  std::map<NodeID, NodeInfo> nodeTable_;
  std::map<uint32_t, NodeID> ipToNodeId_;
  mutable std::mutex nodeTableMutex_;

  std::jthread heartbeatThread_;
  std::mutex wakeMutex_;
  std::condition_variable_any wakeCondition_;
  std::atomic<bool> running_;

  HeartbeatSendCallback sendCallback_;
  NodeExpiredCallback expiredCallback_;
};
