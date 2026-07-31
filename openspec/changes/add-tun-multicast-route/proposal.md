# Proposal: add-tun-multicast-route

## Why

TUN 模式下游戏的局域网发现不可用：以 Minecraft 为例，「对局域网开放」后通过组播 `224.0.2.60:4445` 宣告房间，但 TUN 只安装了虚拟网段（`10.0.0.0/8`）路由，组播包被 OS 路由到物理网卡而非 TUN 设备，永远不会被转发给 peer。这导致双方都连接在 VPN 下时互相搜索不到局域网世界，只能手动输入 IP 直连。

## What Changes

- VPN 启动（IP 协商成功）后，在三个平台（macOS / Linux / Windows）为 TUN 设备额外安装 Minecraft 发现主机路由（`224.0.2.60/32 → TUN`），使本机发出的宣告进入 TUN 设备
- 复用 `steam_vpn_bridge.cpp` 中已有的组播/广播转发逻辑（`isBroadcastAddress` 已覆盖 224–239 组播段），组播包进入 TUN 后自动被转发至所有 peer，无需改动封包协议
- 路由安装从 shell 命令（`/sbin/route`、`ip route`）重构为系统 API:macOS 用 PF_ROUTE routing socket、Linux 用 NETLINK_ROUTE(Windows 已是 `CreateIpForwardEntry2`)
- 组播路由安装失败时降级为 best-effort（记录日志/失败信息），不阻断 VPN 启动
- 验证并处理各平台边界：Windows 与系统自动 `224.0.0.0/4` 路由共存、停止 VPN 时的精确路由清理

## Capabilities

### New Capabilities

- `tun-multicast-routing`: TUN 设备的组播路由安装与生命周期管理，使组播流量能够进入 VPN 隧道并被转发，支撑游戏局域网发现

### Modified Capabilities

（无 — 本仓库尚无既有 specs）

## Impact

- **代码**：
  - `src/integrations/steam/steam_vpn_bridge.cpp` — 组播路由安装调用点（`onNegotiationSuccess`）
  - `src/platform/tun/tun_macos.cpp` / `tun_macos_daemon.cpp` — macOS 路由安装与 daemon 校验
  - `src/platform/tun/tun_linux.cpp` — Linux 路由安装
  - `src/platform/tun/tun_windows.cpp` / `windows_network_config.cpp` — Windows 路由安装与去重
- **系统副作用**：仅接管 Minecraft Java 的 `224.0.2.60`；mDNS/Bonjour、SSDP 等其他本地组播仍走物理网卡
- **依赖**：无新增第三方依赖
