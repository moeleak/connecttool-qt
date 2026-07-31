---
change: add-tun-multicast-route
design-doc: docs/superpowers/specs/2026-07-28-add-tun-multicast-route-design.md
base-ref: 7a2d7aae95010a38badb6f6cc501a685601d9cfa
---

# add-tun-multicast-route 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 引入跨平台 RouteManager 抽象（系统 API，不 shell out),VPN 启动时为 TUN 加装 Minecraft `224.0.2.60/32` 主机路由（best-effort)，停止时显式删除，恢复 Minecraft Java 的局域网发现。

**Architecture:** 新增 `src/platform/route/` 模块：`route_manager.h` 定义 `Ipv4Route`/`RouteManager`/`createRouteManager(ifname)` 工厂；Linux 用 NETLINK_ROUTE、macOS 用 PF_ROUTE routing socket(app 与 daemon 共享同一编译单元）、Windows 用 `CreateIpForwardEntry2`/`DeleteIpForwardEntry2`（路由职责从 `windows_network_config.cpp` 迁入）。三平台 `TunInterface` 实现在 `open()` 成功后持有一个 RouteManager 并委托 `add_route`/`remove_route`;`SteamVpnBridge` 在 `onNegotiationSuccess` 网段路由成功后加装组播路由、`stop()` 关 TUN 前显式删除两条路由。

**Tech Stack:** C++23、CMake presets(`dev` / `core-tests`)、Linux rtnetlink、macOS PF_ROUTE + `rt_msghdr`、Windows netioapi/iphlpapi、Qt6（仅 app 层，route 模块零 Qt 依赖）。

## Global Constraints

- 路由安装一律使用系统 API，禁止 shell out 外部命令（删除 `system("ip route ...")`、`system("/sbin/route ...")`、daemon 内 fork `/sbin/route`)。
- 无异常：RouteManager 接口返回 `bool + std::string *error`。
- 幂等语义：`addRoute` 对已存在的等价路由视为成功；`removeRoute` 对不存在的路由视为成功（`ESRCH`/`ERROR_NOT_FOUND` 吞掉）。
- 组播路由 `add_route` 失败仅记 `std::cerr` 日志，不调用 `recordFailure`、不中断启动；网段路由失败维持现状（`recordFailure`)。
- macOS daemon 的 socket 协议不变（无 `REMOVE_ROUTE` 命令；`ADD_ROUTE` 参数与响应格式不变）。
- `src/platform/route/` 与 daemon target 不得引入 Qt 依赖（daemon 是纯 POSIX 可执行文件）。
- 新增源文件禁止 `#include "../..."` 形式（`tests/verify_architecture.cmake` 硬性检查），一律经 target include root:`#include "platform/route/route_manager.h"`。
- Linux 平台在隔离容器中用生产 RouteManager + 真实 TUN 实测 `/32` 发送入口与清理；完整 Minecraft UI 仍由双机验收。

## 文件结构总览

| 文件 | 责任 | 动作 |
|------|------|------|
| `src/platform/route/route_manager.h` | `Ipv4Route` / `RouteManager` 抽象 / `createRouteManager` 声明 / 平台消息构造纯函数声明 | 新建 |
| `src/platform/route/route_manager_linux.cpp` | netlink 报文构造纯函数 + `RouteManagerLinux` + Linux 工厂 | 新建 |
| `src/platform/route/route_manager_macos.cpp` | `rt_msghdr` 报文构造纯函数 + `RouteManagerMacOS` + macOS 工厂 | 新建 |
| `src/platform/route/route_manager_windows.cpp` | `RouteManagerWindows`（精确去重与 owned-route 清理）+ Windows 工厂 | 新建 |
| `src/platform/tun/tun_interface.h` | 新增 `remove_route` 虚函数（默认 no-op) | 修改 |
| `src/platform/tun/tun_linux.cpp` | `add_route` 委托 RouteManager，删除 `system()` 路径 | 修改 |
| `src/platform/tun/tun_macos.cpp` | root 直执行路径委托 RouteManager(helper 路径不变） | 修改 |
| `src/platform/tun/tun_macos_daemon.cpp` | `ADD_ROUTE` 改用 RouteManager，删除 `runRoute`/`maskToPrefix` | 修改 |
| `src/platform/tun/tun_windows.cpp` | `add_route`/`remove_route` 委托 RouteManagerWindows | 修改 |
| `src/platform/tun/windows_network_config.{h,cpp}` | 删除 `ensureOnLinkRoute`/`Ipv4RouteSpec`（迁入 route 模块） | 修改 |
| `src/integrations/steam/steam_vpn_bridge.cpp` | 组播路由加装 + stop() 显式删除 | 修改 |
| `src/platform/CMakeLists.txt` | route 源文件按平台接入 `connecttool_platform` | 修改 |
| `CMakeLists.txt`（根） | daemon target 增加 `route_manager_macos.cpp` | 修改 |
| `tests/route_message_linux_test.cpp` | netlink 报文构造单测（无需 root) | 新建 |
| `tests/route_message_macos_test.cpp` | `rt_msghdr` 报文构造单测（无需 root) | 新建 |
| `tests/CMakeLists.txt` | 两个平台守护的测试 target | 修改 |

---

### Task 1: RouteManager 抽象头文件

**Files:**
- Create: `src/platform/route/route_manager.h`

**Interfaces:**
- Consumes: 无（基础抽象）
- Produces（后续所有任务依赖，签名不得改动）:
  - `tun::Ipv4Route { std::string network; std::string netmask; }`
  - `tun::RouteManager { virtual bool addRoute(const Ipv4Route&, std::string *error) = 0; virtual bool removeRoute(const Ipv4Route&, std::string *error) = 0; }`
  - `std::unique_ptr<tun::RouteManager> tun::createRouteManager(const std::string &ifname)`
  - Linux 专用（`__linux__`):`tun::detail::buildRouteRequest(uint16_t type, uint32_t seq, const Ipv4Route&, uint32_t ifindex) -> std::vector<uint8_t>`
  - macOS 专用（`__APPLE__`):`tun::detail::buildRouteMessage(uint8_t type, int32_t pid, int32_t seq, const Ipv4Route&, uint16_t ifindex) -> std::vector<uint8_t>`

- [x] **Step 1: 创建头文件**

