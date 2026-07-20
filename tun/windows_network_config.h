#pragma once

#ifdef _WIN32

#include <cstdint>
#include <string>
#include <string_view>

namespace tun {

struct WindowsInterfaceId final {
  std::uint64_t luid = 0;
  std::uint32_t index = 0;
};

struct Ipv4AddressSpec final {
  std::string_view address;
  std::string_view netmask;
};

struct Ipv4RouteSpec final {
  std::string_view network;
  std::string_view netmask;
  std::string_view nextHop;
};

class WindowsNetworkConfig final {
public:
  void bind(WindowsInterfaceId interfaceId) noexcept;
  void clear() noexcept;

  [[nodiscard]] bool assignAddress(const Ipv4AddressSpec &address);
  [[nodiscard]] bool replaceRoute(const Ipv4RouteSpec &route);
  [[nodiscard]] bool setMtu(int mtu);
  [[nodiscard]] bool setEnabled(bool enabled);
  [[nodiscard]] std::string_view lastError() const noexcept { return lastError_; }

private:
  [[nodiscard]] bool fail(std::string_view operation, unsigned long error);
  [[nodiscard]] bool isBound() const noexcept;

  WindowsInterfaceId interface_;
  std::string lastError_;
};

} // namespace tun

#endif
