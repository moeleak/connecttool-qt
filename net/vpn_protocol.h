#pragma once

#include <chrono>
#include <cstdint>
#include <steam_api.h>
#include <string>

#include "net/vpn_wire_types.h"

// Distributed IP negotiation defaults (matches ConnectTool baseline)
constexpr const char *APP_SECRET_SALT = "ConnectTool_VPN_Salt_v1";

// Protocol timing (milliseconds)
constexpr int64_t PROBE_TIMEOUT_MS = 500;
constexpr int64_t HEARTBEAT_INTERVAL_MS = 60000;
constexpr int64_t LEASE_TIME_MS = 120000;
constexpr int64_t LEASE_EXPIRY_MS = 360000;
constexpr int64_t HEARTBEAT_EXPIRY_MS = 180000;

// Node ID
inline constexpr std::size_t NODE_ID_SIZE = connecttool::wire::kNodeIdSize;
using NodeID = connecttool::wire::NodeIdBytes;
using VpnMessageType = connecttool::wire::VpnMessageType;
using VpnMessageHeader = connecttool::wire::VpnMessageHeader;
using VpnPacketWrapper = connecttool::wire::VpnPacketWrapper;
using ProbeRequestPayload = connecttool::wire::ProbeRequestPayload;
using ProbeResponsePayload = connecttool::wire::ProbeResponsePayload;
using AddressAnnouncePayload = connecttool::wire::AddressAnnouncePayload;
using ForcedReleasePayload = connecttool::wire::ForcedReleasePayload;
using HeartbeatPayload = connecttool::wire::HeartbeatPayload;

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
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeat).count();
    return elapsed < HEARTBEAT_EXPIRY_MS;
  }

  bool isLeaseExpired() const {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeat).count();
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