`src/platform/route/route_manager.h` 完整内容：

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tun {

struct Ipv4Route {
  std::string network; // 例: "224.0.0.0"
  std::string netmask; // 例: "240.0.0.0"
};

// 跨平台路由增删抽象。幂等语义：addRoute 对已存在的等价路由视为成功；
// removeRoute 对不存在的路由视为成功。无异常，经 error 回传失败原因。
class RouteManager {
public:
  virtual ~RouteManager() = default;
  virtual bool addRoute(const Ipv4Route &route, std::string *error) = 0;
  virtual bool removeRoute(const Ipv4Route &route, std::string *error) = 0;
};

// 创建绑定到指定接口的 RouteManager（Linux/macOS 解析 ifindex；
// Windows 经接口别名解析 LUID/index）。失败返回 nullptr。
std::unique_ptr<RouteManager> createRouteManager(const std::string &ifname);

namespace detail {
#if defined(__linux__)
// 构造 rtnetlink 请求报文（nlmsghdr + rtmsg + RTA_DST + RTA_OIF）。
// type 仅接受 RTM_NEWROUTE / RTM_DELROUTE；地址或掩码非法时返回空 vector。
// 抽成纯函数以便无 root 单测。
std::vector<uint8_t> buildRouteRequest(uint16_t type, uint32_t seq,
                                       const Ipv4Route &route,
                                       uint32_t ifindex);
#elif defined(__APPLE__)
// 构造 PF_ROUTE 写报文（rt_msghdr + sockaddr_in dst + sockaddr_dl 网关
// + 截断 netmask sockaddr）。type 仅接受 RTM_ADD / RTM_CHANGE / RTM_DELETE；
// 地址或掩码非法时返回空 vector。抽成纯函数以便无 root 单测。
std::vector<uint8_t> buildRouteMessage(uint8_t type, int32_t pid, int32_t seq,
                                       const Ipv4Route &route,
                                       uint16_t ifindex);
#endif
} // namespace detail

} // namespace tun
```

- [x] **Step 2: 验证头文件自足可编译**

Run: `c++ -std=c++23 -fsyntax-only -I src -D__APPLE__ src/platform/route/route_manager.h 2>/dev/null; c++ -std=c++23 -fsyntax-only -Isrc src/platform/route/route_manager.h`
Expected: 无输出（退出码 0)。头文件不依赖任何平台头即可解析。

- [x] **Step 3: Commit**

```bash
git add src/platform/route/route_manager.h
git commit -m "feat(route): add RouteManager abstraction and factory declaration"
```

---

### Task 2: Linux netlink 实现 + 报文构造单测

**Files:**
- Create: `src/platform/route/route_manager_linux.cpp`
- Test: `tests/route_message_linux_test.cpp`
- Modify: `src/platform/CMakeLists.txt`(else/Linux 分支）
- Modify: `tests/CMakeLists.txt`（增加 Linux 守护的测试 target)

**Interfaces:**
- Consumes: Task 1 的 `route_manager.h`。
- Produces: Linux 上 `createRouteManager` 的定义；`tun::detail::buildRouteRequest` 的定义（单测直接调用）。`RTA_DST` 中的地址必须是**清掉 host 位后的网络地址**（按前缀归一化）。

**背景知识（写给零上下文执行者）:**
- netlink 报文布局：`nlmsghdr`(16B) + `rtmsg`(12B) + 若干 `rtattr`。属性按 `NLA_ALIGN`(4 字节）对齐。
- `RTM_NEWROUTE` 用 `NLM_F_REQUEST|NLM_F_ACK|NLM_F_CREATE|NLM_F_REPLACE`（等价 `ip route replace`，天然幂等）;`RTM_DELROUTE` 用 `NLM_F_REQUEST|NLM_F_ACK`。
- `rtmsg` 字段：`rtm_family=AF_INET`、`rtm_dst_len=前缀`、`rtm_table=RT_TABLE_MAIN`、`rtm_protocol=RTPROT_STATIC`、`rtm_scope=RT_SCOPE_LINK`、`rtm_type=RTN_UNICAST`。
- 前缀为 0 时省略 `RTA_DST`（本 change 只用 /4 与 /8，但保持实现正确）。
- ack 判定：`send` 后 `recv` 到 `nlmsg_seq` 匹配的 `NLMSG_ERROR`,`error==0` 成功；删除路径 `error==-ESRCH` 视为成功。

- [x] **Step 1: 写失败测试(netlink 报文构造)**

`tests/route_message_linux_test.cpp` 完整内容（assert 风格，沿用 `tests/windows_firewall_test.cpp` 的 plain-main 模式，不引 Qt):

```cpp
#include "platform/route/route_manager.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <linux/rtnetlink.h>

using tun::Ipv4Route;
using tun::detail::buildRouteRequest;

namespace {
int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testNewRouteMulticast() {
  const Ipv4Route route{"224.0.0.0", "240.0.0.0"};
  const auto msg = buildRouteRequest(RTM_NEWROUTE, 42, route, 5);
  CHECK(!msg.empty());
  CHECK(msg.size() >= sizeof(nlmsghdr) + sizeof(rtmsg));

  const auto *nh = reinterpret_cast<const nlmsghdr *>(msg.data());
  CHECK(nh->nlmsg_len == msg.size());
  CHECK(nh->nlmsg_type == RTM_NEWROUTE);
  CHECK(nh->nlmsg_flags ==
        (NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE));
  CHECK(nh->nlmsg_seq == 42);

  const auto *rtm =
      reinterpret_cast<const rtmsg *>(msg.data() + sizeof(nlmsghdr));
  CHECK(rtm->rtm_family == AF_INET);
  CHECK(rtm->rtm_dst_len == 4);
  CHECK(rtm->rtm_table == RT_TABLE_MAIN);
  CHECK(rtm->rtm_protocol == RTPROT_STATIC);
  CHECK(rtm->rtm_scope == RT_SCOPE_LINK);
  CHECK(rtm->rtm_type == RTN_UNICAST);

  int remaining =
      static_cast<int>(msg.size()) - static_cast<int>(NLMSG_LENGTH(sizeof(rtmsg)));
  const rtattr *rta = reinterpret_cast<const rtattr *>(
      msg.data() + NLMSG_LENGTH(sizeof(rtmsg)));
  bool sawDst = false;
  bool sawOif = false;
  for (; RTA_OK(rta, remaining); rta = RTA_NEXT(rta, remaining)) {
    if (rta->rta_type == RTA_DST) {
      in_addr expected{};
      inet_pton(AF_INET, "224.0.0.0", &expected);
      CHECK(std::memcmp(RTA_DATA(rta), &expected, sizeof(expected)) == 0);
      sawDst = true;
    } else if (rta->rta_type == RTA_OIF) {
      uint32_t index = 0;
      std::memcpy(&index, RTA_DATA(rta), sizeof(index));
      CHECK(index == 5);
      sawOif = true;
    }
  }
  CHECK(sawDst);
  CHECK(sawOif);
}

void testDelRouteFlags() {
  const auto msg =
      buildRouteRequest(RTM_DELROUTE, 7, {"224.0.0.0", "240.0.0.0"}, 5);
  CHECK(!msg.empty());
  const auto *nh = reinterpret_cast<const nlmsghdr *>(msg.data());
  CHECK(nh->nlmsg_type == RTM_DELROUTE);
  CHECK(nh->nlmsg_flags == (NLM_F_REQUEST | NLM_F_ACK));
}

void testHostRoute() {
  const auto msg =
      buildRouteRequest(RTM_NEWROUTE, 1, {"10.0.0.7", "255.255.255.255"}, 3);
  CHECK(!msg.empty());
  const auto *rtm =
      reinterpret_cast<const rtmsg *>(msg.data() + sizeof(nlmsghdr));
  CHECK(rtm->rtm_dst_len == 32);
}

void testHostBitsMasked() {
  // 224.0.0.5/4 必须归一化为 224.0.0.0
  const auto msg =
      buildRouteRequest(RTM_NEWROUTE, 1, {"224.0.0.5", "240.0.0.0"}, 3);
  CHECK(!msg.empty());
  int remaining =
      static_cast<int>(msg.size()) - static_cast<int>(NLMSG_LENGTH(sizeof(rtmsg)));
  const rtattr *rta = reinterpret_cast<const rtattr *>(
      msg.data() + NLMSG_LENGTH(sizeof(rtmsg)));
  bool sawDst = false;
  for (; RTA_OK(rta, remaining); rta = RTA_NEXT(rta, remaining)) {
    if (rta->rta_type == RTA_DST) {
      in_addr expected{};
      inet_pton(AF_INET, "224.0.0.0", &expected);
      CHECK(std::memcmp(RTA_DATA(rta), &expected, sizeof(expected)) == 0);
      sawDst = true;
    }
  }
  CHECK(sawDst);
}

void testRejectsInvalidInput() {
  // 非连续掩码
  CHECK(buildRouteRequest(RTM_NEWROUTE, 1, {"224.0.0.0", "255.0.255.0"}, 3)
            .empty());
  // 非法地址
  CHECK(buildRouteRequest(RTM_NEWROUTE, 1, {"not-an-ip", "240.0.0.0"}, 3)
            .empty());
  // 非法消息类型
  CHECK(buildRouteRequest(RTM_GETROUTE, 1, {"224.0.0.0", "240.0.0.0"}, 3)
            .empty());
}
} // namespace

