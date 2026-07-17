#include "heartbeat_manager.h"
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

HeartbeatManager::HeartbeatManager() : localIP_(0), running_(false) { localNodeId_.fill(0); }

HeartbeatManager::~HeartbeatManager() { stop(); }

void HeartbeatManager::initialize(const NodeID &localNodeId, uint32_t localIP) {
  localNodeId_ = localNodeId;
  localIP_.store(localIP, std::memory_order_relaxed);
  lastHeartbeatSent_ = std::chrono::steady_clock::now();
}

void HeartbeatManager::setSendCallback(HeartbeatSendCallback callback) {
  sendCallback_ = std::move(callback);
}

void HeartbeatManager::setNodeExpiredCallback(NodeExpiredCallback callback) {
  expiredCallback_ = std::move(callback);
}

void HeartbeatManager::start() {
  if (running_) {
    return;
  }
  running_ = true;
  heartbeatThread_ = std::jthread([this](std::stop_token stopToken) { heartbeatLoop(stopToken); });
  std::cout << "Heartbeat manager started" << std::endl;
}

void HeartbeatManager::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  if (heartbeatThread_.joinable()) {
    heartbeatThread_.request_stop();
    wakeCondition_.notify_all();
    heartbeatThread_.join();
  }
  std::cout << "Heartbeat manager stopped" << std::endl;
}

void HeartbeatManager::reset() {
  stop();
  {
    std::lock_guard<std::mutex> lock(nodeTableMutex_);
    nodeTable_.clear();
    ipToNodeId_.clear();
  }
  localIP_.store(0, std::memory_order_relaxed);
  localNodeId_.fill(0);
  lastHeartbeatSent_ = std::chrono::steady_clock::now();
}

void HeartbeatManager::updateLocalIP(uint32_t ip) { localIP_.store(ip, std::memory_order_relaxed); }

void HeartbeatManager::heartbeatLoop(std::stop_token stopToken) {
  std::unique_lock wakeLock(wakeMutex_);
  while (!stopToken.stop_requested()) {
    wakeCondition_.wait_for(wakeLock, stopToken, std::chrono::seconds(1), [] { return false; });
    if (stopToken.stop_requested()) {
      break;
    }
    wakeLock.unlock();
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeatSent_).count();
    if (elapsed >= HEARTBEAT_INTERVAL_MS && localIP_.load(std::memory_order_relaxed) != 0) {
      sendHeartbeat();
      lastHeartbeatSent_ = now;
    }
    checkExpiredLeases();
    wakeLock.lock();
  }
}

void HeartbeatManager::sendHeartbeat() {
  const uint32_t localIP = localIP_.load(std::memory_order_relaxed);
  if (!sendCallback_ || localIP == 0) {
    return;
  }
  HeartbeatPayload payload;
  payload.ipAddress = htonl(localIP);
  payload.nodeId = localNodeId_;
  const auto now = std::chrono::steady_clock::now();
  payload.timestampMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  sendCallback_(VpnMessageType::HEARTBEAT, reinterpret_cast<const uint8_t *>(&payload),
                sizeof(payload), true);
}

void HeartbeatManager::checkExpiredLeases() {
  std::vector<std::pair<NodeID, uint32_t>> expiredNodes;
  {
    std::lock_guard<std::mutex> lock(nodeTableMutex_);
    for (auto it = nodeTable_.begin(); it != nodeTable_.end();) {
      if (!it->second.isLocal && it->second.isLeaseExpired()) {
        std::cout << "Node " << NodeIdentity::toString(it->first) << " lease expired" << std::endl;
        expiredNodes.emplace_back(it->first, it->second.ipAddress);
        ipToNodeId_.erase(it->second.ipAddress);
        it = nodeTable_.erase(it);
      } else {
        ++it;
      }
    }
  }

  if (expiredCallback_) {
    for (const auto &[nodeId, ip] : expiredNodes) {
      expiredCallback_(nodeId, ip);
    }
  }
}

void HeartbeatManager::handleHeartbeat(const HeartbeatPayload &heartbeat, CSteamID peerSteamID,
                                       const std::string &peerName) {
  const uint32_t heartbeatIP = ntohl(heartbeat.ipAddress);
  std::lock_guard<std::mutex> lock(nodeTableMutex_);
  auto it = nodeTable_.find(heartbeat.nodeId);
  if (it != nodeTable_.end()) {
    it->second.lastHeartbeat = std::chrono::steady_clock::now();
  } else {
    NodeInfo nodeInfo;
    nodeInfo.nodeId = heartbeat.nodeId;
    nodeInfo.steamId = peerSteamID;
    nodeInfo.ipAddress = heartbeatIP;
    nodeInfo.lastHeartbeat = std::chrono::steady_clock::now();
    nodeInfo.name = peerName;
    nodeInfo.isLocal = false;
    nodeTable_[heartbeat.nodeId] = nodeInfo;
    ipToNodeId_[heartbeatIP] = heartbeat.nodeId;
  }
}

void HeartbeatManager::registerNode(const NodeID &nodeId, CSteamID steamId, uint32_t ipAddress,
                                    const std::string &name) {
  std::lock_guard<std::mutex> lock(nodeTableMutex_);
  NodeInfo nodeInfo;
  nodeInfo.nodeId = nodeId;
  nodeInfo.steamId = steamId;
  nodeInfo.ipAddress = ipAddress;
  nodeInfo.lastHeartbeat = std::chrono::steady_clock::now();
  nodeInfo.name = name;
  nodeInfo.isLocal = (nodeId == localNodeId_);
  nodeTable_[nodeId] = nodeInfo;
  ipToNodeId_[ipAddress] = nodeId;
}

void HeartbeatManager::unregisterNode(const NodeID &nodeId) {
  std::lock_guard<std::mutex> lock(nodeTableMutex_);
  auto it = nodeTable_.find(nodeId);
  if (it != nodeTable_.end()) {
    ipToNodeId_.erase(it->second.ipAddress);
    nodeTable_.erase(it);
  }
}

bool HeartbeatManager::findNodeByIP(uint32_t ip, NodeID &outNodeId) const {
  std::lock_guard<std::mutex> lock(nodeTableMutex_);
  auto it = ipToNodeId_.find(ip);
  if (it != ipToNodeId_.end()) {
    outNodeId = it->second;
    return true;
  }
  return false;
}

std::map<NodeID, NodeInfo> HeartbeatManager::getAllNodes() const {
  std::lock_guard<std::mutex> lock(nodeTableMutex_);
  return nodeTable_;
}

std::optional<HeartbeatManager::ConflictResolution>
HeartbeatManager::detectConflict(uint32_t sourceIP, const NodeID &senderNodeId) {
  std::lock_guard<std::mutex> lock(nodeTableMutex_);
  auto it = ipToNodeId_.find(sourceIP);
  if (it != ipToNodeId_.end() && it->second != senderNodeId) {
    std::cout << "Packet-level conflict detected for IP" << std::endl;
    if (NodeIdentity::hasPriority(it->second, senderNodeId)) {
      auto nodeIt = nodeTable_.find(senderNodeId);
      if (nodeIt != nodeTable_.end()) {
        return ConflictResolution{.loserSteamId = nodeIt->second.steamId,
                                  .winnerNodeId = it->second};
      }
    } else {
      auto nodeIt = nodeTable_.find(it->second);
      if (nodeIt != nodeTable_.end()) {
        const CSteamID loser = nodeIt->second.steamId;
        it->second = senderNodeId;
        return ConflictResolution{.loserSteamId = loser, .winnerNodeId = senderNodeId};
      }
    }
  }
  return std::nullopt;
}
