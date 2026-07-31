#include "network/vpn/lan_discovery.h"

#include <cstddef>

namespace connecttool::network {
namespace {

constexpr std::size_t kMinimumIpv4HeaderLength = 20;
constexpr std::size_t kUdpHeaderLength = 8;
constexpr std::uint8_t kUdpProtocol = 17;

[[nodiscard]] std::uint16_t readU16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                    static_cast<std::uint16_t>(bytes[offset + 1]));
}

[[nodiscard]] std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}

void writeU16(std::span<std::uint8_t> bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 1] = static_cast<std::uint8_t>(value & 0xFFU);
}

void writeU32(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  bytes[offset + 2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  bytes[offset + 3] = static_cast<std::uint8_t>(value & 0xFFU);
}

void addChecksumBytes(std::uint32_t &sum, std::span<const std::uint8_t> bytes) {
  std::size_t offset = 0;
  for (; offset + 1 < bytes.size(); offset += 2) {
    sum += readU16(bytes, offset);
  }
  if (offset < bytes.size()) {
    sum += static_cast<std::uint16_t>(bytes[offset]) << 8U;
  }
}

[[nodiscard]] std::uint16_t finishChecksum(std::uint32_t sum) {
  while ((sum >> 16U) != 0) {
    sum = (sum & 0xFFFFU) + (sum >> 16U);
  }
  return static_cast<std::uint16_t>(~sum);
}

[[nodiscard]] std::uint16_t ipv4Checksum(std::span<const std::uint8_t> header) {
  std::uint32_t sum = 0;
  addChecksumBytes(sum, header);
  return finishChecksum(sum);
}

[[nodiscard]] std::uint16_t udpChecksum(std::span<const std::uint8_t> packet, std::size_t udpOffset,
                                        std::uint16_t udpLength) {
  std::uint32_t sum = 0;
  addChecksumBytes(sum, packet.subspan(12, 8)); // source and destination IPs
  sum += kUdpProtocol;
  sum += udpLength;
  addChecksumBytes(sum, packet.subspan(udpOffset, udpLength));
  const std::uint16_t result = finishChecksum(sum);
  // A computed zero is transmitted as all ones; zero means "checksum absent"
  // for IPv4 UDP.
  return result == 0 ? 0xFFFFU : result;
}

} // namespace

std::optional<std::vector<std::uint8_t>>
makeMinecraftLanDiscoveryUnicast(std::span<const std::uint8_t> ipv4Packet,
                                 std::uint32_t localAddress, std::uint32_t remoteAddress) {
  if (localAddress == 0 || remoteAddress == 0 || localAddress == remoteAddress ||
      ipv4Packet.size() < kMinimumIpv4HeaderLength || (ipv4Packet[0] >> 4U) != 4) {
    return std::nullopt;
  }

  const std::size_t headerLength = static_cast<std::size_t>(ipv4Packet[0] & 0x0FU) * 4U;
  if (headerLength < kMinimumIpv4HeaderLength ||
      headerLength + kUdpHeaderLength > ipv4Packet.size()) {
    return std::nullopt;
  }

  const std::uint16_t totalLength = readU16(ipv4Packet, 2);
  if (totalLength < headerLength + kUdpHeaderLength || totalLength > ipv4Packet.size() ||
      ipv4Packet[9] != kUdpProtocol || readU32(ipv4Packet, 16) != kMinecraftLanMulticastAddress) {
    return std::nullopt;
  }

  // Advertisements are small and should never be fragmented. Reject fragments
  // because only the first one contains the UDP header/checksum to rewrite.
  const std::uint16_t fragment = readU16(ipv4Packet, 6);
  if ((fragment & 0x3FFFU) != 0) {
    return std::nullopt;
  }

  const std::size_t udpOffset = headerLength;
  const std::uint16_t udpLength = readU16(ipv4Packet, udpOffset + 4);
  if (readU16(ipv4Packet, udpOffset + 2) != kMinecraftLanDiscoveryPort ||
      udpLength < kUdpHeaderLength || udpOffset + udpLength > totalLength) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> unicast(ipv4Packet.begin(), ipv4Packet.begin() + totalLength);
  std::span<std::uint8_t> mutablePacket{unicast};
  writeU32(mutablePacket, 12, remoteAddress);
  writeU32(mutablePacket, 16, localAddress);

  writeU16(mutablePacket, 10, 0);
  writeU16(mutablePacket, 10,
           ipv4Checksum(std::span<const std::uint8_t>{unicast}.first(headerLength)));

  const std::uint16_t originalUdpChecksum = readU16(ipv4Packet, udpOffset + 6);
  if (originalUdpChecksum != 0) {
    writeU16(mutablePacket, udpOffset + 6, 0);
    writeU16(mutablePacket, udpOffset + 6,
             udpChecksum(std::span<const std::uint8_t>{unicast}, udpOffset, udpLength));
  }
  return unicast;
}

} // namespace connecttool::network
