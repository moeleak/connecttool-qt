# Design: add-tun-multicast-route

## Context

当前 VPN 桥（`SteamVpnBridge`）在 IP 协商成功后只安装虚拟网段路由（`10.0.0.0/8 → TUN`，见 `steam_vpn_bridge.cpp` 的 `onNegotiationSuccess`）。以 Minecraft 为代表的游戏局域网发现使用组播（`224.0.2.60:4445`），组播目的地址不在虚拟网段内，OS 将其路由到默认物理接口，组播包从不进入 TUN 设备，`tunReadLoop` 无从读取和转发。

转发侧已就绪：`tunReadLoop` 的 `isBroadcastAddress()` 已覆盖 `255.255.255.255`、子网广播和 224–239 组播段，收到即通过 `broadcastMessage` 发给全部 peer；接收侧 `handleVpnMessage` 也会把组播包写回 TUN。**缺的只是"让组播包进入 TUN"这一步的路由。**

现有 `TunInterface::add_route` 在三平台的实现方式：

| 平台 | 现状 | 目标实现（系统 API） |
|------|------|----------------------|
| macOS | `system("/sbin/route -n add -net ... -interface ...")`；daemon `ADD_ROUTE` 内部同样 fork `route` 命令 | PF_ROUTE routing socket(`RTM_ADD`），进程内（root）与 daemon 共用同一路由安装实现 |
| Linux | `system("ip route replace ... dev ...")`，fallback `route add` | NETLINK_ROUTE socket(`RTM_NEWROUTE`,`NLM_F_CREATE\|NLM_F_REPLACE`,`RTA_OIF` 指定接口，`RT_SCOPE_LINK` on-link） |
| Windows | `CreateIpForwardEntry2`（on-link，Metric=1） | **已是系统 API，无需改动**，仅验证组播前缀行为 |

## Goals / Non-Goals

**Goals:**
- VPN 启动后，本机组播流量（`224.0.0.0/4`）被路由进 TUN 并经现有逻辑转发给所有 peer
- 路由安装一律使用系统 API，不 shell out 外部命令（消除对 `route`/`ip` 二进制的依赖、避免 fork 开销与注入面）
- 三平台行为一致；组播路由安装失败不阻断 VPN 启动（best-effort 降级）
- 停止 VPN 后无路由残留

**Non-Goals:**
- 不修改 VPN 封包协议与转发逻辑（已存在）
- 不实现 IGMP 组成员管理
- 不处理 `255.255.255.255` 有限广播的路由劫持
- 不做接收端"组播→127.0.0.1 单播中继"兜底（仅当实测证明内核投递失败时才另开 change）
- 不改动 macOS daemon 的权限模型与 socket 协议（`ADD_ROUTE` 命令格式不变，仅替换内部实现）

## Decisions

### D1: 在 `onNegotiationSuccess` 中紧随网段路由之后安装组播路由

复用现有 `add_route` 抽象，调用点唯一、三平台自动覆盖。失败时记录日志但不调用 `recordFailure`、不中断启动（组播是增强能力，单播连通性是核心能力）。

### D2: 路由范围初版采用完整 `224.0.0.0/4`

理由：游戏组播地址不统一（Minecraft `224.0.2.60`，其他游戏各异），窄路由（如仅 `224.0.2.60/32`）只能修单个游戏。代价是 VPN 开启期间本机所有组播（mDNS/Bonjour `224.0.0.251`、SSDP `239.255.255.250` 等）被劫持进 TUN，本地局域网组播服务暂时受影响——与 ZeroTier 等同类工具行为一致，且 VPN 关闭后路由随接口消失自动恢复。

备选：只路由已知游戏组播地址的 /32 集合——拒绝，维护成本高且覆盖面窄。

### D3: 组播路由视为网段路由的附属物，不引入独立生命周期

不新增配置项、不新增接口方法；`stop()` 时无需显式删除（macOS/Linux 路由随接口关闭失效；Windows 待验证 `CreateIpForwardEntry2` 创建的条目是否随 adapter 销毁回收，若不回收则在 `close()` 中补充 `DeleteIpForwardEntry2`）。

### D4: 路由安装重构为系统 API（用户明确要求）

- **Linux**：新增内部 netlink helper——构造 `rtmsg`+`RTA_DST`+`RTA_OIF`(`if_nametoindex`）的 `RTM_NEWROUTE` 请求，`NLM_F_CREATE|NLM_F_REPLACE` 语义等价于现有 `ip route replace`；读取 ack 判定成功。删除 fallback 的 `route` 命令路径。
- **macOS**：新增内部 routing-socket helper——构造 `rt_msghdr`+`RTA_DST`+`RTA_NETMASK`+`RTA_IFP`(sockaddr_dl 携带接口索引）的 `RTM_ADD`,`RTF_STATIC|RTF_UP`；若报 `EEXIST` 则发 `RTM_CHANGE` 重试，等价于现有 add/change 双命令。daemon 与进程内路径共用该 helper,daemon 的 `ADD_ROUTE` 协议不变。
- **Windows**：维持 `CreateIpForwardEntry2` 不动。

网段路由与组播路由共用同一条 API 路径，避免两套实现并存。

## Risks / Trade-offs

- [Linux TUN 无 `IFF_MULTICAST`，接收端内核可能不把注入的组播包投递给仅在默认接口 join 组的游戏 socket] → verify 阶段双机实测 Minecraft 发现；若失败，评估补充 `IFF_MULTICAST` 标志或另开 change 做中继兜底
- [VPN 开启期间本机全部组播被劫持，mDNS 等本地服务受影响] → 接受（与同类 VPN 一致），在 release notes 中说明；后续可按需收窄
- [Windows 系统默认 `224.0.0.0/4` 路由条目与新增条目冲突或去重误判] → `ensureOnLinkRoute` 已按 (prefix, interface) 匹配，实现时验证匹配维度是否包含接口，必要时调整匹配逻辑或先删后建
- [routing socket / netlink 实现错误导致路由安装失败] → 失败路径为 best-effort 降级（不影响单播），且三平台各自有可回退的独立实现；通过双机实测验证
- [macOS 未走 daemon 且非 root 时 routing socket 仍会 EPERM] → 与现有行为一致（网段路由同样需权限），无新增风险

## Migration Plan

纯增量改动，无数据迁移。`add_route` 行为对外不变（语义等价），仅实现替换。回滚 = 移除组播路由安装调用；API 重构可独立回退为命令实现。

## Open Questions

1. Linux 接收端组播投递是否可用（待 verify 实测）
2. Windows 路由条目是否随 adapter 关闭自动回收（待实现时验证）
