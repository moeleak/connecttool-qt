#ifdef _WIN32

#include "tun_interface.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <rpc.h>
#include <cstdlib>
#include <atomic>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Rpcrt4.lib")

// Minimal WinTUN API declarations (avoid external headers)
struct _WINTUN_ADAPTER;
struct _WINTUN_SESSION;
using WINTUN_ADAPTER_HANDLE = _WINTUN_ADAPTER *;
using WINTUN_SESSION_HANDLE = _WINTUN_SESSION *;

enum WINTUN_LOGGER_LEVEL { WINTUN_LOG_INFO, WINTUN_LOG_WARN, WINTUN_LOG_ERR };
using WINTUN_LOGGER_FUNC =
    void(CALLBACK *)(WINTUN_LOGGER_LEVEL Level, DWORD64 Timestamp,
                     LPCWSTR Message);

using WINTUN_CREATE_ADAPTER_FUNC = WINTUN_ADAPTER_HANDLE(WINAPI *)(LPCWSTR,
                                                                    LPCWSTR,
                                                                    const GUID *);
using WINTUN_OPEN_ADAPTER_FUNC = WINTUN_ADAPTER_HANDLE(WINAPI *)(LPCWSTR);
using WINTUN_CLOSE_ADAPTER_FUNC = void(WINAPI *)(WINTUN_ADAPTER_HANDLE);
using WINTUN_DELETE_DRIVER_FUNC = DWORD(WINAPI *)();
using WINTUN_GET_ADAPTER_LUID_FUNC =
    void(WINAPI *)(WINTUN_ADAPTER_HANDLE, NET_LUID *);
using WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC = DWORD(WINAPI *)();
using WINTUN_SET_LOGGER_FUNC = void(WINAPI *)(WINTUN_LOGGER_FUNC);
using WINTUN_START_SESSION_FUNC =
    WINTUN_SESSION_HANDLE(WINAPI *)(WINTUN_ADAPTER_HANDLE, DWORD);
using WINTUN_END_SESSION_FUNC = void(WINAPI *)(WINTUN_SESSION_HANDLE);
using WINTUN_GET_READ_WAIT_EVENT_FUNC =
    HANDLE(WINAPI *)(WINTUN_SESSION_HANDLE);
using WINTUN_RECEIVE_PACKET_FUNC =
    BYTE *(WINAPI *)(WINTUN_SESSION_HANDLE, DWORD *);
using WINTUN_RELEASE_RECEIVE_PACKET_FUNC =
    void(WINAPI *)(WINTUN_SESSION_HANDLE, BYTE *);
using WINTUN_ALLOCATE_SEND_PACKET_FUNC =
    BYTE *(WINAPI *)(WINTUN_SESSION_HANDLE, DWORD);
using WINTUN_SEND_PACKET_FUNC = void(WINAPI *)(WINTUN_SESSION_HANDLE, BYTE *);

static constexpr DWORD WINTUN_MAX_IP_PACKET_SIZE = 0xFFFF;

