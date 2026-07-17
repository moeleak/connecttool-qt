#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace connecttool::wire {

inline constexpr std::size_t kNodeIdSize = 32;
using NodeIdBytes = std::array<std::uint8_t, kNodeIdSize>;

enum class VpnMessageType : std::uint8_t {
  IpPacket = 1,
  RouteUpdate = 3,
  ProbeRequest = 10,
  ProbeResponse = 11,
  AddressAnnounce = 12,
  ForcedRelease = 13,
  Heartbeat = 14,
  HeartbeatAck = 15,
  SessionHello = 20,

  // Source-compatible names retained while the application migrates from
  // the 1.5.x protocol declarations.
  IP_PACKET = IpPacket,
  ROUTE_UPDATE = RouteUpdate,
  PROBE_REQUEST = ProbeRequest,
  PROBE_RESPONSE = ProbeResponse,
  ADDRESS_ANNOUNCE = AddressAnnounce,
  FORCED_RELEASE = ForcedRelease,
  HEARTBEAT = Heartbeat,
  HEARTBEAT_ACK = HeartbeatAck,
  SESSION_HELLO = SessionHello,
};

#pragma pack(push, 1)
struct VpnMessageHeader {
  VpnMessageType type{};
  std::uint16_t length{};
};

struct VpnPacketWrapper {
  NodeIdBytes senderNodeId{};
  std::uint32_t sourceIP{};
};

struct ProbeRequestPayload {
  std::uint32_t ipAddress{};
  NodeIdBytes nodeId{};
};

struct ProbeResponsePayload {
  std::uint32_t ipAddress{};
  NodeIdBytes nodeId{};
  std::int64_t lastHeartbeatMs{};
};

struct AddressAnnouncePayload {
  std::uint32_t ipAddress{};
  NodeIdBytes nodeId{};
};

struct ForcedReleasePayload {
  std::uint32_t ipAddress{};
  NodeIdBytes winnerNodeId{};
};

struct HeartbeatPayload {
  std::uint32_t ipAddress{};
  NodeIdBytes nodeId{};
  std::int64_t timestampMs{};
};
#pragma pack(pop)

static_assert(sizeof(VpnMessageHeader) == 3);
static_assert(sizeof(VpnPacketWrapper) == 36);
static_assert(sizeof(ProbeRequestPayload) == 36);
static_assert(sizeof(ProbeResponsePayload) == 44);
static_assert(sizeof(AddressAnnouncePayload) == 36);
static_assert(sizeof(ForcedReleasePayload) == 36);
static_assert(sizeof(HeartbeatPayload) == 44);
static_assert(std::is_trivially_copyable_v<VpnMessageHeader>);

} // namespace connecttool::wire
