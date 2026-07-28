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
#include <memory>
#include <optional>
#include <sstream>

namespace tun {
namespace {

struct MibTableDeleter final {
  void operator()(void *table) const noexcept { FreeMibTable(table); }
};

using AddressTable = std::unique_ptr<MIB_UNICASTIPADDRESS_TABLE, MibTableDeleter>;

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

[[nodiscard]] NET_LUID makeLuid(std::uint64_t value) noexcept {
  NET_LUID luid{};
  luid.Value = value;
  return luid;
}

[[nodiscard]] std::string systemMessage(unsigned long error) {
  char *buffer = nullptr;
  const DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, error, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
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

  if (!clearIpv4Addresses()) {
    return false;
  }

  MIB_UNICASTIPADDRESS_ROW row{};
  InitializeUnicastIpAddressEntry(&row);
  row.InterfaceLuid = makeLuid(interface_.luid);
  row.InterfaceIndex = interface_.index;
  row.Address.Ipv4.sin_family = AF_INET;
  row.Address.Ipv4.sin_addr = *parsedAddress;
  row.OnLinkPrefixLength = *prefix;

  const DWORD createResult = CreateUnicastIpAddressEntry(&row);
  if (createResult != NO_ERROR && createResult != ERROR_OBJECT_ALREADY_EXISTS) {
    return fail("Failed to assign IPv4 address", createResult);
  }
  lastError_.clear();
  return true;
}

bool WindowsNetworkConfig::clearIpv4Addresses() {
  MIB_UNICASTIPADDRESS_TABLE *rawTable = nullptr;
  const DWORD tableResult = GetUnicastIpAddressTable(AF_INET, &rawTable);
  if (tableResult == ERROR_NOT_FOUND) {
    return true;
  }
  if (tableResult != NO_ERROR) {
    return fail("Failed to enumerate existing IPv4 addresses", tableResult);
  }
  const AddressTable table{rawTable};

  for (ULONG index = 0; index < table->NumEntries; ++index) {
    const auto &candidate = table->Table[index];
    if (candidate.InterfaceLuid.Value != interface_.luid) {
      continue;
    }
    const DWORD deleteResult = DeleteUnicastIpAddressEntry(&candidate);
    if (deleteResult != NO_ERROR && deleteResult != ERROR_NOT_FOUND) {
      return fail("Failed to remove an existing IPv4 address", deleteResult);
    }
  }
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
