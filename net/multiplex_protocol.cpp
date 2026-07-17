#include "net/multiplex_protocol.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace connecttool::multiplex {

namespace {

constexpr bool isValidIdCharacter(char character) noexcept {
  return (character >= '0' && character <= '9') || (character >= 'A' && character <= 'Z') ||
         (character >= 'a' && character <= 'z');
}

} // namespace

ConnectionId::ConnectionId(std::string_view value) noexcept {
  std::ranges::copy(value, storage_.begin());
}

std::expected<ConnectionId, CodecError> ConnectionId::parse(std::string_view value) noexcept {
  if (value.size() != kConnectionIdLength || !std::ranges::all_of(value, isValidIdCharacter)) {
    return std::unexpected(CodecError::InvalidConnectionId);
  }
  return ConnectionId{value};
}

std::vector<std::byte> encodeFrame(const ConnectionId &connectionId, PacketType type,
                                   std::span<const std::byte> payload) {
  if (type == PacketType::Disconnect) {
    payload = {};
  }

  std::vector<std::byte> frame(kHeaderSize + payload.size());
  std::ranges::transform(connectionId.view(), frame.begin(),
                         [](char value) { return static_cast<std::byte>(value); });
  frame[kConnectionIdLength] = std::byte{0};

  const auto rawType = static_cast<std::uint32_t>(type);
  static_assert(std::endian::native == std::endian::little,
                "The 1.5.x multiplex protocol stores packet type as little endian");
  std::memcpy(frame.data() + kConnectionIdLength + 1, &rawType, sizeof(rawType));
  std::ranges::copy(payload, frame.begin() + kHeaderSize);
  return frame;
}

std::expected<FrameView, CodecError> decodeFrame(std::span<const std::byte> frame) noexcept {
  if (frame.size() < kHeaderSize) {
    return std::unexpected(CodecError::TruncatedFrame);
  }
  if (frame[kConnectionIdLength] != std::byte{0}) {
    return std::unexpected(CodecError::MissingIdTerminator);
  }

  std::array<char, kConnectionIdLength> idBytes{};
  std::ranges::transform(frame.first(kConnectionIdLength), idBytes.begin(), [](std::byte value) {
    return static_cast<char>(std::to_integer<unsigned char>(value));
  });
  auto connectionId = ConnectionId::parse(std::string_view{idBytes.data(), idBytes.size()});
  if (!connectionId) {
    return std::unexpected(connectionId.error());
  }

  std::uint32_t rawType = 0;
  std::memcpy(&rawType, frame.data() + kConnectionIdLength + 1, sizeof(rawType));
  if (rawType > static_cast<std::uint32_t>(PacketType::Disconnect)) {
    return std::unexpected(CodecError::UnknownPacketType);
  }
  const auto type = static_cast<PacketType>(rawType);
  const auto payload = frame.subspan(kHeaderSize);
  if (type == PacketType::Disconnect && !payload.empty()) {
    return std::unexpected(CodecError::UnexpectedDisconnectPayload);
  }

  return FrameView{.connectionId = *connectionId, .type = type, .payload = payload};
}

} // namespace connecttool::multiplex
