#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace connecttool::multiplex {

inline constexpr std::size_t kConnectionIdLength = 6;
inline constexpr std::size_t kHeaderSize = kConnectionIdLength + 1 + 4;

enum class PacketType : std::uint32_t { Data = 0, Disconnect = 1 };

enum class CodecError {
  InvalidConnectionId,
  TruncatedFrame,
  MissingIdTerminator,
  UnknownPacketType,
  UnexpectedDisconnectPayload,
};

class ConnectionId final {
public:
  [[nodiscard]] static std::expected<ConnectionId, CodecError>
  parse(std::string_view value) noexcept;

  [[nodiscard]] std::string_view view() const noexcept {
    return {storage_.data(), storage_.size()};
  }
  [[nodiscard]] std::string toString() const { return std::string{view()}; }

  auto operator<=>(const ConnectionId &) const = default;

private:
  explicit ConnectionId(std::string_view value) noexcept;
  std::array<char, kConnectionIdLength> storage_{};
};

struct FrameView {
  ConnectionId connectionId;
  PacketType type{};
  std::span<const std::byte> payload{};
};

[[nodiscard]] std::vector<std::byte> encodeFrame(const ConnectionId &connectionId, PacketType type,
                                                 std::span<const std::byte> payload = {});

[[nodiscard]] std::expected<FrameView, CodecError>
decodeFrame(std::span<const std::byte> frame) noexcept;

} // namespace connecttool::multiplex
