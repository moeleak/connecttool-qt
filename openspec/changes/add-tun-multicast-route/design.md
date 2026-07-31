# Design: add-tun-multicast-route

## Context

当前 VPN 桥（`SteamVpnBridge`）在 IP 协商成功后只安装虚拟网段路由（`10.0.0.0/8 → TUN`，见 `steam_vpn_bridge.cpp` 的 `onNegotiationSuccess`）。以 Minecraft 为代表的游戏局域网发现使用组播（`224.0.2.60:4445`），组播目的地址不在虚拟网段内，OS 将其路由到默认物理接口，组播包从不进入 TUN 设备，`tunReadLoop` 无从读取和转发。

转发侧已就绪：`tunReadLoop` 的 `isBroadcastAddress()` 已覆盖 `255.255.255.255`、子网广播和 224–239 组播段，收到即通过 `broadcastMessage` 发给全部 peer；接收侧 `handleVpnMessage` 也会把组播包写回 TUN。除了让发送包进入 TUN 的路由外，还需处理接收 socket 的接口归属：组播成员关系绑定具体接口，游戏若在物理网卡加入组播组，内核不会把从 TUN 注入的同组数据交给它。

现有 `TunInterface::add_route` 在三平台的实现方式：

| 平台 | 现状 | 目标实现（系统 API） |
|------|------|----------------------|
| macOS | `system("/sbin/route -n add -net ... -interface ...")`；daemon `ADD_ROUTE` 内部同样 fork `route` 命令 | PF_ROUTE routing socket(`RTM_ADD`），进程内（root）与 daemon 共用同一路由安装实现 |
| Linux | `system("ip route replace ... dev ...")`，fallback `route add` | NETLINK_ROUTE socket(`RTM_NEWROUTE`,`NLM_F_CREATE\|NLM_F_REPLACE`,`RTA_OIF` 指定接口，`RT_SCOPE_LINK` on-link） |
| Windows | `CreateIpForwardEntry2`（on-link，Metric=1） | **已是系统 API，无需改动**，仅验证组播前缀行为 |

## Goals / Non-Goals

**Goals:**
- VPN 启动后，本机 Minecraft 发现流量（`224.0.2.60/32`）被路由进 TUN 并经现有逻辑转发给所有 peer
- 路由安装一律使用系统 API，不 shell out 外部命令（消除对 `route`/`ip` 二进制的依赖、避免 fork 开销与注入面）
- 三平台行为一致；组播路由安装失败不阻断 VPN 启动（best-effort 降级）
- 停止 VPN 后无路由残留
- 为 Minecraft `224.0.2.60:4445` 宣告提供接收端单播投递兜底，并把源地址规范为已认证房间成员的 TUN IP

**Non-Goals:**
- 不修改 VPN 封包协议与转发逻辑（已存在）
- 不实现 IGMP 组成员管理
- 不处理 `255.255.255.255` 有限广播的路由劫持
- 不把接收端单播兜底泛化到所有组播协议（仅处理 Minecraft 基准场景）
- 不改动 macOS daemon 的权限模型与 socket 协议（`ADD_ROUTE` 命令格式不变，仅替换内部实现）

## Decisions

### D1: 在 `onNegotiationSuccess` 中紧随网段路由之后安装组播路由

复用现有 `add_route` 抽象，调用点唯一、三平台自动覆盖。失败时记录日志但不调用 `recordFailure`、不中断启动（组播是增强能力，单播连通性是核心能力）。

### D2: 路由范围采用 Minecraft 精确主机路由 `224.0.2.60/32`

本 change 的验收目标是 Minecraft Java。Windows 会自动为每个 IPv4 接口建立 `224.0.0.0/4`，同长度路由还需比较 route metric 与 interface metric 的总和；`/32` 通过最长前缀稳定优先于这些自动路由。同时它不会把 mDNS/Bonjour、LLMNR、SSDP 等无关流量发给房间成员。

后续支持其他游戏时使用显式组播地址 allowlist 扩展，不恢复整个 `/4` 劫持。

### D3: 组播路由视为网段路由的附属物，不引入独立生命周期

不新增配置项、不新增接口方法；`stop()` 时无需显式删除（macOS/Linux 路由随接口关闭失效；Windows 待验证 `CreateIpForwardEntry2` 创建的条目是否随 adapter 销毁回收，若不回收则在 `close()` 中补充 `DeleteIpForwardEntry2`）。

### D4: 路由安装重构为系统 API（用户明确要求）

- **Linux**：新增内部 netlink helper——构造 `rtmsg`+`RTA_DST`+`RTA_OIF`(`if_nametoindex`）的 `RTM_NEWROUTE` 请求，`NLM_F_CREATE|NLM_F_REPLACE` 语义等价于现有 `ip route replace`；读取 ack 判定成功。删除 fallback 的 `route` 命令路径。
- **macOS**：新增内部 routing-socket helper——构造 `rt_msghdr`+`RTA_DST`+`RTA_NETMASK`+`RTA_IFP`(sockaddr_dl 携带接口索引）的 `RTM_ADD`,`RTF_STATIC|RTF_UP`；若报 `EEXIST` 则发 `RTM_CHANGE` 重试，等价于现有 add/change 双命令。daemon 与进程内路径共用该 helper,daemon 的 `ADD_ROUTE` 协议不变。
- **Windows**：同一精确 on-link 路由按 `(LUID, prefix, NextHop=0)` 去重；保留其他接口以及所有预先存在的路由，只删除当前 RouteManager 确认由 `CreateIpForwardEntry2` 新建的条目。

网段路由与组播路由共用同一条 API 路径，避免两套实现并存。

### D5: Minecraft 接收端同时注入原组播包与 TUN-IP 单播副本

对收到的 IPv4 UDP `224.0.2.60:4445` 宣告，保留原包写入 TUN，并额外把目的地址改为本机 TUN IP 后再次写入。Minecraft 的 `MulticastSocket` 绑定 wildcard 地址，因此无需依赖它在哪个接口加入组播组也能收到单播副本。仅当前 Steam sender 属于房间、且 `senderSteamID + nodeId` 命中已协商路由时生成副本；其源地址规范为该 peer 的 TUN IP（多人列表据此连接房主），并重算 IPv4 与非零 UDP 校验和。

## Risks / Trade-offs

- [接收端内核不把 TUN 组播投递给在物理接口 join 组的游戏 socket] → Minecraft 宣告额外注入目的为本机 TUN IP 的单播副本；保留原组播路径
- [无关组播被转发或本地发现中断] → 仅安装 `224.0.2.60/32`，其他组播仍使用物理网卡
- [Windows 系统自动 `224.0.0.0/4` 抢占发送路径] → Minecraft `/32` 先按最长前缀胜出；不删除或修改物理接口路由
- [routing socket / netlink 实现错误导致路由安装失败] → 失败路径为 best-effort 降级（不影响单播），且三平台各自有可回退的独立实现；通过双机实测验证
- [macOS 未走 daemon 且非 root 时 routing socket 仍会 EPERM] → 与现有行为一致（网段路由同样需权限），无新增风险

## Migration Plan

纯增量改动，无数据迁移。`add_route` 行为对外不变（语义等价），仅实现替换。回滚 = 移除组播路由安装调用；API 重构可独立回退为命令实现。

## Open Questions

1. 真实 Windows 上用 JVM 发包并确认 `WintunReceivePacket` 收到、停止后 `/32` 无残留（待双机验收）
