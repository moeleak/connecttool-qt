# Comet Design Handoff

- Change: add-tun-multicast-route
- Phase: design
- Mode: compact
- Context hash: a572e8ea4acad4ac0643a957f4af939be66c2ef526d1f81a760627d259d08b15

Generated-by: comet-handoff.sh

OpenSpec remains the canonical capability spec. This handoff is a deterministic, source-traceable context pack, not an agent-authored summary.

## openspec/changes/add-tun-multicast-route/proposal.md

- Source: openspec/changes/add-tun-multicast-route/proposal.md
- Lines: 1-33
- SHA256: 27d23bac4262687b0fd95fb4825c7ac8cbfa019f9857bb2d473f024d65db62a0

```md
# Proposal: add-tun-multicast-route

## Why

TUN 模式下游戏的局域网发现不可用：以 Minecraft 为例，「对局域网开放」后通过组播 `224.0.2.60:4445` 宣告房间，但 TUN 只安装了虚拟网段（`10.0.0.0/8`）路由，组播包被 OS 路由到物理网卡而非 TUN 设备，永远不会被转发给 peer。这导致双方都连接在 VPN 下时互相搜索不到局域网世界，只能手动输入 IP 直连。

## What Changes

- VPN 启动（IP 协商成功）后，在三个平台（macOS / Linux / Windows）为 TUN 设备额外安装组播路由（`224.0.0.0/4 → TUN`），使本机发出的组播流量进入 TUN 设备
- 复用 `steam_vpn_bridge.cpp` 中已有的组播/广播转发逻辑（`isBroadcastAddress` 已覆盖 224–239 组播段），组播包进入 TUN 后自动被转发至所有 peer，无需改动封包协议
- 路由安装从 shell 命令（`/sbin/route`、`ip route`）重构为系统 API:macOS 用 PF_ROUTE routing socket、Linux 用 NETLINK_ROUTE(Windows 已是 `CreateIpForwardEntry2`)
- 组播路由安装失败时降级为 best-effort（记录日志/失败信息），不阻断 VPN 启动
- 验证并处理各平台边界：Windows 已存在的系统组播路由条目的去重/接管、停止 VPN 时的路由清理

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
- **系统副作用**：VPN 开启期间，本机 `224.0.0.0/4` 组播流量将被路由进 TUN，本地局域网的组播服务（mDNS/Bonjour、SSDP 等）可能暂时不可用——设计阶段需决策是否收窄路由范围
- **依赖**：无新增第三方依赖
```

## openspec/changes/add-tun-multicast-route/design.md

- Source: openspec/changes/add-tun-multicast-route/design.md
- Lines: 1-71
- SHA256: 46446651d63a082a01f670f744f054574f19bb5c63ba8b66bbc92e99f9f0f6b9

```md
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
```

## openspec/changes/add-tun-multicast-route/tasks.md

- Source: openspec/changes/add-tun-multicast-route/tasks.md
- Lines: 1-25
- SHA256: c69fddaa6af083df4b72998bededb34ff5777686fcf68594e88da9bb80408a27

```md
# Tasks: add-tun-multicast-route

## 1. Linux：netlink 路由安装

- [ ] 1.1 在 `tun_linux.cpp` 中实现 netlink helper：`RTM_NEWROUTE`(`NLM_F_CREATE|NLM_F_REPLACE`、`RTA_DST`、`RTA_OIF` 经 `if_nametoindex`、`RT_SCOPE_LINK`、proto static），读取 ack 判定成败
- [ ] 1.2 用 netlink helper 重写 `add_route`，删除 `system("ip route ...")` 与 `route` fallback 路径
- [ ] 1.3 验证网段路由与 `224.0.0.0/4` 组播路由均生效（`ip route show` 确认）；评估接口是否需要补充 `IFF_MULTICAST` 标志

## 2. macOS：routing socket 路由安装

- [ ] 2.1 实现 PF_ROUTE helper：`RTM_ADD`(`rt_msghdr` + `RTA_DST`/`RTA_NETMASK`/`RTA_IFP` 经 sockaddr_dl,`RTF_STATIC|RTF_UP`),`EEXIST` 时降级 `RTM_CHANGE`
- [ ] 2.2 用 routing-socket helper 重写 `tun_macos.cpp` 的 `add_route`（root 直执行路径）
- [ ] 2.3 `tun_macos_daemon.cpp` 的 `ADD_ROUTE` 处理改用同一 helper（协议不变），删除 fork `route` 命令的实现
- [ ] 2.4 验证直执行与 daemon 两条路径下网段路由与组播路由均生效（`netstat -rn` 确认）

## 3. 桥接层集成

- [ ] 3.1 在 `steam_vpn_bridge.cpp` 的 `onNegotiationSuccess` 网段路由安装后，调用 `add_route("224.0.0.0", "240.0.0.0")`；失败仅记录日志，不调用 `recordFailure`、不中断启动
- [ ] 3.2 Windows：验证 `ensureOnLinkRoute` 对 `224.0.0.0/4` 的行为（系统默认条目冲突/去重维度是否含接口），必要时调整匹配逻辑或先删后建；确认 adapter 销毁后路由是否回收，不回收则在 `close()` 补充 `DeleteIpForwardEntry2`

## 4. 端到端验证

- [ ] 4.1 构建并实测：组播包进入 TUN（日志确认 `tunReadLoop` 读到 224.x 包并转发给所有 peer)
- [ ] 4.2 Minecraft 双机实测：A 开局域网世界，B 多人列表自动出现；记录 Linux 接收端投递结果（若失败，按 design 风险项评估兜底方案并回报用户）
- [ ] 4.3 回归：单播联机正常、反复启停 VPN 无路由残留
```

