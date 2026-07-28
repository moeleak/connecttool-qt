# Tasks: add-tun-multicast-route

## 1. Linux：netlink 路由安装

- [x] 1.1 在 `tun_linux.cpp` 中实现 netlink helper：`RTM_NEWROUTE`(`NLM_F_CREATE|NLM_F_REPLACE`、`RTA_DST`、`RTA_OIF` 经 `if_nametoindex`、`RT_SCOPE_LINK`、proto static），读取 ack 判定成败(实现落于 `src/platform/route/route_manager_linux.cpp`,plan Task 2)
- [x] 1.2 用 netlink helper 重写 `add_route`，删除 `system("ip route ...")` 与 `route` fallback 路径（实现于 plan Task 5，委托 RouteManager）
- [ ] 1.3 验证网段路由与 `224.0.0.0/4` 组播路由均生效（`ip route show` 确认）；评估接口是否需要补充 `IFF_MULTICAST` 标志

## 2. macOS：routing socket 路由安装

- [x] 2.1 实现 PF_ROUTE helper：`RTM_ADD`(`rt_msghdr` + `RTA_DST`/`RTA_NETMASK`/`RTA_IFP` 经 sockaddr_dl,`RTF_STATIC|RTF_UP`),`EEXIST` 时降级 `RTM_CHANGE`(实现落于 `src/platform/route/route_manager_macos.cpp`,plan Task 3)
- [x] 2.2 用 routing-socket helper 重写 `tun_macos.cpp` 的 `add_route`（root 直执行路径）（实现于 plan Task 6，委托 RouteManager）
- [x] 2.3 `tun_macos_daemon.cpp` 的 `ADD_ROUTE` 处理改用同一 helper（协议不变），删除 fork `route` 命令的实现（实现于 plan Task 6）
- [ ] 2.4 验证直执行与 daemon 两条路径下网段路由与组播路由均生效（`netstat -rn` 确认）

## 3. 桥接层集成

- [x] 3.1 在 `steam_vpn_bridge.cpp` 的 `onNegotiationSuccess` 网段路由安装后，调用 `add_route("224.0.0.0", "240.0.0.0")`；失败仅记录日志，不调用 `recordFailure`、不中断启动（实现于 plan Task 8）
- [ ] 3.2 Windows：验证 `ensureOnLinkRoute` 对 `224.0.0.0/4` 的行为（系统默认条目冲突/去重维度是否含接口），必要时调整匹配逻辑或先删后建；确认 adapter 销毁后路由是否回收，不回收则在 `close()` 补充 `DeleteIpForwardEntry2`

## 4. 端到端验证

- [ ] 4.1 构建并实测：组播包进入 TUN（日志确认 `tunReadLoop` 读到 224.x 包并转发给所有 peer)
- [ ] 4.2 Minecraft 双机实测：A 开局域网世界，B 多人列表自动出现；记录 Linux 接收端投递结果（若失败，按 design 风险项评估兜底方案并回报用户）
- [ ] 4.3 回归：单播联机正常、反复启停 VPN 无路由残留
