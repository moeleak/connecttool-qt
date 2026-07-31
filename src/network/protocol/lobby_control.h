#pragma once

#include <string_view>

namespace connecttool::lobby {

inline constexpr std::string_view kPingPrefix = "PING|";
inline constexpr std::string_view kLegacyProfilePrefix = "PROFILE|";

enum class PayloadKind {
  UserChat,
  Ping,
  LegacyProfile,
};

// Lobby chat is also used as a compatibility transport by older clients.
// Classify those reserved frames before they reach the user-visible chat model.
[[nodiscard]] constexpr PayloadKind classifyPayload(std::string_view payload) {
  if (payload.starts_with(kPingPrefix)) {
    return PayloadKind::Ping;
  }
  if (payload.starts_with(kLegacyProfilePrefix)) {
    return PayloadKind::LegacyProfile;
  }
  return PayloadKind::UserChat;
}

} // namespace connecttool::lobby