## openspec/changes/add-tun-multicast-route/specs/tun-multicast-routing/spec.md

- Source: openspec/changes/add-tun-multicast-route/specs/tun-multicast-routing/spec.md
- Lines: 1-59
- SHA256: 68aeee47296a05539ba46c512ce03fb0518b31670d061849d4a9470c2bc31506

```md
# tun-multicast-routing 规格

## ADDED Requirements

### Requirement: VPN 启动时安装组播路由

系统 SHALL 在 VPN IP 协商成功、虚拟网段路由安装完成后，为 TUN 设备安装组播路由（`224.0.0.0/4` 指向 TUN 设备），使本机发出的组播流量进入 TUN 设备。该行为 MUST 在 macOS、Linux、Windows 三个平台一致生效。

#### Scenario: 组播路由安装成功

- **WHEN** VPN 启动且 IP 协商成功
- **THEN** 系统路由表中存在 `224.0.0.0/4` 指向 TUN 设备的路由条目
- **AND** 本机发往组播地址（如 `224.0.2.60`）的数据包进入 TUN 设备被桥接层读取

#### Scenario: 组播流量被转发至所有 peer

- **WHEN** 组播路由已安装且本机应用发送组播包
- **THEN** 桥接层将该组播包转发给当前所有已连接 peer
- **AND** 每个 peer 收到后将其写入本地 TUN 设备

### Requirement: 组播路由安装失败不阻断 VPN

系统 SHALL 将组播路由安装视为增强能力：安装失败时 MUST NOT 导致 VPN 启动失败或中断已有单播连通性，且 MUST 记录可诊断的失败信息。

#### Scenario: 组播路由安装失败降级

- **WHEN** 组播路由安装失败（如权限不足、路由冲突）
- **THEN** VPN 保持运行，单播通信不受影响
- **AND** 失败信息被记录（日志或失败状态）

### Requirement: 停止 VPN 后无组播路由残留

系统 SHALL 保证 VPN 停止后，组播路由不再指向已销毁的 TUN 设备：停止流程 MUST 在关闭接口前显式删除组播路由（best-effort），与接口关闭导致的自动回收构成双保险。

#### Scenario: 反复启停无残留无冲突

- **WHEN** 用户多次启动并停止 VPN
- **THEN** 每次启动后组播路由正确指向当前 TUN 设备
- **AND** 停止后路由表中不存在指向已销毁 TUN 设备的组播路由
- **AND** 不出现过期的重复路由条目

### Requirement: Linux 平台验证级别

系统 SHALL 在 macOS 与 Windows 上通过端到端实测验证组播功能；Linux 平台本次 MUST 至少通过编译验证与代码审查，组播端到端实测允许标记为已知限制留待后续。

#### Scenario: Linux 未实测的已知限制

- **WHEN** 本 change 验收时无 Linux 测试环境
- **THEN** Linux 平台以编译通过、netlink 消息构造单测通过、代码审查通过作为验收依据
- **AND** Linux 组播收发未实测的事实被记录为已知限制

### Requirement: 游戏局域网发现端到端可用

系统在组播路由与转发链路就绪后，SHALL 使基于组播的局域网发现（以 Minecraft `224.0.2.60:4445` 为基准场景）在 VPN 两端可用。

#### Scenario: Minecraft 局域网世界被发现

- **WHEN** 两台机器通过 TUN 模式互连，A 在 Minecraft 中「对局域网开放」世界
- **THEN** B 的 Minecraft 多人游戏列表在扫描周期内自动显示 A 的世界
```

