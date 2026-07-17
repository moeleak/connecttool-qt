#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace connecttool::domain {

template <typename Tag, std::integral Representation = std::uint64_t> class StrongId final {
public:
  using representation_type = Representation;

  constexpr StrongId() noexcept = default;
  explicit constexpr StrongId(Representation value) noexcept : value_(value) {}

  [[nodiscard]] constexpr Representation value() const noexcept { return value_; }

  [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
  explicit constexpr operator bool() const noexcept { return valid(); }

  auto operator<=>(const StrongId &) const = default;

private:
  Representation value_{};
};

struct SteamIdTag;
struct LobbyIdTag;
struct VirtualIpTag;

using SteamId = StrongId<SteamIdTag>;
using LobbyId = StrongId<LobbyIdTag>;
using VirtualIp = StrongId<VirtualIpTag, std::uint32_t>;

} // namespace connecttool::domain

template <typename Tag, std::integral Representation>
struct std::hash<connecttool::domain::StrongId<Tag, Representation>> {
  [[nodiscard]] std::size_t
  operator()(connecttool::domain::StrongId<Tag, Representation> id) const noexcept {
    return std::hash<Representation>{}(id.value());
  }
};
