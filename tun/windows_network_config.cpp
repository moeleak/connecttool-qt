#ifdef _WIN32

#include "windows_network_config.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <iphlpapi.h>
#include <netioapi.h>

#include <bit>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>

namespace tun {
namespace {

struct MibTableDeleter final {
  void operator()(void *table) const noexcept { FreeMibTable(table); }
};

using RouteTable = std::unique_ptr<MIB_IPFORWARD_TABLE2, MibTableDeleter>;

[[nodiscard]] std::optional<IN_ADDR> parseAddress(std::string_view text) {
  IN_ADDR address{};
  const std::string terminated{text};
  if (InetPtonA(AF_INET, terminated.c_str(), &address) != 1) {
    return std::nullopt;
  }
  return address;
}

[[nodiscard]] std::optional<UINT8> prefixLength(std::string_view netmask) {
  const auto address = parseAddress(netmask);
  if (!address) {
    return std::nullopt;
  }

  const std::uint32_t mask = ntohl(address->S_un.S_addr);
  const auto prefix = static_cast<UINT8>(std::countl_one(mask));
  const std::uint32_t remainder = prefix == 32 ? 0U : mask << prefix;
  if (remainder != 0) {
    return std::nullopt;
  }
  return prefix;
}

[[nodiscard]] IN_ADDR networkAddress(IN_ADDR address, UINT8 prefix) noexcept {
  const std::uint32_t mask =
      prefix == 0 ? 0U : std::numeric_limits<std::uint32_t>::max() << (32U - prefix);
  address.S_un.S_addr = htonl(ntohl(address.S_un.S_addr) & mask);
  return address;
}

[[nodiscard]] NET_LUID makeLuid(std::uint64_t value) noexcept {
  NET_LUID luid{};
  luid.Value = value;
  return luid;
}

[[nodiscard]] bool matchesRoute(const MIB_IPFORWARD_ROW2 &candidate,
                                const MIB_IPFORWARD_ROW2 &desired) noexcept {
  return candidate.InterfaceLuid.Value == desired.InterfaceLuid.Value &&
         candidate.DestinationPrefix.PrefixLength == desired.DestinationPrefix.PrefixLength &&
         candidate.DestinationPrefix.Prefix.si_family == AF_INET &&
         candidate.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr ==
             desired.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr;
}

[[nodiscard]] std::string systemMessage(unsigned long error) {
  char *buffer = nullptr;
  const DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      reinterpret_cast<char *>(&buffer), 0, nullptr);
  if (length == 0 || buffer == nullptr) {
    return {};
  }

  std::string message{buffer, length};
  LocalFree(buffer);
  while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
    message.pop_back();
  }
  return message;
}

} // namespace

void WindowsNetworkConfig::bind(WindowsInterfaceId interfaceId) noexcept {
  interface_ = interfaceId;
  lastError_.clear();
}

void WindowsNetworkConfig::clear() noexcept {
  interface_ = {};
  lastError_.clear();
}

bool WindowsNetworkConfig::assignAddress(const Ipv4AddressSpec &address) {
  if (!isBound()) {
    return fail("Address interface is not bound", ERROR_INVALID_PARAMETER);
  }

  const auto parsedAddress = parseAddress(address.address);
  const auto prefix = prefixLength(address.netmask);
  if (!parsedAddress || !prefix) {
    return fail("Invalid IPv4 interface address", ERROR_INVALID_PARAMETER);
  }

  MIB_UNICASTIPADDRESS_ROW row{};
  InitializeUnicastIpAddressEntry(&row);
  row.InterfaceLuid = makeLuid(interface_.luid);
  row.InterfaceIndex = interface_.index;
  row.Address.Ipv4.sin_family = AF_INET;
  row.Address.Ipv4.sin_addr = *parsedAddress;
  row.OnLinkPrefixLength = *prefix;
  row.DadState = IpDadStatePreferred;

  const DWORD deleteResult = DeleteUnicastIpAddressEntry(&row);
  if (deleteResult != NO_ERROR && deleteResult != ERROR_NOT_FOUND) {
    return fail("Failed to replace existing IPv4 address", deleteResult);
  }

  const DWORD createResult = CreateUnicastIpAddressEntry(&row);
  if (createResult != NO_ERROR && createResult != ERROR_OBJECT_ALREADY_EXISTS) {
    return fail("Failed to assign IPv4 address", createResult);
  }
  lastError_.clear();
  return true;
}

