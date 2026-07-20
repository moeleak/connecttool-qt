#pragma once

#include <cstddef>
#include <span>

namespace connecttool::network {

enum class SendStatus { Sent, Backpressured, Dropped };

class ReliableChannel {
public:
  virtual ~ReliableChannel() = default;

  [[nodiscard]] virtual SendStatus send(std::span<const std::byte> payload) = 0;
  [[nodiscard]] virtual std::size_t pendingReliableBytes() const = 0;
};

} // namespace connecttool::network
