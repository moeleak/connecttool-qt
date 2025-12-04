#pragma once

#include "node_identity.h"
#include "vpn_protocol.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <steam_api.h>
#include <thread>

using HeartbeatSendCallback =
    std::function<void(VpnMessageType type, const uint8_t *payload,
                       size_t length, bool reliable)>;
using NodeExpiredCallback =
    std::function<void(const NodeID &nodeId, uint32_t ipAddress)>;

class HeartbeatManager {
public:
  HeartbeatManager();
  ~HeartbeatManager();

  void initialize(const NodeID &localNodeId, uint32_t localIP);
  void setSendCallback(HeartbeatSendCallback callback);
  void setNodeExpiredCallback(NodeExpiredCallback callback);
  void start();
  void stop();
  void reset();
  void updateLocalIP(uint32_t ip);

  void handleHeartbeat(const HeartbeatPayload &heartbeat,
                       CSteamID peerSteamID, const std::string &peerName);
  void registerNode(const NodeID &nodeId, CSteamID steamId, uint32_t ipAddress,
                    const std::string &name);
  void unregisterNode(const NodeID &nodeId);
  bool findNodeByIP(uint32_t ip, NodeID &outNodeId) const;
  std::map<NodeID, NodeInfo> getAllNodes() const;
  bool detectConflict(uint32_t sourceIP, const NodeID &senderNodeId,
                      CSteamID &outConflictingSteamID);

private:
  void heartbeatLoop();
  void sendHeartbeat();
  void checkExpiredLeases();

  NodeID localNodeId_;
  uint32_t localIP_;
  std::chrono::steady_clock::time_point lastHeartbeatSent_;

  std::map<NodeID, NodeInfo> nodeTable_;
  std::map<uint32_t, NodeID> ipToNodeId_;
  mutable std::mutex nodeTableMutex_;

  std::unique_ptr<std::thread> heartbeatThread_;
  std::atomic<bool> running_;

  HeartbeatSendCallback sendCallback_;
  NodeExpiredCallback expiredCallback_;
};