bool WindowsNetworkConfig::replaceRoute(const Ipv4RouteSpec &route) {
  if (!isBound()) {
    return fail("Route interface is not bound", ERROR_INVALID_PARAMETER);
  }

  const auto network = parseAddress(route.network);
  const auto nextHop = parseAddress(route.nextHop);
  const auto prefix = prefixLength(route.netmask);
  if (!network || !nextHop || !prefix) {
    return fail("Invalid IPv4 route", ERROR_INVALID_PARAMETER);
  }

  MIB_IPFORWARD_ROW2 desired{};
  InitializeIpForwardEntry(&desired);
  desired.InterfaceLuid = makeLuid(interface_.luid);
  desired.InterfaceIndex = interface_.index;
  desired.DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
  desired.DestinationPrefix.Prefix.Ipv4.sin_addr = networkAddress(*network, *prefix);
  desired.DestinationPrefix.PrefixLength = *prefix;
  desired.NextHop.Ipv4.sin_family = AF_INET;
  desired.NextHop.Ipv4.sin_addr = *nextHop;
  desired.SitePrefixLength = *prefix;
  desired.Metric = 1;
  desired.Protocol = MIB_IPPROTO_NETMGMT;

  MIB_IPFORWARD_TABLE2 *rawTable = nullptr;
  const DWORD tableResult = GetIpForwardTable2(AF_INET, &rawTable);
  if (tableResult != NO_ERROR) {
    return fail("Failed to enumerate IPv4 routes", tableResult);
  }
  const RouteTable table{rawTable};

  for (ULONG index = 0; index < table->NumEntries; ++index) {
    const auto &candidate = table->Table[index];
    if (!matchesRoute(candidate, desired)) {
      continue;
    }
    const DWORD deleteResult = DeleteIpForwardEntry2(&candidate);
    if (deleteResult != NO_ERROR && deleteResult != ERROR_NOT_FOUND) {
      return fail("Failed to replace existing IPv4 route", deleteResult);
    }
  }

  const DWORD createResult = CreateIpForwardEntry2(&desired);
  if (createResult != NO_ERROR && createResult != ERROR_OBJECT_ALREADY_EXISTS) {
    return fail("Failed to create IPv4 route", createResult);
  }
  lastError_.clear();
  return true;
}

bool WindowsNetworkConfig::setMtu(int mtu) {
  if (!isBound() || mtu <= 0 || mtu > 65'535) {
    return fail("Invalid interface MTU", ERROR_INVALID_PARAMETER);
  }

  MIB_IPINTERFACE_ROW row{};
  InitializeIpInterfaceEntry(&row);
  row.Family = AF_INET;
  row.InterfaceLuid = makeLuid(interface_.luid);
  row.InterfaceIndex = interface_.index;

  const DWORD getResult = GetIpInterfaceEntry(&row);
  if (getResult != NO_ERROR) {
    return fail("Failed to read IPv4 interface", getResult);
  }

  row.NlMtu = static_cast<ULONG>(mtu);
  row.SitePrefixLength = 0;
  const DWORD setResult = SetIpInterfaceEntry(&row);
  if (setResult != NO_ERROR) {
    return fail("Failed to set interface MTU", setResult);
  }
  lastError_.clear();
  return true;
}

bool WindowsNetworkConfig::setEnabled(bool enabled) {
  if (!isBound()) {
    return fail("Interface is not bound", ERROR_INVALID_PARAMETER);
  }

  MIB_IFROW row{};
  row.dwIndex = interface_.index;
  row.dwAdminStatus = enabled ? MIB_IF_ADMIN_STATUS_UP : MIB_IF_ADMIN_STATUS_DOWN;
  const DWORD result = SetIfEntry(&row);
  if (result != NO_ERROR) {
    return fail("Failed to change interface state", result);
  }
  lastError_.clear();
  return true;
}

bool WindowsNetworkConfig::fail(std::string_view operation, unsigned long error) {
  std::ostringstream stream;
  stream << operation << " (Error " << error << ')';
  const std::string message = systemMessage(error);
  if (!message.empty()) {
    stream << ": " << message;
  }
  lastError_ = stream.str();
  return false;
}

bool WindowsNetworkConfig::isBound() const noexcept {
  return interface_.luid != 0 && interface_.index != 0;
}

} // namespace tun

#endif
