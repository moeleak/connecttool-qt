#ifdef _WIN32

#include "platform/route/route_manager.h"

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

[[nodiscard]] std::optional<IN_ADDR> parseAddress(const std::string &text) {
  IN_ADDR address{};
  if (InetPtonA(AF_INET, text.c_str(), &address) != 1) {
    return std::nullopt;
  }
  return address;
}

[[nodiscard]] std::optional<UINT8> prefixLength(const std::string &netmask) {
  const auto address = parseAddress(netmask);
  if (!address) {
    return std::nullopt;
  }
  const std::uint32_t mask = ntohl(address->S_un.S_addr);
  const auto prefix = static_cast<UINT8>(std::countl_one(mask));
  const std::uint32_t remainder = prefix == 32 ? 0U : mask << prefix;
  if (remainder != 0) {
    return std::nullopt; // 非连续掩码
  }
  return prefix;
}

[[nodiscard]] IN_ADDR networkAddress(IN_ADDR address, UINT8 prefix) noexcept {
  const std::uint32_t mask =
      prefix == 0 ? 0U : std::numeric_limits<std::uint32_t>::max() << (32U - prefix);
  address.S_un.S_addr = htonl(ntohl(address.S_un.S_addr) & mask);
  return address;
}

[[nodiscard]] std::string systemMessage(unsigned long error) {
  char *buffer = nullptr;
  const DWORD length = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
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

[[nodiscard]] bool failWith(std::string *error, const char *operation,
                            DWORD code) {
  std::ostringstream stream;
  stream << operation << " (Error " << code << ')';
  const std::string message = systemMessage(code);
  if (!message.empty()) {
    stream << ": " << message;
  }
  *error = stream.str();
  return false;
}

// 匹配维度不含接口：用于发现同前缀同地址但绑在其他接口上的冲突条目
// （如系统默认 224.0.0.0/4），由调用方决定先删后建。
[[nodiscard]] bool sameDestination(const MIB_IPFORWARD_ROW2 &candidate,
                                   const MIB_IPFORWARD_ROW2 &desired) noexcept {
  return candidate.DestinationPrefix.PrefixLength ==
             desired.DestinationPrefix.PrefixLength &&
         candidate.DestinationPrefix.Prefix.si_family == AF_INET &&
         candidate.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr ==
             desired.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr;
}

class RouteManagerWindows final : public RouteManager {
public:
  RouteManagerWindows(NET_LUID luid, ULONG index)
      : luid_(luid), index_(index) {}

  bool addRoute(const Ipv4Route &route, std::string *error) override {
    MIB_IPFORWARD_ROW2 desired{};
    if (!buildRow(route, &desired, error)) {
      return false;
    }

    MIB_IPFORWARD_TABLE2 *rawTable = nullptr;
    const DWORD tableResult = GetIpForwardTable2(AF_INET, &rawTable);
    if (tableResult != NO_ERROR && tableResult != ERROR_NOT_FOUND) {
      return failWith(error, "Failed to enumerate IPv4 routes", tableResult);
    }
    const RouteTable table{rawTable};

    if (table) {
      for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto &candidate = table->Table[i];
        if (!sameDestination(candidate, desired)) {
          continue;
        }
        if (candidate.InterfaceLuid.Value == luid_.Value) {
          return true; // 完全相同条目：幂等成功
        }
        // 同前缀不同接口/网关（如系统默认 224.0.0.0/4 条目）：先删后建
        MIB_IPFORWARD_ROW2 stale = candidate;
        const DWORD deleteResult = DeleteIpForwardEntry2(&stale);
        if (deleteResult != NO_ERROR && deleteResult != ERROR_NOT_FOUND) {
          return failWith(error, "Failed to remove a conflicting IPv4 route",
                          deleteResult);
        }
      }
    }

    const DWORD createResult = CreateIpForwardEntry2(&desired);
    if (createResult != NO_ERROR &&
        createResult != ERROR_OBJECT_ALREADY_EXISTS) {
      return failWith(error, "Failed to create IPv4 route", createResult);
    }
    return true;
  }

  bool removeRoute(const Ipv4Route &route, std::string *error) override {
    MIB_IPFORWARD_ROW2 desired{};
    if (!buildRow(route, &desired, error)) {
      return false;
    }

    MIB_IPFORWARD_TABLE2 *rawTable = nullptr;
    const DWORD tableResult = GetIpForwardTable2(AF_INET, &rawTable);
    if (tableResult == ERROR_NOT_FOUND) {
      return true; // 幂等：不存在视为成功
    }
    if (tableResult != NO_ERROR) {
      return failWith(error, "Failed to enumerate IPv4 routes", tableResult);
    }
    const RouteTable table{rawTable};

    for (ULONG i = 0; i < table->NumEntries; ++i) {
      const auto &candidate = table->Table[i];
      if (!sameDestination(candidate, desired) ||
          candidate.InterfaceLuid.Value != luid_.Value) {
        continue;
      }
      // DeleteIpForwardEntry2 参数为非 const 指针，候选项先拷贝到本地再删
      MIB_IPFORWARD_ROW2 stale = candidate;
      const DWORD deleteResult = DeleteIpForwardEntry2(&stale);
      if (deleteResult != NO_ERROR && deleteResult != ERROR_NOT_FOUND) {
        return failWith(error, "Failed to delete IPv4 route", deleteResult);
      }
      return true;
    }
    return true; // 未找到：幂等成功
  }

private:
  [[nodiscard]] bool buildRow(const Ipv4Route &route, MIB_IPFORWARD_ROW2 *row,
                              std::string *error) const {
    const auto network = parseAddress(route.network);
    const auto prefix = prefixLength(route.netmask);
    if (!network || !prefix) {
      *error = "Invalid route " + route.network + "/" + route.netmask;
      return false;
    }
    InitializeIpForwardEntry(row);
    row->InterfaceLuid = luid_;
    row->InterfaceIndex = index_;
    row->DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
    row->DestinationPrefix.Prefix.Ipv4.sin_addr = networkAddress(*network, *prefix);
    row->DestinationPrefix.PrefixLength = *prefix;
    row->NextHop.Ipv4.sin_family = AF_INET; // on-link: 0.0.0.0
    row->SitePrefixLength = *prefix;
    row->Metric = 1;
    row->Protocol = MIB_IPPROTO_NETMGMT;
    return true;
  }

  NET_LUID luid_{};
  ULONG index_ = 0;
};

} // namespace

std::unique_ptr<RouteManager> createRouteManager(const std::string &ifname) {
  // WinTUN 适配器别名为 ASCII，直接加宽为 UTF-16 即可
  const std::wstring alias{ifname.begin(), ifname.end()};
  NET_LUID luid{};
  // ConvertInterfaceAliasToLuid 仅收 PCWSTR（无 A/W 拆分），与 UNICODE 宏无关
  if (ConvertInterfaceAliasToLuid(alias.c_str(), &luid) != NO_ERROR) {
    return nullptr;
  }
  ULONG index = 0;
  if (ConvertInterfaceLuidToIndex(&luid, &index) != NO_ERROR || index == 0) {
    return nullptr;
  }
  return std::make_unique<RouteManagerWindows>(luid, index);
}

} // namespace tun

#endif // _WIN32
