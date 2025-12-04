#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <steam_api.h>
#include <string>

// Distributed IP negotiation defaults (matches ConnectTool baseline)
constexpr const char *APP_SECRET_SALT = "ConnectTool_VPN_Salt_v1";

// Protocol timing (milliseconds)
constexpr int64_t PROBE_TIMEOUT_MS = 500;
constexpr int64_t HEARTBEAT_INTERVAL_MS = 60000;
constexpr int64_t LEASE_TIME_MS = 120000;
constexpr int64_t LEASE_EXPIRY_MS = 360000;
constexpr int64_t HEARTBEAT_EXPIRY_MS = 180000;

// Node ID
constexpr size_t NODE_ID_SIZE = 32;
using NodeID = std::array<uint8_t, NODE_ID_SIZE>;

enum class VpnMessageType : uint8_t {
  IP_PACKET = 1,
  ROUTE_UPDATE = 3,
  PROBE_REQUEST = 10,
  PROBE_RESPONSE = 11,
  ADDRESS_ANNOUNCE = 12,
  FORCED_RELEASE = 13,
  HEARTBEAT = 14,
  HEARTBEAT_ACK = 15,
  SESSION_HELLO = 20
};

#pragma pack(push, 1)
struct VpnMessageHeader {
  VpnMessageType type;
  uint16_t length;
};

struct VpnPacketWrapper {
  NodeID senderNodeId;
  uint32_t sourceIP; // network byte order
};

struct ProbeRequestPayload {
  uint32_t ipAddress;
  NodeID nodeId;
};

struct ProbeResponsePayload {
  uint32_t ipAddress;
  NodeID nodeId;
  int64_t lastHeartbeatMs;
};

struct AddressAnnouncePayload {
  uint32_t ipAddress;
  NodeID nodeId;
};

struct ForcedReleasePayload {
  uint32_t ipAddress;
  NodeID winnerNodeId;
};

struct HeartbeatPayload {
  uint32_t ipAddress;
  NodeID nodeId;
  int64_t timestampMs;
};
#pragma pack(pop)

struct NodeInfo {
  NodeID nodeId;
  CSteamID steamId;
  uint32_t ipAddress;
  std::chrono::steady_clock::time_point lastHeartbeat;
  std::string name;
  bool isLocal;

  bool isActive() const {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                              lastHeartbeat)
            .count();
    return elapsed < HEARTBEAT_EXPIRY_MS;
  }

  bool isLeaseExpired() const {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                              lastHeartbeat)
            .count();
    return elapsed >= LEASE_EXPIRY_MS;
  }
};

struct RouteEntry {
  CSteamID steamID;
  uint32_t ipAddress;
  std::string name;
  bool isLocal;
  NodeID nodeId;
};
