#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace connecttool::network::detail {

[[nodiscard]] std::array<std::uint8_t, 32> sha256(std::span<const std::byte> input);

} // namespace connecttool::network::detail
