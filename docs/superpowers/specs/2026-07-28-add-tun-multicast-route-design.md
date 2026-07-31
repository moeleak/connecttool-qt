---
comet_change: add-tun-multicast-route
role: technical-design
canonical_spec: openspec
---

# 技术设计：TUN 组播路由（add-tun-multicast-route)

日期：2026-07-28

## 1. 背景与问题

TUN 模式下游戏局域网发现不可用。Minecraft「对局域网开放」通过组播 `224.0.2.60:4445` 宣告房间，但 TUN 只安装了虚拟网段路由（`10.0.0.0/8`)，组播包被 OS 路由到物理网卡，从不进入 TUN 设备。桥接层（`SteamVpnBridge`）的组播转发逻辑（`isBroadcastAddress` → `broadcastMessage`）已存在且覆盖 224–239 组播段，缺的只是入口路由。

用户要求：路由安装一律使用系统 API，不 shell out 外部命令。

## 2. 方案总览

引入跨平台 **RouteManager 抽象**，承载三平台的路由增删；VPN 启动时为 TUN 加装 Minecraft `224.0.2.60/32` 主机路由，停止时显式删除。

```
src/platform/route/
├── route_manager.h           # Ipv4Route / RouteManager 抽象 / createRouteManager 工厂
├── route_manager_linux.cpp   # NETLINK_ROUTE
├── route_manager_macos.cpp   # PF_ROUTE routing socket
└── route_manager_windows.cpp # CreateIpForwardEntry2 / DeleteIpForwardEntry2
```

### 2.1 接口

```cpp
struct Ipv4Route {
  std::string network;  // 例: "224.0.0.0"
  std::string netmask;  // 例: "240.0.0.0"
};

class RouteManager {
public:
  virtual ~RouteManager() = default;
  virtual bool addRoute(const Ipv4Route &route, std::string *error) = 0;
  virtual bool removeRoute(const Ipv4Route &route, std::string *error) = 0;
};

// 绑定接口（解析 ifindex；Windows 解析 LUID/index）。失败返回 nullptr。
std::unique_ptr<RouteManager> createRouteManager(const std::string &ifname);
```

设计要点：

- **幂等语义**:`addRoute` 对已存在的等价路由视为成功（replace/change 语义）;`removeRoute` 对不存在的路由视为成功（`ESRCH` 吞掉）
- **无异常**：返回 `bool + error string`
- **可 mock**:bridge 层单测可注入 fake RouteManager

### 2.2 平台实现

**Linux(`route_manager_linux.cpp`)**

