#pragma once

#include "net/vpn_wire_types.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace connecttool::wire {

enum class DecodeError {
  TruncatedHeader,
  TruncatedPayload,
  PayloadTooLarge,
  InvalidPayloadSize,
  InvalidMessageType,
};

struct EnvelopeView {
  VpnMessageType type{};
  std::span<const std::byte> payload{};
};

[[nodiscard]] constexpr bool isKnown(VpnMessageType type) noexcept {
  switch (type) {
  case VpnMessageType::IpPacket:
  case VpnMessageType::RouteUpdate:
  case VpnMessageType::ProbeRequest:
  case VpnMessageType::ProbeResponse:
  case VpnMessageType::AddressAnnounce:
  case VpnMessageType::ForcedRelease:
  case VpnMessageType::Heartbeat:
  case VpnMessageType::HeartbeatAck:
  case VpnMessageType::SessionHello:
    return true;
  }
  return false;
}

[[nodiscard]] constexpr std::uint16_t
readBigEndian16(std::span<const std::byte, 2> bytes) noexcept {
  return static_cast<std::uint16_t>((std::to_integer<std::uint16_t>(bytes[0]) << 8U) |
                                    std::to_integer<std::uint16_t>(bytes[1]));
}

constexpr void writeBigEndian16(std::uint16_t value, std::span<std::byte, 2> output) noexcept {
  output[0] = static_cast<std::byte>((value >> 8U) & 0xffU);
  output[1] = static_cast<std::byte>(value & 0xffU);
}

[[nodiscard]] inline std::expected<std::vector<std::byte>, DecodeError>
encodeEnvelope(VpnMessageType type, std::span<const std::byte> payload) {
  if (!isKnown(type)) {
    return std::unexpected(DecodeError::InvalidMessageType);
  }
  if (payload.size() > std::numeric_limits<std::uint16_t>::max()) {
    return std::unexpected(DecodeError::PayloadTooLarge);
  }

  std::vector<std::byte> result(sizeof(VpnMessageHeader) + payload.size());
  result[0] = static_cast<std::byte>(type);
  writeBigEndian16(static_cast<std::uint16_t>(payload.size()),
                   std::span<std::byte, 2>{result.data() + 1, 2});
  std::ranges::copy(payload, result.begin() + sizeof(VpnMessageHeader));
  return result;
}

[[nodiscard]] inline std::expected<EnvelopeView, DecodeError>
decodeEnvelope(std::span<const std::byte> message) noexcept {
  if (message.size() < sizeof(VpnMessageHeader)) {
    return std::unexpected(DecodeError::TruncatedHeader);
  }

  const auto type = static_cast<VpnMessageType>(std::to_integer<std::uint8_t>(message.front()));
  if (!isKnown(type)) {
    return std::unexpected(DecodeError::InvalidMessageType);
  }

  const auto payloadSize = readBigEndian16(std::span<const std::byte, 2>{message.data() + 1, 2});
  if (message.size() < sizeof(VpnMessageHeader) + payloadSize) {
    return std::unexpected(DecodeError::TruncatedPayload);
  }

  return EnvelopeView{
      .type = type,
      .payload = message.subspan(sizeof(VpnMessageHeader), payloadSize),
  };
}

template <typename Payload>
concept WirePayload = std::is_trivially_copyable_v<Payload> && std::is_standard_layout_v<Payload>;

template <WirePayload Payload>
[[nodiscard]] std::span<const std::byte, sizeof(Payload)> asBytes(const Payload &payload) noexcept {
  return std::as_bytes(std::span<const Payload, 1>{&payload, std::size_t{1}});
}

template <WirePayload Payload>
[[nodiscard]] std::expected<Payload, DecodeError>
decodePayload(std::span<const std::byte> bytes) noexcept {
  if (bytes.size() != sizeof(Payload)) {
    return std::unexpected(DecodeError::InvalidPayloadSize);
  }
  Payload payload{};
  std::memcpy(&payload, bytes.data(), sizeof(Payload));
  return payload;
}

template <WirePayload Payload>
[[nodiscard]] std::expected<Payload, DecodeError>
decodePayloadPrefix(std::span<const std::byte> bytes) noexcept {
  if (bytes.size() < sizeof(Payload)) {
    return std::unexpected(DecodeError::InvalidPayloadSize);
  }
  return decodePayload<Payload>(bytes.first(sizeof(Payload)));
}

} // namespace connecttool::wire