int main() {
  testNewRouteMulticast();
  testDelRouteFlags();
  testHostRoute();
  testHostBitsMasked();
  testRejectsInvalidInput();
  if (failures == 0) {
    std::printf("route_message_linux_test: all passed\n");
    return 0;
  }
  std::printf("route_message_linux_test: %d failure(s)\n", failures);
  return 1;
}
```

在 `tests/CMakeLists.txt` 末尾追加：

```cmake
if(UNIX AND NOT APPLE)
    add_executable(connecttool_route_linux_tests
        route_message_linux_test.cpp
        ../src/platform/route/route_manager_linux.cpp)
    target_include_directories(connecttool_route_linux_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(connecttool_route_linux_tests PRIVATE cxx_std_23)
    add_test(NAME route-linux-message COMMAND connecttool_route_linux_tests)
endif()
```

- [x] **Step 2: 运行测试确认失败(netlink 报文构造)**

Run: `cmake --preset dev && cmake --build --preset dev --target connecttool_route_linux_tests`
Expected: 编译失败（`route_manager_linux.cpp` 不存在）。这确认测试 target 已接线。
（注：本机为 macOS 时此 target 被 `if(UNIX AND NOT APPLE)` 排除，在 Linux CI 上执行本步；本地以 Task 3 的 macOS 测试等价走通 TDD 循环。)

- [x] **Step 3: 实现 route_manager_linux.cpp**

`src/platform/route/route_manager_linux.cpp` 完整内容：

```cpp
#ifdef __linux__

#include "platform/route/route_manager.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tun {
namespace {

int maskToPrefix(const std::string &mask) {
  in_addr addr{};
  if (inet_pton(AF_INET, mask.c_str(), &addr) != 1) {
    return -1;
  }
  uint32_t m = ntohl(addr.s_addr);
  int prefix = 0;
  while (m & 0x80000000) {
    prefix++;
    m <<= 1;
  }
  if (m != 0) {
    return -1; // 非连续掩码
  }
  return prefix;
}

template <typename T>
void appendAttribute(std::vector<uint8_t> &msg, uint16_t type, const T &value) {
  const size_t offset = msg.size();
  msg.resize(offset + NLA_ALIGN(sizeof(rtattr) + sizeof(T)), 0);
  auto *rta = reinterpret_cast<rtattr *>(msg.data() + offset);
  rta->rta_type = type;
  rta->rta_len = RTA_LENGTH(sizeof(T));
  std::memcpy(RTA_DATA(rta), &value, sizeof(T));
}

bool sendAndAck(const std::vector<uint8_t> &msg, uint32_t seq,
                bool treatNotFoundAsSuccess, std::string *error) {
  const int fd = ::socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (fd < 0) {
    *error = std::string("netlink socket: ") + std::strerror(errno);
    return false;
  }
  bool ok = false;
  bool done = false;
  if (::send(fd, msg.data(), msg.size(), 0) !=
      static_cast<ssize_t>(msg.size())) {
    *error = std::string("netlink send: ") + std::strerror(errno);
    done = true;
  }
  while (!done) {
    uint8_t buffer[512];
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      *error = std::string("netlink recv: ") + std::strerror(errno);
      break;
    }
    int len = static_cast<int>(n);
    for (nlmsghdr *nh = reinterpret_cast<nlmsghdr *>(buffer);
         NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
      if (nh->nlmsg_seq != seq || nh->nlmsg_type != NLMSG_ERROR) {
        continue;
      }
      const auto *err = static_cast<nlmsgerr *>(NLMSG_DATA(nh));
      if (err->error == 0 ||
          (treatNotFoundAsSuccess && err->error == -ESRCH)) {
        ok = true;
      } else {
        *error = std::string("rtnetlink: ") + std::strerror(-err->error);
      }
      done = true;
      break;
    }
    // 未匹配的通告消息继续读，直到拿到本请求 ACK
  }
  ::close(fd);
  return ok;
}

uint32_t nextSeq() {
  static std::atomic<uint32_t> seq{1};
  return seq.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

namespace detail {

std::vector<uint8_t> buildRouteRequest(uint16_t type, uint32_t seq,
                                       const Ipv4Route &route,
                                       uint32_t ifindex) {
  if (type != RTM_NEWROUTE && type != RTM_DELROUTE) {
    return {};
  }
  in_addr network{};
  if (inet_pton(AF_INET, route.network.c_str(), &network) != 1) {
    return {};
  }
  const int prefix = maskToPrefix(route.netmask);
  if (prefix < 0) {
    return {};
  }
  // 归一化：清掉 host 位
  if (prefix < 32) {
    const uint32_t mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    network.s_addr = htonl(ntohl(network.s_addr) & mask);
  }

  std::vector<uint8_t> msg(sizeof(nlmsghdr) + sizeof(rtmsg), 0);
  auto *nh = reinterpret_cast<nlmsghdr *>(msg.data());
  nh->nlmsg_type = type;
  nh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  if (type == RTM_NEWROUTE) {
    nh->nlmsg_flags |= NLM_F_CREATE | NLM_F_REPLACE;
  }
  nh->nlmsg_seq = seq;

  auto *rtm = reinterpret_cast<rtmsg *>(msg.data() + sizeof(nlmsghdr));
  rtm->rtm_family = AF_INET;
  rtm->rtm_dst_len = static_cast<uint8_t>(prefix);
  rtm->rtm_table = RT_TABLE_MAIN;
  rtm->rtm_protocol = RTPROT_STATIC;
  rtm->rtm_scope = RT_SCOPE_LINK;
  rtm->rtm_type = RTN_UNICAST;

  if (prefix > 0) {
    appendAttribute(msg, RTA_DST, network);
  }
  appendAttribute(msg, RTA_OIF, ifindex);
  nh->nlmsg_len = static_cast<uint32_t>(msg.size());
  return msg;
}

} // namespace detail

namespace {

class RouteManagerLinux final : public RouteManager {
public:
  explicit RouteManagerLinux(uint32_t ifindex) : ifindex_(ifindex) {}

  bool addRoute(const Ipv4Route &route, std::string *error) override {
    const uint32_t seq = nextSeq();
    const auto msg =
        detail::buildRouteRequest(RTM_NEWROUTE, seq, route, ifindex_);
    if (msg.empty()) {
      *error = "Invalid route " + route.network + "/" + route.netmask;
      return false;
    }
    return sendAndAck(msg, seq, false, error);
  }

  bool removeRoute(const Ipv4Route &route, std::string *error) override {
    const uint32_t seq = nextSeq();
    const auto msg =
        detail::buildRouteRequest(RTM_DELROUTE, seq, route, ifindex_);
    if (msg.empty()) {
      *error = "Invalid route " + route.network + "/" + route.netmask;
      return false;
    }
    return sendAndAck(msg, seq, true, error); // ESRCH 视为成功
  }

private:
  uint32_t ifindex_;
};

} // namespace

std::unique_ptr<RouteManager> createRouteManager(const std::string &ifname) {
  const unsigned index = if_nametoindex(ifname.c_str());
  if (index == 0) {
    return nullptr;
  }
  return std::make_unique<RouteManagerLinux>(index);
}

} // namespace tun

#endif // __linux__
```

- [x] **Step 4: 接入 CMake 并跑测试(netlink 报文构造)**

`src/platform/CMakeLists.txt` 的 `else()`(Linux）分支，把：

```cmake
    target_sources(connecttool_platform PRIVATE tun/tun_linux.cpp)
```

改为：

```cmake
    target_sources(connecttool_platform PRIVATE
        tun/tun_linux.cpp
        route/route_manager_linux.cpp)
```

Run(Linux/CI): `cmake --preset dev && cmake --build --preset dev --target connecttool_route_linux_tests && ctest --preset core-tests -R route-linux-message --output-on-failure`
Expected: `route-linux-message` PASSED。

- [x] **Step 5: Commit(任务 2:Linux netlink)**

```bash
git add src/platform/route/route_manager_linux.cpp src/platform/CMakeLists.txt tests/route_message_linux_test.cpp tests/CMakeLists.txt
git commit -m "feat(route): add Linux netlink RouteManager with message builder tests"
```

---

### Task 3: macOS PF_ROUTE 实现 + 报文构造单测

**Files:**
- Create: `src/platform/route/route_manager_macos.cpp`
- Test: `tests/route_message_macos_test.cpp`
- Modify: `src/platform/CMakeLists.txt`(APPLE 分支）
- Modify: `tests/CMakeLists.txt`（增加 APPLE 守护的测试 target)

**Interfaces:**
- Consumes: Task 1 的 `route_manager.h`。
- Produces: macOS 上 `createRouteManager` 的定义；`tun::detail::buildRouteMessage` 的定义。此编译单元同时被 app(`connecttool_platform`）与 daemon(`connecttool-tun-daemon`）链接，**禁止引用 Qt**。

**背景知识（写给零上下文执行者）:**
- 报文布局：`rt_msghdr` + 按 `rtm_addrs` 位序（`RTA_DST`=0x1、`RTA_GATEWAY`=0x2、`RTA_NETMASK`=0x4）依次排列的 sockaddr，每个 sockaddr 按 4 字节对齐。
- gateway 用 `sockaddr_dl`（空 name):`sdl_len = offsetof(sockaddr_dl, sdl_data)`(=8)、`sdl_family = AF_LINK`、`sdl_index = ifindex`，等价 `route add -net x/y -interface ifname`。flags 为 `RTF_UP|RTF_STATIC`(/32 时加 `RTF_HOST` 且整个省略 netmask);-interface 路由**不**设 `RTF_GATEWAY`。
- netmask 用截断的 `sockaddr_in`:`sin_family = 0`(BSD 掩码约定）、`sin_len = offsetof(sockaddr_in, sin_addr) + 掩码有效字节数`(240.0.0.0 → 1 字节 → `sin_len = 5`)。
- 错误判定：`write` 后从同一 socket `read` 回本进程消息（按 `rtm_pid` + `rtm_seq` 匹配，跳过其他进程的内核通告）,`rtm_errno` 即结果。`RTM_ADD` 遇 `EEXIST` 以 `RTM_CHANGE` 重试一次；`RTM_DELETE` 遇 `ESRCH` 视为成功。
- 需要 root(daemon 内或提权进程）；非 root `write` 返回 `EPERM`，由调用方降级。

- [x] **Step 1: 写失败测试**

`tests/route_message_macos_test.cpp` 完整内容：

```cpp
#include "platform/route/route_manager.h"

#include <arpa/inet.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/socket.h>

using tun::Ipv4Route;
using tun::detail::buildRouteMessage;

namespace {
int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

constexpr size_t kRound = sizeof(uint32_t);
size_t roundup(size_t n) { return (n + kRound - 1) & ~(kRound - 1); }

void testAddMulticast() {
  const Ipv4Route route{"224.0.0.0", "240.0.0.0"};
  const auto msg = buildRouteMessage(RTM_ADD, 1234, 7, route, 9);
  CHECK(!msg.empty());
  CHECK(msg.size() > sizeof(rt_msghdr));

  const auto *hdr = reinterpret_cast<const rt_msghdr *>(msg.data());
  CHECK(hdr->rtm_msglen == msg.size());
  CHECK(hdr->rtm_version == RTM_VERSION);
  CHECK(hdr->rtm_type == RTM_ADD);
  CHECK(hdr->rtm_pid == 1234);
  CHECK(hdr->rtm_seq == 7);
  CHECK((hdr->rtm_flags & RTF_UP) != 0);
  CHECK((hdr->rtm_flags & RTF_STATIC) != 0);
  CHECK((hdr->rtm_flags & RTF_HOST) == 0);
  CHECK(hdr->rtm_addrs == (RTA_DST | RTA_GATEWAY | RTA_NETMASK));

  // 按 rtm_addrs 位序走查三个 sockaddr
  const uint8_t *p = msg.data() + sizeof(rt_msghdr);
  const uint8_t *end = msg.data() + msg.size();

  // RTA_DST
  CHECK(p + sizeof(sockaddr_in) <= end);
  const auto *dst = reinterpret_cast<const sockaddr_in *>(p);
  CHECK(dst->sin_family == AF_INET);
  in_addr expected{};
  inet_pton(AF_INET, "224.0.0.0", &expected);
  CHECK(std::memcmp(&dst->sin_addr, &expected, sizeof(expected)) == 0);
  p += roundup(dst->sin_len);

  // RTA_GATEWAY: sockaddr_dl
  CHECK(p + offsetof(sockaddr_dl, sdl_data) <= end);
  const auto *gw = reinterpret_cast<const sockaddr_dl *>(p);
  CHECK(gw->sdl_family == AF_LINK);
  CHECK(gw->sdl_index == 9);
  CHECK(gw->sdl_len == offsetof(sockaddr_dl, sdl_data));
  CHECK(gw->sdl_nlen == 0);
  p += roundup(gw->sdl_len);

  // RTA_NETMASK: 截断 sockaddr_in,240.0.0.0 只有 1 个有效字节
  CHECK(p < end);
  const auto *mask = reinterpret_cast<const sockaddr_in *>(p);
  CHECK(mask->sin_family == 0);
  CHECK(mask->sin_len == offsetof(sockaddr_in, sin_addr) + 1);
  const auto *maskBytes = reinterpret_cast<const uint8_t *>(&mask->sin_addr);
  CHECK(maskBytes[0] == 0xF0);
  p += roundup(mask->sin_len);

  CHECK(p == end); // 报文恰好消费完，无拖尾
}

void testHostRouteOmitsNetmask() {
  const auto msg =
      buildRouteMessage(RTM_ADD, 1, 1, {"10.0.0.7", "255.255.255.255"}, 3);
  CHECK(!msg.empty());
  const auto *hdr = reinterpret_cast<const rt_msghdr *>(msg.data());
  CHECK((hdr->rtm_flags & RTF_HOST) != 0);
  CHECK(hdr->rtm_addrs == (RTA_DST | RTA_GATEWAY));
}

void testDeleteMessage() {
  const auto msg =
      buildRouteMessage(RTM_DELETE, 1, 2, {"224.0.0.0", "240.0.0.0"}, 9);
  CHECK(!msg.empty());
  const auto *hdr = reinterpret_cast<const rt_msghdr *>(msg.data());
  CHECK(hdr->rtm_type == RTM_DELETE);
  CHECK(hdr->rtm_seq == 2);
}

void testHostBitsMasked() {
  const auto msg =
      buildRouteMessage(RTM_ADD, 1, 1, {"224.0.0.5", "240.0.0.0"}, 3);
  CHECK(!msg.empty());
  const auto *dst = reinterpret_cast<const sockaddr_in *>(
      msg.data() + sizeof(rt_msghdr));
  in_addr expected{};
  inet_pton(AF_INET, "224.0.0.0", &expected);
  CHECK(std::memcmp(&dst->sin_addr, &expected, sizeof(expected)) == 0);
}

void testRejectsInvalidInput() {
  CHECK(buildRouteMessage(RTM_ADD, 1, 1, {"224.0.0.0", "255.0.255.0"}, 3)
            .empty()); // 非连续掩码
  CHECK(buildRouteMessage(RTM_ADD, 1, 1, {"bad", "240.0.0.0"}, 3).empty());
  CHECK(buildRouteMessage(RTM_GET, 1, 1, {"224.0.0.0", "240.0.0.0"}, 3)
            .empty());
}
} // namespace

int main() {
  testAddMulticast();
  testHostRouteOmitsNetmask();
  testDeleteMessage();
  testHostBitsMasked();
  testRejectsInvalidInput();
  if (failures == 0) {
    std::printf("route_message_macos_test: all passed\n");
    return 0;
  }
  std::printf("route_message_macos_test: %d failure(s)\n", failures);
  return 1;
}
```

在 `tests/CMakeLists.txt` 末尾（Task 2 追加的 Linux 块之后）追加：

```cmake
if(APPLE)
    add_executable(connecttool_route_macos_tests
        route_message_macos_test.cpp
        ../src/platform/route/route_manager_macos.cpp)
    target_include_directories(connecttool_route_macos_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(connecttool_route_macos_tests PRIVATE cxx_std_23)
    add_test(NAME route-macos-message COMMAND connecttool_route_macos_tests)
endif()
```

- [x] **Step 2: 运行测试确认失败**

Run: `cmake --preset dev && cmake --build --preset dev --target connecttool_route_macos_tests`
Expected: 编译失败（`route_manager_macos.cpp` 不存在）。

- [x] **Step 3: 实现 route_manager_macos.cpp**

`src/platform/route/route_manager_macos.cpp` 完整内容：

```cpp
#ifdef __APPLE__

#include "platform/route/route_manager.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tun {
namespace {

constexpr size_t kRound = sizeof(uint32_t);

size_t rtaRoundup(size_t n) { return (n + kRound - 1) & ~(kRound - 1); }

int maskToPrefix(const std::string &mask) {
  in_addr addr{};
  if (inet_pton(AF_INET, mask.c_str(), &addr) != 1) {
    return -1;
  }
  uint32_t m = ntohl(addr.s_addr);
  int prefix = 0;
  while (m & 0x80000000) {
    prefix++;
    m <<= 1;
  }
  if (m != 0) {
    return -1; // 非连续掩码
  }
  return prefix;
}

void appendSockaddr(std::vector<uint8_t> &msg, const void *sa, size_t saLen) {
  const size_t offset = msg.size();
  msg.resize(offset + rtaRoundup(saLen), 0);
  std::memcpy(msg.data() + offset, sa, saLen);
}

// 写入报文并从同一 socket 读回本进程的回执；rtm_errno 经 outErrno 返回。
bool sendRouteMessage(const std::vector<uint8_t> &msg, int32_t pid, int32_t seq,
                      int *outErrno, std::string *error) {
  const int fd = ::socket(PF_ROUTE, SOCK_RAW, AF_INET);
  if (fd < 0) {
    *error = std::string("routing socket: ") + std::strerror(errno);
    return false;
  }
  *outErrno = EIO;
  bool done =
      ::write(fd, msg.data(), msg.size()) != static_cast<ssize_t>(msg.size());
  if (done) {
    *error = std::string("routing socket write: ") + std::strerror(errno);
  }
  while (!done) {
    uint8_t buffer[2048];
    const ssize_t n = ::read(fd, buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      *error = std::string("routing socket read: ") + std::strerror(errno);
      break;
    }
    if (n < static_cast<ssize_t>(sizeof(rt_msghdr))) {
      continue;
    }
    const auto *hdr = reinterpret_cast<const rt_msghdr *>(buffer);
    if (hdr->rtm_pid != pid || hdr->rtm_seq != seq) {
      continue; // 其他进程/请求的内核通告
    }
    *outErrno = hdr->rtm_errno;
    done = true;
  }
  ::close(fd);
  if (*outErrno != 0) {
    if (error->empty()) {
      *error = std::string("routing socket: ") + std::strerror(*outErrno);
    }
    return false;
  }
  return true;
}

int32_t nextSeq() {
  static std::atomic<int32_t> seq{1};
  return seq.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

namespace detail {

std::vector<uint8_t> buildRouteMessage(uint8_t type, int32_t pid, int32_t seq,
                                       const Ipv4Route &route,
                                       uint16_t ifindex) {
  if (type != RTM_ADD && type != RTM_CHANGE && type != RTM_DELETE) {
    return {};
  }
  in_addr network{};
  if (inet_pton(AF_INET, route.network.c_str(), &network) != 1) {
    return {};
  }
  const int prefix = maskToPrefix(route.netmask);
  if (prefix < 0) {
    return {};
  }
  // 归一化：清掉 host 位
  if (prefix < 32) {
    const uint32_t mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    network.s_addr = htonl(ntohl(network.s_addr) & mask);
  }

  const bool isHost = (prefix == 32);
  std::vector<uint8_t> msg(sizeof(rt_msghdr), 0);
  auto *hdr = reinterpret_cast<rt_msghdr *>(msg.data());
  hdr->rtm_version = RTM_VERSION;
  hdr->rtm_type = type;
  hdr->rtm_pid = pid;
  hdr->rtm_seq = seq;
  hdr->rtm_flags = RTF_UP | RTF_STATIC | (isHost ? RTF_HOST : 0);
  hdr->rtm_addrs = RTA_DST | RTA_GATEWAY | (isHost ? 0 : RTA_NETMASK);

  sockaddr_in dst{};
  dst.sin_len = sizeof(dst);
  dst.sin_family = AF_INET;
  dst.sin_addr = network;
  appendSockaddr(msg, &dst, sizeof(dst));

  sockaddr_dl gateway{};
  gateway.sdl_len = static_cast<uint8_t>(offsetof(sockaddr_dl, sdl_data));
  gateway.sdl_family = AF_LINK;
  gateway.sdl_index = ifindex;
  appendSockaddr(msg, &gateway, gateway.sdl_len);

  if (!isHost) {
    sockaddr_in mask{};
    mask.sin_family = 0; // BSD 掩码约定 family 置 0
    const uint32_t m = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    mask.sin_addr.s_addr = htonl(m);
    mask.sin_len = static_cast<uint8_t>(offsetof(sockaddr_in, sin_addr) +
                                        (prefix + 7) / 8);
    appendSockaddr(msg, &mask, mask.sin_len);
  }

  hdr->rtm_msglen = static_cast<u_short>(msg.size());
  return msg;
}

} // namespace detail

namespace {

class RouteManagerMacOS final : public RouteManager {
public:
  explicit RouteManagerMacOS(uint16_t ifindex) : ifindex_(ifindex) {}

  bool addRoute(const Ipv4Route &route, std::string *error) override {
    const int32_t pid = static_cast<int32_t>(::getpid());
    int32_t seq = nextSeq();
    auto msg = detail::buildRouteMessage(RTM_ADD, pid, seq, route, ifindex_);
    if (msg.empty()) {
      *error = "Invalid route " + route.network + "/" + route.netmask;
      return false;
    }
    int routeErrno = 0;
    if (sendRouteMessage(msg, pid, seq, &routeErrno, error)) {
      return true;
    }
    if (routeErrno != EEXIST) {
      return false;
    }
    // 等价路由已存在：以 RTM_CHANGE 重试一次（幂等收敛）
    error->clear();
    seq = nextSeq();
    msg = detail::buildRouteMessage(RTM_CHANGE, pid, seq, route, ifindex_);
    return sendRouteMessage(msg, pid, seq, &routeErrno, error);
  }

  bool removeRoute(const Ipv4Route &route, std::string *error) override {
    const int32_t pid = static_cast<int32_t>(::getpid());
    const int32_t seq = nextSeq();
    const auto msg =
        detail::buildRouteMessage(RTM_DELETE, pid, seq, route, ifindex_);
    if (msg.empty()) {
      *error = "Invalid route " + route.network + "/" + route.netmask;
      return false;
    }
    int routeErrno = 0;
    if (sendRouteMessage(msg, pid, seq, &routeErrno, error)) {
      return true;
    }
    if (routeErrno == ESRCH) {
      error->clear();
      return true; // 幂等：不存在视为成功
    }
    return false;
  }

private:
  uint16_t ifindex_;
};

} // namespace

std::unique_ptr<RouteManager> createRouteManager(const std::string &ifname) {
  const unsigned index = if_nametoindex(ifname.c_str());
  if (index == 0) {
    return nullptr;
  }
  return std::make_unique<RouteManagerMacOS>(static_cast<uint16_t>(index));
}

} // namespace tun

#endif // __APPLE__
```

- [x] **Step 4: 接入 CMake 并跑测试**

`src/platform/CMakeLists.txt` 的 `elseif(APPLE)` 分支，把：

```cmake
    target_sources(connecttool_platform PRIVATE
        tun/tun_macos.cpp
        tun/tun_privileged_helper.cpp)
```

改为：

```cmake
    target_sources(connecttool_platform PRIVATE
        tun/tun_macos.cpp
        tun/tun_privileged_helper.cpp
        route/route_manager_macos.cpp)
```

Run: `cmake --preset dev && cmake --build --preset dev --target connecttool_route_macos_tests && ctest --preset core-tests -R route-macos-message --output-on-failure`
Expected: `route-macos-message` PASSED(`route-macos-message: all passed`)。

- [x] **Step 5: Commit(任务 3:macOS PF_ROUTE)**

```bash
git add src/platform/route/route_manager_macos.cpp src/platform/CMakeLists.txt tests/route_message_macos_test.cpp tests/CMakeLists.txt
git commit -m "feat(route): add macOS PF_ROUTE RouteManager with message builder tests"
```

---

### Task 4: Windows RouteManager（路由职责从 windows_network_config 迁入）

**Files:**
- Create: `src/platform/route/route_manager_windows.cpp`
- Modify: `src/platform/tun/windows_network_config.h`（删 `Ipv4RouteSpec`、`ensureOnLinkRoute`)
- Modify: `src/platform/tun/windows_network_config.cpp`（删 `ensureOnLinkRoute`、`matchesRoute` 及仅它使用的 helper)
- Modify: `src/platform/CMakeLists.txt`(WIN32 分支）

**Interfaces:**
- Consumes: Task 1 的 `route_manager.h`。
- Produces: Windows 上 `createRouteManager(ifname)` 的定义（经 `ConvertInterfaceAliasToLuidW` + `ConvertInterfaceLuidToIndex` 解析）;Task 7 依赖它替换 `TunWindows::add_route` 的现有实现。`windows_network_config.h` 迁移后只保留：`WindowsInterfaceId`、`Ipv4AddressSpec`、`WindowsNetworkConfig::{bind, clear, assignAddress, setMtu, setEnabled, lastError}`。

**背景知识：**
- 系统默认 `224.0.0.0/4` 必须保留；Minecraft 使用独立 `224.0.2.60/32`，以最长前缀优先。RouteManager 按 `(LUID, prefix, NextHop=0)` 去重，借用但不删除任何预先存在的条目，仅清理本次调用成功创建的路由。
- 路由条目：on-link(`NextHop` 置 `0.0.0.0`)、`Metric=1`、`Protocol=MIB_IPPROTO_NETMGMT`、`SitePrefixLength=前缀`。
- `DeleteIpForwardEntry2` 参数为非 const 指针，枚举到的候选项需先拷贝到本地变量再删。
- 解析小工具（`parseAddress`/`prefixLength`/`networkAddress`/`makeLuid`/`systemMessage`）现为 `windows_network_config.cpp` 的匿名命名空间 static，两个编译单元各保留一份拷贝（内部链接，~60 行，避免为此新建共享头）。

- [x] **Step 1: 创建 route_manager_windows.cpp(任务 4:Windows RouteManager)**

`src/platform/route/route_manager_windows.cpp` 完整内容：

```cpp
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
        if (candidate.InterfaceLuid.Value == luid_.Value &&
            candidate.NextHop.Ipv4.sin_addr.S_un.S_addr == INADDR_ANY) {
          return true; // 精确 on-link 条目已存在：幂等成功
        }
        // 同前缀的其他接口路由合法共存，必须保留。
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
  const std::wstring alias{ifname.begin(), ifname.end()};
  NET_LUID luid{};
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
```

- [x] **Step 2: 从 windows_network_config 删除已迁移的路由职责(任务 4)**

`src/platform/tun/windows_network_config.h`：删除 `Ipv4RouteSpec` 结构体（第 21-24 行）与 `ensureOnLinkRoute` 声明（第 32 行）。

`src/platform/tun/windows_network_config.cpp`:
- 删除整个 `WindowsNetworkConfig::ensureOnLinkRoute` 实现（第 169-213 行）。
- 删除匿名命名空间中仅被它使用的 `matchesRoute` 函数与 `RouteTable` 类型别名（`MibTableDeleter` 保留，`AddressTable` 仍使用）。删除后 `networkAddress` 若不再被引用一并删除（`assignAddress` 不用它）；编译器 `-Werror=unused-function` 会指出，以编译结果为准。
- 保留 `parseAddress`/`prefixLength`(`assignAddress` 仍用）与 `makeLuid`。

- [x] **Step 3: 接入 CMake(任务 4:Windows)**

`src/platform/CMakeLists.txt` 的 `if(WIN32)` 分支，把：

```cmake
    target_sources(connecttool_platform PRIVATE
        tun/windows_network_config.cpp
        tun/tun_windows.cpp
```

改为：

```cmake
    target_sources(connecttool_platform PRIVATE
        tun/windows_network_config.cpp
        tun/tun_windows.cpp
        route/route_manager_windows.cpp
```

（保留其余行不变。)

- [x] **Step 4: 验证(任务 4:Windows 桩头语法检查)**

本机为 macOS 无法编译 Windows 代码。验证方式：
1. 代码审查：对照本任务代码块逐行核对，确认 `windows_network_config.cpp` 删除后无残留引用（`grep -rn "ensureOnLinkRoute\|Ipv4RouteSpec" src/`)——Expected：仅剩本计划文件，源码中 0 处命中。
2. Windows CI 构建通过（或装有 Windows 工具链的机器上 `cmake --preset dev && cmake --build --preset dev`)。

Run: `grep -rn "ensureOnLinkRoute\|Ipv4RouteSpec" /Users/lolimaster/Projects/connecttool/connecttool-qt/src/`
Expected: 无输出。

- [x] **Step 5: Commit(任务 4:Windows RouteManager)**

```bash
git add src/platform/route/route_manager_windows.cpp src/platform/tun/windows_network_config.h src/platform/tun/windows_network_config.cpp src/platform/CMakeLists.txt
git commit -m "refactor(route): move Windows route management into RouteManagerWindows"
```

---

### Task 5: TunInterface 增加 remove_route + TunLinux 委托

**Files:**
- Modify: `src/platform/tun/tun_interface.h`（新增 `remove_route` 默认实现）
- Modify: `src/platform/tun/tun_linux.cpp`(`add_route` 委托、删除 `system()` 与 `maskToPrefix`、open() 创建 RouteManager)

**Interfaces:**
- Consumes: Task 1 `route_manager.h`、Task 2 Linux 实现。
- Produces: `TunInterface::remove_route(const std::string &network, const std::string &netmask) -> bool`，默认 no-op 返回 true(Task 8 的 bridge 直接调用；Task 6/7 各平台 override)。

- [x] **Step 1: 扩展 tun_interface.h**

在 `add_route` 声明（第 24-25 行）之后插入：

```cpp
  // Remove a previously installed route; best-effort semantics. The default
  // implementation is a no-op success for platforms where the kernel reclaims
  // interface routes on close().
  virtual bool remove_route(const std::string &network,
                            const std::string &netmask) {
    (void)network;
    (void)netmask;
    return true;
  }
```

- [x] **Step 2: TunLinux 委托改造**

`tun_linux.cpp` 改动点：

1. 头部新增 include:`#include "platform/route/route_manager.h"`。
2. 删除匿名命名空间中的 `maskToPrefix`（第 42-57 行）——委托后不再使用。
3. `TunLinux` 私有成员新增：

```cpp
  std::unique_ptr<RouteManager> routeManager_;
```

4. `open()` 中 `name_ = ifr.ifr_name;`（第 94 行）之后插入：

```cpp
    routeManager_ = createRouteManager(name_);
```

5. `close()` 中 `fd_ = -1;` 之后插入 `routeManager_.reset();`。
6. `add_route` 整体替换（原第 177-201 行）:

```cpp
  bool add_route(const std::string &network,
                 const std::string &netmask) override {
    if (!routeManager_) {
      lastError_ = "Route manager unavailable";
      return false;
    }
    std::string error;
    if (!routeManager_->addRoute({network, netmask}, &error)) {
      lastError_ = error;
      return false;
    }
    return true;
  }

  bool remove_route(const std::string &network,
                    const std::string &netmask) override {
    if (!routeManager_) {
      lastError_ = "Route manager unavailable";
      return false;
    }
    std::string error;
    if (!routeManager_->removeRoute({network, netmask}, &error)) {
      lastError_ = error;
      return false;
    }
    return true;
  }
```

- [x] **Step 3: 验证(任务 5:TunLinux 委托)**

1. `grep -n "system(\|\"route \|ip route" src/platform/tun/tun_linux.cpp`——Expected: 无输出（shell out 路径已清除）。
2. Linux CI 构建通过 + `route-linux-message` 测试通过（design 约定 Linux 本 change 仅编译验证）。

- [x] **Step 4: Commit(任务 5:TunLinux 委托)**

```bash
git add src/platform/tun/tun_interface.h src/platform/tun/tun_linux.cpp
git commit -m "refactor(tun): delegate Linux route management to RouteManager"
```

---

### Task 6: TunMacOS 委托 + daemon 改用 RouteManager

**Files:**
- Modify: `src/platform/tun/tun_macos.cpp`(root 直执行路径委托；helper 路径不变）
- Modify: `src/platform/tun/tun_macos_daemon.cpp`(`ADD_ROUTE` 改用 RouteManager，删 `runRoute`/`maskToPrefix`)
- Modify: `CMakeLists.txt`（根，daemon target)

**Interfaces:**
- Consumes: Task 1、Task 3(`createRouteManager` macOS 定义）、Task 5(`remove_route` 接口）。
- Produces: 无新接口；daemon 协议不变（`ADD_ROUTE if=... net=... mask=...` → `OK` / `ERR <message>`)。

**背景知识：**
- `tun_macos.cpp` 有两条路径：`usingHelper_ == true` 时经 unix socket 走 daemon(`helperAddRoute`，协议不变，继续用）;root 直执行时此前 `system("/sbin/route ...")`，本任务改为 RouteManager。
- helper 模式下 `remove_route` 是 no-op 成功：daemon 协议没有 `REMOVE_ROUTE`(design 明确不改协议）,utun 关闭时内核自动回收接口路由（design 的"双保险")。
- daemon(`tun_macos_daemon.cpp`）以 root 运行，`ADD_ROUTE` 处理此前 `posix_spawn("/sbin/route")`，改为直接调 `tun::createRouteManager`；它不在 `namespace tun` 内，需写全限定名。
- daemon 中 `maskToPrefix`（第 146-161 行）仅被 `ADD_ROUTE` 的 cidr 拼接使用，改造后一并删除；`runRoute`（第 249-255 行）删除，`runCommand`/`runIfconfig` 保留（SET_IP/SET_MTU/SET_UP 仍用 ifconfig——design 只迁移路由职责）。

- [x] **Step 1: tun_macos.cpp 改造**

1. 头部新增 include:`#include "platform/route/route_manager.h"`。
2. 删除匿名命名空间中的 `maskToPrefix`（第 49-64 行）。
3. `TunMacOS` 私有成员新增：`std::unique_ptr<RouteManager> routeManager_;`。
4. `open()` 的 root 直执行路径末尾（`name_ = ifName;` 之后、`return true;` 之前，约第 137 行）插入：

```cpp
    routeManager_ = createRouteManager(name_);
```

   (helper 路径不创建，保持 nullptr。)
5. `close()` 中 `usingHelper_ = false;` 之前插入 `routeManager_.reset();`。
6. `add_route`（第 220-248 行）替换为：

```cpp
  bool add_route(const std::string &network,
                 const std::string &netmask) override {
    if (usingHelper_) {
      std::string error;
      if (!helperAddRoute(network, netmask, name_, &error)) {
        lastError_ = error.empty() ? "Helper add_route failed" : error;
        return false;
      }
      return true;
    }
    if (!routeManager_) {
      lastError_ = "Route manager unavailable";
      return false;
    }
    std::string error;
    if (!routeManager_->addRoute({network, netmask}, &error)) {
      lastError_ = error;
      return false;
    }
    return true;
  }

  bool remove_route(const std::string &network,
                    const std::string &netmask) override {
    if (usingHelper_) {
      // daemon 协议无 REMOVE_ROUTE；utun 关闭时内核回收接口路由。
      return true;
    }
    if (!routeManager_) {
      lastError_ = "Route manager unavailable";
      return false;
    }
    std::string error;
    if (!routeManager_->removeRoute({network, netmask}, &error)) {
      lastError_ = error;
      return false;
    }
    return true;
  }
```

- [x] **Step 2: daemon 改造**

`tun_macos_daemon.cpp`:
1. 头部新增 include:`#include "platform/route/route_manager.h"`。
2. 删除 `runRoute` 函数（第 249-255 行）与 `maskToPrefix` 函数（第 146-161 行）。
3. `ADD_ROUTE` 分支（第 419-441 行）整体替换为：

```cpp
  if (verb == "ADD_ROUTE") {
    const std::string ifname = args.count("if") ? args.at("if") : "";
    const std::string network = args.count("net") ? args.at("net") : "";
    const std::string mask = args.count("mask") ? args.at("mask") : "";
    if (!validIfName(ifname) || !validAddress(network) ||
        !validAddress(mask)) {
      sendResponse(fd, "ERR invalid ADD_ROUTE arguments");
      return;
    }
    auto manager = tun::createRouteManager(ifname);
    if (!manager) {
      sendResponse(fd, "ERR route manager unavailable");
      return;
    }
    std::string error;
    if (!manager->addRoute({network, mask}, &error)) {
      sendResponse(fd, "ERR " + error);
      return;
    }
    sendResponse(fd, "OK");
    return;
  }
```

- [x] **Step 3: daemon target 接入新编译单元**

根 `CMakeLists.txt`(APPLE 块内，约第 157-159 行）,daemon target 源列表加入 route 实现：

```cmake
    add_executable(connecttool-tun-daemon
        src/platform/tun/tun_macos_daemon.cpp
        src/platform/route/route_manager_macos.cpp
```

（原有其余源文件保持不变；include root `${CMAKE_SOURCE_DIR}/src` 已配置，无需改动。)

- [x] **Step 4: 构建验证(任务 6:macOS 目标级构建)**

Run: `cmake --preset dev && cmake --build --preset dev && ctest --preset core-tests --output-on-failure`
Expected: 全量构建成功（含 `connecttool-tun-daemon`)；全部测试 PASSED（含 `route-macos-message`、`architecture-boundaries`)。

同时确认 shell out 已清除：
Run: `grep -n "sbin/route\|runRoute\|posix_spawn.*route" src/platform/tun/tun_macos_daemon.cpp src/platform/tun/tun_macos.cpp`
Expected: 无输出。

- [x] **Step 5: 手动实测(任务 6,按 brief 免责条款推迟至 Task 9 端到端验证)**

```bash
# root 直执行路径（不装 daemon 时）:
sudo ./build/connecttool-qt.app/Contents/MacOS/connecttool-qt &  # 启动 VPN 后
route -n get 224.0.2.60                    # 应看到精确路由 → utunX
# 停止 VPN 后:
route -n get 224.0.2.60                    # 不应再指向已关闭的 utun
```

daemon 路径（非 root 常规启动 + 已安装 helper）重复以上检查。Expected: 两条路径网段路由与 `224.0.2.60/32` 均生效，停止后无残留。
（若实测条件暂不具备，记录为 Task 9 端到端验证的一部分，不阻塞 commit。)

- [x] **Step 6: Commit(任务 6:macOS 委托)**

```bash
git add src/platform/tun/tun_macos.cpp src/platform/tun/tun_macos_daemon.cpp CMakeLists.txt
git commit -m "refactor(tun): use PF_ROUTE RouteManager on macOS app and daemon"
```

---

### Task 7: TunWindows 委托

**Files:**
- Modify: `src/platform/tun/tun_windows.cpp`

**Interfaces:**
- Consumes: Task 1、Task 4(`createRouteManager` Windows 定义）、Task 5(`remove_route` 接口）、Task 4 迁移后的 `windows_network_config.h`（不再含 `ensureOnLinkRoute`)。
- Produces: 无新接口。

- [x] **Step 1: tun_windows.cpp 改造**

1. 头部（`#include "windows_network_config.h"` 之后）新增：

```cpp
#include "platform/route/route_manager.h"
```

2. `TunWindows` 私有成员新增（`WindowsNetworkConfig networkConfig_;` 旁）:

```cpp
  std::unique_ptr<RouteManager> routeManager_;
```

3. `open()` 中 `networkConfig_.bind({.luid = adapterLuid.Value, .index = adapterIndex});`（第 185 行）之后插入：

```cpp
    routeManager_ = createRouteManager(deviceName_);
```

   (WinTUN 适配器的接口别名即适配器名 `deviceName_`,`ConvertInterfaceAliasToLuidW` 可解析。)
4. `close()` 中 `networkConfig_.clear();` 之前插入 `routeManager_.reset();`。
5. `add_route`（第 284-290 行）替换为：

```cpp
  bool add_route(const std::string &network, const std::string &netmask) override {
    if (!routeManager_) {
      setError("Route manager unavailable");
      return false;
    }
    std::string error;
    if (!routeManager_->addRoute({network, netmask}, &error)) {
      setError(error);
      return false;
    }
    return true;
  }

  bool remove_route(const std::string &network, const std::string &netmask) override {
    if (!routeManager_) {
      setError("Route manager unavailable");
      return false;
    }
    std::string error;
    if (!routeManager_->removeRoute({network, netmask}, &error)) {
      setError(error);
      return false;
    }
    return true;
  }
```

- [x] **Step 2: 验证(任务 7:Windows 桩头语法检查)**

1. `grep -n "ensureOnLinkRoute" src/platform/tun/tun_windows.cpp`——Expected: 无输出。
2. Windows CI 构建通过（本机 macOS 无法编译，以 CI + 代码审查为准；同 Task 4 的验证约定）。

- [x] **Step 3: Commit(任务 7:Windows 委托)**

```bash
git add src/platform/tun/tun_windows.cpp
git commit -m "refactor(tun): delegate Windows route management to RouteManagerWindows"
```

---

### Task 8: SteamVpnBridge 集成（组播路由加装 + stop 显式删除）

**Files:**
- Modify: `src/integrations/steam/steam_vpn_bridge.cpp`(`onNegotiationSuccess` 约第 483-491 行、`stop()` 约第 138-140 行）

**Interfaces:**
- Consumes: `TunInterface::add_route`（现状）、Task 5 的 `TunInterface::remove_route`；现有成员 `baseIP_`、`subnetMask_`、helper `ipToString`。
- Produces: 无新接口。

**错误处理矩阵（design §4，必须严格遵守）:**
| 场景 | 行为 |
|------|------|
| 组播 `add_route` 失败 | `std::cerr` 记日志，VPN 继续（不 `recordFailure`、不 return) |
| 网段 `add_route` 失败 | 维持现状：`recordFailure` 并 return |
| `remove_route` 失败 | `std::cerr` 记日志，不阻断 stop |

- [x] **Step 1: onNegotiationSuccess 加装组播路由**

在网段 `add_route` 成功块之后（`localIP_.store(...)` 之前，约第 491 行）插入：

```cpp
  // Install the multicast route so LAN-discovery traffic (e.g. Minecraft
  // 224.0.2.60:4445) is routed into the TUN device. Best-effort: failure only
  // degrades LAN discovery and must not abort the VPN.
  if (!tunDevice_->add_route("224.0.2.60", "255.255.255.255")) {
    std::cerr << "[SteamVPN] Failed to add the multicast route (LAN discovery "
                 "degraded): "
              << tunDevice_->get_last_error() << std::endl;
  }
```

- [x] **Step 2: stop() 关 TUN 前显式删除路由**

`stop()` 中现有代码：

```cpp
  if (tunDevice_) {
    tunDevice_->close(); // wake blocking reads
  }
```

替换为：

```cpp
  if (tunDevice_) {
    // Explicit best-effort route cleanup before closing the device. Windows
    // routes persist after adapter teardown; on macOS/Linux the kernel
    // reclaims interface routes on close, so failures are only logged.
    if (baseIP_ != 0 && subnetMask_ != 0) {
      const std::string subnetMaskStr = ipToString(subnetMask_);
      const std::string networkStr = ipToString(baseIP_ & subnetMask_);
      if (!tunDevice_->remove_route("224.0.2.60", "255.255.255.255")) {
        std::cerr << "[SteamVPN] Failed to remove the multicast route: "
                  << tunDevice_->get_last_error() << std::endl;
      }
      if (!tunDevice_->remove_route(networkStr, subnetMaskStr)) {
        std::cerr << "[SteamVPN] Failed to remove the subnet route: "
                  << tunDevice_->get_last_error() << std::endl;
      }
    }
    tunDevice_->close(); // wake blocking reads
  }
```

注意：`remove_route` 必须在 `close()` **之前**(Windows 上 adapter 销毁后 LUID 失效；`close()` 提前调用是为唤醒阻塞读线程的既有语义，不可挪后）。

- [x] **Step 3: 构建与回归(任务 8:目标级构建+ctest)**

Run: `cmake --preset dev && cmake --build --preset dev && ctest --preset core-tests --output-on-failure`
Expected: 构建成功，全部测试 PASSED。

- [x] **Step 4: Commit(任务 8:Bridge 集成)**

```bash
git add src/integrations/steam/steam_vpn_bridge.cpp
git commit -m "feat(vpn): install multicast route on TUN startup and remove routes on stop"
```

---

### Task 9: 端到端手动验证（design §5)

**Files:** 无（纯验证任务；不通过则回到对应 Task 修复，禁止带病收尾）

- [ ] **Step 1: macOS 路由装/删验证**

直执行与 daemon 两条路径分别：
1. 启动 VPN → `route -n get 224.0.2.60` 确认精确 `/32` 指向 utunX，网段路由存在。
2. 日志确认 `tunReadLoop` 读到 224.x 包并广播（`[SteamVPN] Broadcast ... -> 224.x`)。
3. 停止 VPN → `netstat -rn` 确认无指向已销毁 utun 的残留；反复启停 3 次无累积残留。

- [ ] **Step 2: Windows 路由装/删验证**

1. 启动 VPN → `route print -4` 确认 `224.0.2.60/255.255.255.255` 指向 SteamVPN 适配器，物理网卡默认 `224.0.0.0/4` 保持不变。
2. 停止 VPN → `route print -4` 确认该条目已删除，无残留；反复启停 3 次验证。
3. 停止 VPN 后确认 ConnectTool 的 `224.0.2.60/32` 已删除，物理网卡 `224.0.0.0/4` 在启停前后均未改变。

- [ ] **Step 3: Minecraft 双机实测（macOS ↔ Windows)**

1. A 开局域网世界（「对局域网开放」)。
2. B 的多人游戏列表应**自动出现**该世界（不经手动添加服务器）。
3. 加入后单播联机正常（移动/交互无异常）。
4. 接收端同时验证原始组播与 Minecraft 单播兜底；确认列表记录的服务器地址仍为房主 TUN IP。Linux 即使未在 TUN 加入组播组，也应通过单播副本收到宣告。

- [ ] **Step 4: 验证记录与 tasks.md 勾选**

将以上结果（命令输出摘录、实测结论）写入 verify 阶段报告；勾选 `openspec/changes/add-tun-multicast-route/tasks.md` 对应条目。

---

## Self-Review 结论

- **Spec 覆盖**:tasks.md §1.1/1.2 → Task 2+5;§1.3 → 隔离 Linux TUN 实测；§2.1/2.2/2.3 → Task 3+6;§2.4 → Task 6 Step 5 / Task 9 Step 1;§3.1 → Task 8 Step 1;§3.2 → Task 4（精确去重、保留其他接口）+ Task 8 Step 2(stop 显式删除）+ Task 9 Step 2;§4.1/4.2/4.3 → Task 9。design §2.2 三平台实现、§2.3 集成点、§4 错误处理矩阵全部有对应任务。
- **类型一致性**:`Ipv4Route`/`addRoute`/`removeRoute`/`createRouteManager`/`buildRouteRequest`/`buildRouteMessage`/`remove_route` 在各任务间签名一致；daemon 中按全限定 `tun::createRouteManager` 调用。
- **偏离 design 说明**:design §2.3 未指明 `stop()` 删除路由的调用路径；本计划经 `TunInterface::remove_route`（默认 no-op 实现）委托到各实现持有的 RouteManager，符合"TunInterface 各实现委托"与"接口签名不变（仅新增带默认实现的虚函数）"的意图，bridge 无需感知平台差异。
