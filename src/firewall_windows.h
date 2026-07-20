#pragma once

#include <string_view>

struct TunFirewallScope final {
  std::string_view localAddress;
  std::string_view subnetMask;
};

#ifdef _WIN32
bool ensureTunFirewallRule(std::string_view ruleName, const TunFirewallScope &scope);
#else
inline bool ensureTunFirewallRule(std::string_view, const TunFirewallScope &) { return true; }
#endif
