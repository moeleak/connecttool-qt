# Brainstorm Summary

- Change: add-tun-multicast-route
- Date: 2026-07-28

## 确认的技术方案

**跨平台 RouteManager 抽象（方案 C，用户选定）**

- 新增 `src/platform/route/`:`route_manager.h`(`Ipv4Route`、`RouteManager` 抽象：`addRoute`/`removeRoute` 返回 bool+error string、工厂 `createRouteManager(ifname)`)+ 三平台实现文件
- Linux:`NETLINK_ROUTE`,`RTM_NEWROUTE`(`NLM_F_CREATE|NLM_F_REPLACE`,`RTA_DST`+`RTA_OIF`,`RT_SCOPE_LINK`,`RTPROT_STATIC`),ack 判定；删除 = `RTM_DELROUTE`,`ESRCH` 视为成功
- macOS:`PF_ROUTE`,`RTM_ADD`(`RTA_DST|RTA_GATEWAY|RTA_NETMASK`,gateway=sockaddr_dl+ifindex,`RTF_UP|RTF_STATIC`),`EEXIST`→`RTM_CHANGE`；删除 = `RTM_DELETE`;app 与 daemon 共享编译单元,`ADD_ROUTE` 协议不变
- Windows：路由增删从 `windows_network_config.cpp` 迁入 `RouteManagerWindows`(`CreateIpForwardEntry2`/`DeleteIpForwardEntry2`)，同前缀先删后建
- 各 `TunInterface` 持有 RouteManager,`add_route()` 委托，接口签名不变
- 桥接层:`onNegotiationSuccess` 网段路由后加装 `224.0.0.0/240.0.0.0`，失败仅日志不阻断；`stop()` 显式 `removeRoute`（组播+网段）再 close
- 网段路由失败维持原语义（recordFailure 中止启动）

## 关键取舍与风险

- 完整 `224.0.0.0/4` 路由：覆盖所有游戏组播；副作用是 VPN 期间本机 mDNS/SSDP 等被劫持（接受，与 ZeroTier 一致）
- Linux 接收端组播投递未经验证（TUN 无 IFF_MULTICAST)——已知风险，实测留待后续
- Windows 系统默认组播路由条目：先删后建规避冲突
- 显式 removeRoute + 接口关闭双保险，消除路由残留

## 测试策略

- 单测：netlink/rt_msghdr 消息构造纯函数（无需 root);RouteManager 可 mock
- 实测：macOS↔Windows 双端 Minecraft;`netstat -rn`/`route print` 验证装删与残留
- Linux：仅编译验证 + 代码审查（用户确认接受未实测）

## Spec Patch

- 「停止无残留」场景补充显式 removeRoute + 接口关闭双保险说明
- 新增已知限制说明：Linux 组播功能本次仅编译验证，未经实测

## 已确认约束

- 可实测平台：macOS、Windows;Linux 接受未实测
- 不加配置开关、不做 IGMP、不做接收端中继兜底、不改 daemon 协议与权限模型
