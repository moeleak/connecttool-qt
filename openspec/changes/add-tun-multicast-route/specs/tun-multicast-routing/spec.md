# tun-multicast-routing 规格

## ADDED Requirements

### Requirement: VPN 启动时安装组播路由

系统 SHALL 在 VPN IP 协商成功、虚拟网段路由安装完成后，为 TUN 设备安装 Minecraft 发现主机路由（`224.0.2.60/32` 指向 TUN 设备），使本机发出的宣告进入 TUN 设备。该行为 MUST 在 macOS、Linux、Windows 三个平台一致生效，且 MUST NOT 删除或替换物理接口的 `224.0.0.0/4` 系统路由。

#### Scenario: 组播路由安装成功

- **WHEN** VPN 启动且 IP 协商成功
- **THEN** 系统路由表中存在 `224.0.2.60/32` 指向 TUN 设备的路由条目
- **AND** 本机发往 `224.0.2.60` 的数据包进入 TUN 设备被桥接层读取
- **AND** 其他组播地址继续按系统原路由发送

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

### Requirement: Linux 平台隔离验证

系统 MUST 在隔离 Linux 网络环境中使用生产 RouteManager 与真实 TUN 验证 Minecraft `/32` 发送入口；完整 Steam P2P 与 Minecraft UI 仍由双机验收覆盖。

#### Scenario: Linux TUN 发送入口

- **WHEN** wildcard UDP socket 向 `224.0.2.60:4445` 发送 Minecraft 宣告
- **THEN** 生产 `/32` 路由使数据包从 TUN 读出，源地址为发送端 TUN IP
- **AND** IPv4/UDP 校验和及 payload 有效，删除路由与关闭接口后无残留

### Requirement: 游戏局域网发现端到端可用

系统在组播路由与转发链路就绪后，SHALL 使基于组播的局域网发现（以 Minecraft `224.0.2.60:4445` 为基准场景）在 VPN 两端可用。

#### Scenario: Minecraft 局域网世界被发现

- **WHEN** 两台机器通过 TUN 模式互连，A 在 Minecraft 中「对局域网开放」世界
- **THEN** B 的 Minecraft 多人游戏列表在扫描周期内自动显示 A 的世界
- **AND** 当 B 的组播 socket 加入了物理网卡而非 TUN 时，系统向 B 的 TUN IP 注入一份宣告单播副本作为投递兜底
- **AND** 单播副本的源地址取自 A 已认证的 TUN 路由，并重算 IPv4/UDP 校验和，使列表条目连接到 A 而非 B 自身
- **AND** 原始组播包仍被注入，不影响已正确加入 TUN 组播组的接收端