- `socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE)`，每次调用一开一关（无状态，线程安全）
- `addRoute`:`RTM_NEWROUTE`,`nlmsg_flags = NLM_F_REQUEST|NLM_F_ACK|NLM_F_CREATE|NLM_F_REPLACE`;`rtmsg`:`rtm_family=AF_INET`、`rtm_dst_len=prefix`、`rtm_table=RT_TABLE_MAIN`、`rtm_protocol=RTPROT_STATIC`、`rtm_scope=RT_SCOPE_LINK`、`rtm_type=RTN_UNICAST`;attributes:`RTA_DST`(网络地址）、`RTA_OIF`(`if_nametoindex`)。读 `NLMSG_ERROR` ack,`error==0` 成功
- `removeRoute`:`RTM_DELROUTE` 同结构；ack `-ESRCH` 视为成功
- 消息构造抽成纯函数（`buildRouteRequest(...)`）便于单测

**macOS(`route_manager_macos.cpp`)**

- `socket(PF_ROUTE, SOCK_RAW, AF_INET)`，每次调用一开一关
- `addRoute`:`RTM_ADD`,`rtm_flags=RTF_UP|RTF_STATIC`(/32 时 `RTF_HOST` 且省 netmask),`rtm_addrs=RTA_DST|RTA_GATEWAY|RTA_NETMASK`;gateway 为 `sockaddr_dl`(`sdl_family=AF_LINK`、`sdl_index=ifindex`、`sdl_len` 按空 name)——等价于 `route add -net x/y -interface ifname`。写后从 socket 读回本进程消息判定 errno;`EEXIST` 时以 `RTM_CHANGE` 重试一次
- `removeRoute`:`RTM_DELETE` 同结构；`ESRCH` 视为成功
- 需要 root(daemon 内或提权进程）；非 root 返回错误，由调用方降级
- 消息布局构造抽成纯函数便于单测

**Windows(`route_manager_windows.cpp`)**

- 从 `windows_network_config.cpp` 迁入路由增删职责（地址分配、MTU、防火墙规则保留原处）,`WindowsNetworkConfig::ensureOnLinkRoute` 删除，`TunWindows::add_route` 改为委托 RouteManager
- `addRoute`：枚举 `GetIpForwardTable2`，按 `(LUID, prefix, NextHop=0)` 精确匹配；保留其他接口与所有预先存在的路由，仅将本次 `CreateIpForwardEntry2` 成功创建的条目标为 owned 并在停止时删除
- `removeRoute`:`DeleteIpForwardEntry2`;`ERROR_NOT_FOUND` 视为成功

### 2.3 集成点

**TunInterface 各实现**

- 持有一个 `RouteManager` 实例（`createRouteManager(get_device_name())`，在 `open()` 成功后创建）;`add_route()` 委托之，接口签名不变
- macOS daemon(`tun_macos_daemon.cpp`）链接 `route_manager_macos.cpp`;`ADD_ROUTE` 命令协议不变，内部实现从 fork `/sbin/route` 改为 `RouteManager::addRoute`

**SteamVpnBridge(`steam_vpn_bridge.cpp`)**

- `onNegotiationSuccess`：网段 `add_route` 成功后，加装 `{224.0.2.60, 255.255.255.255}`；失败仅 `std::cerr` 记日志，不调用 `recordFailure`，不中断启动
- `stop()`：关 TUN 前显式 `removeRoute` 组播路由与网段路由（best-effort，失败仅日志）;macOS/Linux 与接口关闭自动回收构成双保险，Windows 依赖显式删除

## 3. 数据流

```
应用(游戏) ─224.0.2.60─► OS 路由表 ─224.0.2.60/32→TUN─► tunReadLoop
   │                                                        │ isBroadcastAddress → broadcastMessage
   │                                                        ▼
   │                                            Steam P2P ─► 所有 peer
   │                                                        ▼
   │                              peer: handleVpnMessage → 写入本地 TUN → 内核投递
   ▼
onNegotiationSuccess: add_route(网段) → add_route(224.0.2.60/32, best-effort)
stop(): removeRoute(组播) + removeRoute(网段) → close()
```

## 4. 错误处理

| 场景 | 行为 |
|------|------|
| 组播 `add_route` 失败 | 记日志，VPN 继续运行（增强能力降级） |
| 网段 `add_route` 失败 | 维持现状：`recordFailure`，中止启动 |
| daemon `ADD_ROUTE` 失败 | 回 `ERR <message>`（协议不变） |
| `createRouteManager` 返回 nullptr | `add_route` 返回 false + error；组播路径降级 |
| `removeRoute` 失败 | 记日志，不阻断停止流程 |

## 5. 测试策略

- **单测**(tests/):netlink/rt_msghdr 消息构造纯函数的序列化正确性（无需 root)；前缀/掩码换算边界（/32、/4、非对齐掩码拒绝）
- **macOS 实测**:`netstat -rn | grep 224` 验证装/删；直执行与 daemon 两条路径；反复启停查残留
- **Windows 实测**:`route print` 验证；与系统默认组播条目共存/接管；adapter 关闭后无残留
- **端到端**:macOS↔Windows 双端 Minecraft,A 开局域网世界 → B 多人列表自动出现；日志确认 `tunReadLoop` 读到 224.x 包并转发
- **Linux**：隔离容器中使用生产 RouteManager + 真实 TUN 验证 `/32` 路由、wildcard UDP 发送、校验和与清理

## 6. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 接收端内核不投递组播给仅在默认接口 join 组的游戏 socket | 对 Minecraft `224.0.2.60:4445` 同时注入源为认证 peer TUN IP 的本地 TUN-IP 单播副本，并重算校验和；原组播路径继续保留 |
| 无关组播被转发或本地发现中断 | 仅路由 Minecraft `224.0.2.60/32`;mDNS/SSDP 等继续走物理网卡 |
| routing socket/netlink 实现缺陷 | 消息构造单测 + 双平台实测；组播路径失败仅降级不影响单播 |
| Windows 默认 `/4` 组播路由冲突 | `/32` 通过最长前缀优先，不删除或修改物理路由 |
| macOS 非 root 直执行 EPERM | 与现状一致，daemon 路径不受影响 |

## 7. 范围边界（Non-Goals)

不改 VPN 封包协议与转发逻辑；不做 IGMP；不处理 255.255.255.255 有限广播；不加配置开关；不改 daemon 权限模型与 socket 协议；不做接收端中继兜底。
