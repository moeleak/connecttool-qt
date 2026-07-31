# Tasks: add-tun-multicast-route

## 1. Linux：netlink 路由安装

- [x] 1.1 在 `tun_linux.cpp` 中实现 netlink helper：`RTM_NEWROUTE`(`NLM_F_CREATE|NLM_F_REPLACE`、`RTA_DST`、`RTA_OIF` 经 `if_nametoindex`、`RT_SCOPE_LINK`、proto static），读取 ack 判定成败(实现落于 `src/platform/route/route_manager_linux.cpp`,plan Task 2)
- [x] 1.2 用 netlink helper 重写 `add_route`，删除 `system("ip route ...")` 与 `route` fallback 路径（实现于 plan Task 5，委托 RouteManager）
- [x] 1.3 在隔离 Linux 容器中验证生产 RouteManager 安装 `224.0.2.60/32` 后 wildcard UDP 宣告进入真实 TUN，删除路由与关闭接口后均无残留

## 2. macOS：routing socket 路由安装

- [x] 2.1 实现 PF_ROUTE helper：`RTM_ADD`(`rt_msghdr` + `RTA_DST`/`RTA_NETMASK`/`RTA_IFP` 经 sockaddr_dl,`RTF_STATIC|RTF_UP`),`EEXIST` 时降级 `RTM_CHANGE`(实现落于 `src/platform/route/route_manager_macos.cpp`,plan Task 3)
- [x] 2.2 用 routing-socket helper 重写 `tun_macos.cpp` 的 `add_route`（root 直执行路径）（实现于 plan Task 6，委托 RouteManager）
- [x] 2.3 `tun_macos_daemon.cpp` 的 `ADD_ROUTE` 处理改用同一 helper（协议不变），删除 fork `route` 命令的实现（实现于 plan Task 6）
- [ ] 2.4 验证直执行与 daemon 两条路径下网段路由与组播路由均生效（`netstat -rn` 确认）

## 3. 桥接层集成

- [x] 3.1 在 `steam_vpn_bridge.cpp` 的 `onNegotiationSuccess` 网段路由安装后，调用 `add_route("224.0.2.60", "255.255.255.255")`；失败仅记录日志，不调用 `recordFailure`、不中断启动
- [x] 3.2 Windows：精确匹配 `(LUID, prefix, NextHop)`；保留其他接口和所有预先存在的路由，仅清理本 RouteManager 成功创建的 `/32`
- [x] 3.3 Minecraft 接收端兜底：保留原始 `224.0.2.60:4445` 注入，并额外生成目的为本机 TUN IP、源为认证 peer TUN IP且校验和重算的单播副本

## 4. 端到端验证

- [ ] 4.1 构建并实测：组播包进入 TUN（日志确认 `tunReadLoop` 读到 224.x 包并转发给所有 peer)
- [ ] 4.2 Minecraft 双机实测：A 开局域网世界，B 多人列表自动出现；记录 Linux 接收端投递结果（若失败，按 design 风险项评估兜底方案并回报用户）
- [ ] 4.3 回归：单播联机正常、反复启停 VPN 无路由残留

验证记录（2026-07-31）：生产 `SteamVpnBridge::handleVpnMessage` + 真实 macOS utun 实测中，未加入组播组、仅绑定 `0.0.0.0:4445` 的 UDP socket 成功收到单播兜底；即使原包伪造其他源地址，实际源也被规范为认证 peer 的 TUN IP，目的为本机 TUN IP，校验和由内核验证。Docker 隔离环境中，生产 Linux RouteManager + TUN 已证明 `/32` 可令普通 wildcard UDP 组播进入 TUN，并在删除后无残留。macOS helper 路径也实测 `/32` 期间只接管 Minecraft 地址、父 `/4` 始终保留在 en0，关闭 utun 后精确路由自动回收并回落到 en0。双机 Minecraft 与真实 Windows 路由表仍按 4.1–4.3 验收。