namespace tun {

static HMODULE g_wintunModule = nullptr;
static WINTUN_CREATE_ADAPTER_FUNC WintunCreateAdapter = nullptr;
static WINTUN_OPEN_ADAPTER_FUNC WintunOpenAdapter = nullptr;
static WINTUN_CLOSE_ADAPTER_FUNC WintunCloseAdapter = nullptr;
static WINTUN_DELETE_DRIVER_FUNC WintunDeleteDriver = nullptr;
static WINTUN_GET_ADAPTER_LUID_FUNC WintunGetAdapterLUID = nullptr;
static WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC
    WintunGetRunningDriverVersion = nullptr;
static WINTUN_SET_LOGGER_FUNC WintunSetLogger = nullptr;
static WINTUN_START_SESSION_FUNC WintunStartSession = nullptr;
static WINTUN_END_SESSION_FUNC WintunEndSession = nullptr;
static WINTUN_GET_READ_WAIT_EVENT_FUNC WintunGetReadWaitEvent = nullptr;
static WINTUN_RECEIVE_PACKET_FUNC WintunReceivePacket = nullptr;
static WINTUN_RELEASE_RECEIVE_PACKET_FUNC WintunReleaseReceivePacket = nullptr;
static WINTUN_ALLOCATE_SEND_PACKET_FUNC WintunAllocateSendPacket = nullptr;
static WINTUN_SEND_PACKET_FUNC WintunSendPacket = nullptr;

static bool LoadWintun() {
  if (g_wintunModule) {
    return true;
  }

  g_wintunModule = LoadLibraryExW(L"wintun.dll", nullptr,
                                  LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!g_wintunModule) {
    g_wintunModule = LoadLibraryW(L"wintun.dll");
  }
  if (!g_wintunModule) {
    std::cerr << "Failed to load wintun.dll: " << GetLastError() << std::endl;
    return false;
  }

#define LOAD_PROC(Name)                                                        \
  ((Name = reinterpret_cast<decltype(Name)>(                                  \
        GetProcAddress(g_wintunModule, #Name))) == nullptr)

  if (LOAD_PROC(WintunCreateAdapter) || LOAD_PROC(WintunOpenAdapter) ||
      LOAD_PROC(WintunCloseAdapter) || LOAD_PROC(WintunDeleteDriver) ||
      LOAD_PROC(WintunGetAdapterLUID) ||
      LOAD_PROC(WintunGetRunningDriverVersion) || LOAD_PROC(WintunSetLogger) ||
      LOAD_PROC(WintunStartSession) || LOAD_PROC(WintunEndSession) ||
      LOAD_PROC(WintunGetReadWaitEvent) || LOAD_PROC(WintunReceivePacket) ||
      LOAD_PROC(WintunReleaseReceivePacket) ||
      LOAD_PROC(WintunAllocateSendPacket) || LOAD_PROC(WintunSendPacket)) {
    std::cerr << "Failed to resolve WinTUN functions" << std::endl;
    FreeLibrary(g_wintunModule);
    g_wintunModule = nullptr;
    return false;
  }
#undef LOAD_PROC

  WintunSetLogger([](WINTUN_LOGGER_LEVEL Level, DWORD64 /*Timestamp*/,
                     LPCWSTR Message) {
    const char *levelStr = "INFO";
    if (Level == WINTUN_LOG_WARN) {
      levelStr = "WARN";
    } else if (Level == WINTUN_LOG_ERR) {
      levelStr = "ERR";
    }
    std::wcout << L"[WinTUN " << levelStr << L"] " << Message << std::endl;
  });

  const DWORD version = WintunGetRunningDriverVersion();
  if (version == 0) {
    std::cout << "WinTUN driver not running, will be loaded on first adapter "
                 "creation"
              << std::endl;
  } else {
    std::cout << "WinTUN driver version: " << ((version >> 16) & 0xFF) << "."
              << ((version >> 8) & 0xFF) << "." << (version & 0xFF)
              << std::endl;
  }
  return true;
}

class TunWindows : public TunInterface {
public:
  TunWindows()
      : adapter_(nullptr), session_(nullptr), mtu_(1500), nonBlocking_(false),
        adapterIndex_(0), readReady_(false) {
    std::memset(&adapterLuid_, 0, sizeof(adapterLuid_));
  }
  ~TunWindows() override { close(); }

  bool open(const std::string &deviceName, int mtu) override {
    if (adapter_) {
      setError("Adapter already open");
      return false;
    }
    if (!LoadWintun()) {
      setError("Failed to load WinTUN");
      return false;
    }
    mtu_ = mtu;
    const std::string name = deviceName.empty() ? "SteamVPN" : deviceName;
    deviceName_ = name;

    const std::wstring wName(name.begin(), name.end());
    const std::wstring wTunnelType = L"SteamConnectTool";

    GUID requestedGuid{};
    UuidFromStringW((RPC_WSTR)L"e5a3b5c9-8d7e-4f1a-b2c3-d4e5f6a7b8c9",
                    &requestedGuid);

    adapter_ = WintunOpenAdapter(wName.c_str());
    if (!adapter_) {
      adapter_ =
          WintunCreateAdapter(wName.c_str(), wTunnelType.c_str(), &requestedGuid);
      if (!adapter_ && GetLastError() == ERROR_ALREADY_EXISTS) {
        WINTUN_ADAPTER_HANDLE oldAdapter = WintunOpenAdapter(wName.c_str());
        if (oldAdapter) {
          WintunCloseAdapter(oldAdapter);
          Sleep(100);
        }
        adapter_ = WintunCreateAdapter(wName.c_str(), wTunnelType.c_str(),
                                       &requestedGuid);
        if (!adapter_ && GetLastError() == ERROR_ALREADY_EXISTS) {
          GUID newGuid;
          if (UuidCreate(&newGuid) == RPC_S_OK) {
            adapter_ =
                WintunCreateAdapter(wName.c_str(), wTunnelType.c_str(), &newGuid);
          }
        }
      }
    }

    if (!adapter_) {
      setWindowsError("Failed to create/open WinTUN adapter");
      return false;
    }

    WintunGetAdapterLUID(adapter_, &adapterLuid_);
    session_ = WintunStartSession(adapter_, 0x400000);
    if (!session_) {
      setWindowsError("Failed to start WinTUN session");
      WintunCloseAdapter(adapter_);
      adapter_ = nullptr;
      return false;
    }
    std::cout << "WinTUN adapter '" << name << "' opened successfully"
              << std::endl;
    return true;
  }

  void close() override {
    if (session_) {
      WintunEndSession(session_);
      session_ = nullptr;
    }
    if (adapter_) {
      WintunCloseAdapter(adapter_);
      adapter_ = nullptr;
    }
    deviceName_.clear();
  }

  bool is_open() const override { return adapter_ && session_; }

  int read(uint8_t *buffer, size_t size) override {
    if (!session_) {
      return -1;
    }
    DWORD packetSize = 0;
    BYTE *packet = WintunReceivePacket(session_, &packetSize);
    if (!packet) {
      const DWORD error = GetLastError();
      if (error == ERROR_NO_MORE_ITEMS) {
        if (nonBlocking_) {
          return 0;
        }
        HANDLE event = WintunGetReadWaitEvent(session_);
        if (event) {
          WaitForSingleObject(event, 10);
        }
        return 0;
      }
      if (error == ERROR_HANDLE_EOF) {
        return -1;
      }
      return -1;
    }
    const DWORD copySize =
        packetSize < size ? packetSize : static_cast<DWORD>(size);
    std::memcpy(buffer, packet, copySize);
    WintunReleaseReceivePacket(session_, packet);
    return static_cast<int>(copySize);
  }

  int write(const uint8_t *buffer, size_t size) override {
    if (!session_) {
      return -1;
    }
    if (size > WINTUN_MAX_IP_PACKET_SIZE) {
      setError("Packet too large");
      return -1;
    }
    BYTE *packet =
        WintunAllocateSendPacket(session_, static_cast<DWORD>(size));
    if (!packet) {
      const DWORD error = GetLastError();
      if (error == ERROR_BUFFER_OVERFLOW) {
        return 0;
      }
      return -1;
    }
    std::memcpy(packet, buffer, size);
    WintunSendPacket(session_, packet);
    return static_cast<int>(size);
  }

  std::string get_device_name() const override { return deviceName_; }

  bool set_ip(const std::string &ip, const std::string &netmask) override {
    if (!adapter_) {
      setError("Adapter not open");
      return false;
    }
    in_addr addr{};
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
      setError("Invalid IP address: " + ip);
      return false;
    }
    const int prefixLength = NetmaskToPrefixLength(netmask);
    MIB_UNICASTIPADDRESS_ROW addressRow;
    InitializeUnicastIpAddressEntry(&addressRow);
    addressRow.InterfaceLuid = adapterLuid_;
    addressRow.Address.Ipv4.sin_family = AF_INET;
    addressRow.Address.Ipv4.sin_addr = addr;
    addressRow.OnLinkPrefixLength = static_cast<UINT8>(prefixLength);
    addressRow.DadState = IpDadStatePreferred;
    DeleteUnicastIpAddressEntry(&addressRow);

    const DWORD result = CreateUnicastIpAddressEntry(&addressRow);
    if (result != NO_ERROR && result != ERROR_OBJECT_ALREADY_EXISTS) {
      std::ostringstream oss;
      oss << "Failed to set IP address (Error " << result << ")";
      setError(oss.str());
      return false;
    }
    std::cout << "Set IP address: " << ip << "/" << prefixLength << std::endl;
    lastConfiguredIp_ = ip;
    ConvertInterfaceLuidToIndex(&adapterLuid_, &adapterIndex_);
    ensureFirewallRule();
    return true;
  }

  bool add_route(const std::string &network,
                 const std::string &netmask) override {
    // Best effort using route.exe; requires admin.
    if (lastConfiguredIp_.empty() || adapterIndex_ == 0) {
      setError("Adapter IP/index not set for routing");
      return false;
    }
    std::ostringstream del;
    del << "route DELETE " << network << " MASK " << netmask
        << " IF " << adapterIndex_ << " >nul 2>&1";
    ::system(del.str().c_str());
    std::ostringstream cmd;
    cmd << "route ADD " << network << " MASK " << netmask << " "
        << lastConfiguredIp_ << " IF " << adapterIndex_ << " METRIC 1";
    if (::system(cmd.str().c_str()) == 0) {
      return true;
    }
    std::ostringstream cmdChange;
    cmdChange << "route CHANGE " << network << " MASK " << netmask << " "
              << lastConfiguredIp_ << " IF " << adapterIndex_ << " METRIC 1";
    if (::system(cmdChange.str().c_str()) == 0) {
      return true;
    }
    setError("Failed to add route " + network);
    return false;
  }

  bool set_mtu(int mtu) override {
    if (!adapter_) {
      setError("Adapter not open");
      return false;
    }
    mtu_ = mtu;
    std::ostringstream cmd;
    cmd << "netsh interface ipv4 set subinterface \"" << deviceName_
        << "\" mtu=" << mtu << " store=persistent";
    if (::system(cmd.str().c_str()) != 0) {
      setError("Failed to set MTU via netsh");
      return false;
    }
    return true;
  }

  bool set_up(bool up) override {
    if (!adapter_) {
      setError("Adapter not open");
      return false;
    }
    std::ostringstream cmd;
    cmd << "netsh interface set interface \"" << deviceName_ << "\" "
        << (up ? "enable" : "disable");
    if (::system(cmd.str().c_str()) != 0) {
      setError("Failed to change interface state via netsh");
      return false;
    }
    return true;
  }

  bool set_non_blocking(bool nonBlocking) override {
    nonBlocking_ = nonBlocking;
    return true;
  }

  std::string get_last_error() const override { return lastError_; }

  void *get_read_wait_event() const override {
    if (session_) {
      return WintunGetReadWaitEvent(session_);
    }
    return nullptr;
  }

private:
  static std::string escape_ps(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
      if (c == '\'') {
        escaped += "''";
      } else {
        escaped.push_back(c);
      }
    }
    return escaped;
  }

  void ensureFirewallRule() const {
    if (deviceName_.empty()) {
      return;
    }
    const std::string ruleName = "ConnectTool TUN inbound";
    const std::string escapedName = escape_ps(ruleName);
    const std::string escapedAlias = escape_ps(deviceName_);
    std::ostringstream ps;
    ps << "powershell -Command \"$ErrorActionPreference='SilentlyContinue'; "
       << "Remove-NetFirewallRule -DisplayName '" << escapedName
       << "' -ErrorAction SilentlyContinue; "
       << "New-NetFirewallRule -DisplayName '" << escapedName
       << "' -Direction Inbound -Action Allow -Protocol Any "
       << "-InterfaceAlias '" << escapedAlias << "' -Enabled True\"";
    const int rc = ::system(ps.str().c_str());
    if (rc != 0) {
      std::cerr << "Failed to add firewall rule for " << deviceName_
                << " (rc=" << rc << ")" << std::endl;
    } else {
      std::cout << "Added firewall rule for interface " << deviceName_
                << std::endl;
    }
  }

  static int NetmaskToPrefixLength(const std::string &netmask) {
    in_addr addr{};
    if (inet_pton(AF_INET, netmask.c_str(), &addr) != 1) {
      return 24;
    }
    uint32_t mask = ntohl(addr.s_addr);
    int prefix = 0;
    while (mask & 0x80000000) {
      prefix++;
      mask <<= 1;
    }
    return prefix;
  }

  void setError(const std::string &error) {
    lastError_ = error;
    std::cerr << "TUN Error: " << error << std::endl;
  }

  void setWindowsError(const std::string &prefix) {
    DWORD error = GetLastError();
    char *msgBuf = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<LPSTR>(&msgBuf), 0, nullptr);
    std::ostringstream oss;
    oss << prefix << " (Error " << error << ")";
    if (msgBuf) {
      oss << ": " << msgBuf;
      LocalFree(msgBuf);
    }
    setError(oss.str());
  }

  WINTUN_ADAPTER_HANDLE adapter_;
  WINTUN_SESSION_HANDLE session_;
  std::string deviceName_;
  std::string lastError_;
  std::string lastConfiguredIp_;
  int mtu_;
  bool nonBlocking_;
  NET_LUID adapterLuid_;
  ULONG adapterIndex_;
  std::atomic<bool> readReady_;
};

std::unique_ptr<TunInterface> create_tun() {
  return std::make_unique<TunWindows>();
}

} // namespace tun

#endif // _WIN32
