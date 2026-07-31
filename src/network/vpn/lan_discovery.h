#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace connecttool::network {

inline constexpr std::uint32_t kMinecraftLanMulticastAddress = 0xE000023CU;
inline constexpr std::uint16_t kMinecraftLanDiscoveryPort = 4445;

// Minecraft joins its discovery multicast group on one concrete interface.
// A packet received from a TUN interface is therefore not delivered when the
// game joined the group on a physical interface. For Minecraft advertisements,
// create a unicast copy addressed to our TUN IP. The source is canonicalized to
// the authenticated remote peer's TUN IP so the server entry remains reachable.
// The caller should inject both the original and this fallback copy.
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
makeMinecraftLanDiscoveryUnicast(std::span<const std::uint8_t> ipv4Packet,
                                 std::uint32_t localAddress, std::uint32_t remoteAddress);

} // namespace connecttool::network
