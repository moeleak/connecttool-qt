# Subagent Progress: add-tun-multicast-route

## Current Task

- Plan task text: 全分支终审(Task 1-8 全部完成勾选);Task 9 手动实测待用户
- OpenSpec task text: 3.2 运行时确认与 4.x 待 Task 9

## Stage

`awaiting-user-verification`(清理批量 re-review APPROVED,代码侧全部收官:Task 1-8 + 终审 + 修复复核;Task 9 手动实测清单已交付用户,2026-07-28)

## Task 8 Result

- spec PASS + quality APPROVED(1 Minor plan-mandated note,round 0/3);checkoff commit 6ff9276;OpenSpec 3.1 已勾
- Impl commit: `565b2f4` feat(vpn): install multicast route on TUN startup and remove routes on stop
- connecttool_steam 目标构建过;ctest 3/3;错误处理矩阵逐项核实

## Task 7 Result

- spec PASS + quality APPROVED(3 Minor cosmetic deferred,round 0/3);checkoff commit 34f1322
- Impl commit: `1ee37af` refactor(tun): delegate Windows route management to RouteManagerWindows
- 桩头 RED(编译错误恰为 ensureOnLinkRoute)→GREEN 证明修复;签名对 route_manager.h 核实

## Task 6 Result

- spec PASS + quality APPROVED(1 Minor cosmetic deferred,round 0/3);checkoff commit 5a0d49b;OpenSpec 2.2/2.3 已勾
- Impl commit: `224a564` refactor(tun): use PF_ROUTE RouteManager on macOS app and daemon
- T3#6 已解决(daemon target 链 route_manager_macos.cpp);EEXIST→RTM_CHANGE 语义保留;ctest 3/3
- Step 5 sudo 实测按 brief 免责条款并入 Task 9

## Task 5 Result

- spec PASS + quality APPROVED(2 Minor no-action,round 0/3);checkoff commit b023228;OpenSpec 1.2 已勾
- Impl commit: `9f61c34` refactor(tun): delegate Linux route management to RouteManager
- GREEN: docker gcc:14 route-linux-message + compile/link + verify_architecture.cmake

## Task 4 Result

- spec PASS + quality APPROVED(3 Minor deferred,round 0/3)
- Implement commit: `8f3ad86` refactor(route): migrate Windows route management into RouteManagerWindows(原 7e2fb36,rebase 改写英文 message 后新 hash)
- Files: route_manager_windows.cpp(新,231 行)、windows_network_config.{h,cpp} 删除路由职责、src/platform/CMakeLists.txt
- 验证:桩头 clang++ -fsyntax-only(无 Windows 工具链,报告如实披露覆盖边界)
- 审查注:`tun_windows.cpp:285` 残留引用为计划内中间状态(Task 7 修复),不标记

## Task 4 Deferred Minor（交 final review 分诊）

- T4#1 route_manager_windows.cpp:152-154 addRoute 同 LUID 早退后不再删跨接口冲突条目(与旧 ensureOnLinkRoute 行为一致,忠实迁移;可改 foundOwn 标志继续循环)
- T4#2 :200 removeRoute 只删第一条匹配(可能残留系统自动组播条目;可去 return 继续遍历)
- T4#3 :234 ASCII 按字节加宽(WinTUN 别名约定 ASCII,风险可忽略;未来用 MultiByteToWideChar(CP_UTF8))

## Completed

- Task 1: RouteManager 抽象头文件(1d16f01)
- Task 2: Linux netlink(13fb7ff + fix 19957e8,spec PASS,quality APPROVED round 1/3,checkoff c863e89,GREEN docker gcc:14 all passed;RED 证据因检查点丢失不可重建,已记录)
- Task 3: macOS PF_ROUTE(09eb5f4,spec PASS 5 处偏差均裁定合理,quality APPROVED,checkoff bd3259b,GREEN ctest 3/3 + -Werror)
- Task 4: Windows RouteManager 迁移(8f3ad86,spec PASS,quality APPROVED round 0/3,checkoff 见本次提交)

## Hash Remap（2026-07-28 英文 message rebase 后）

- ed23857 → 09eb5f4(Task 3 impl)
- 067dfa0 → bd3259b(Task 3 checkoff)
- 691a12f → 19957e8(Task 2 fix)
- 5c528e3 → c863e89(Task 2 checkoff)
- 7e2fb36 → 8f3ad86(Task 4 impl)

## Task 2 Deferred Minor（交 final review 分诊）

- Task2#4 maskToPrefix 与 tun_linux.cpp 重复(可接受现状)
- Task2#5 tests/CMakeLists.txt 守卫 if(UNIX AND NOT APPLE) 含 FreeBSD → 宜改 CMAKE_SYSTEM_NAME STREQUAL "Linux"
- Task2#6 测试缺口:prefix==0 省略 RTA_DST 无用例;未钉报文总长 44B 与 rta_len=8
- Task2#7 ACK 解析未校验 NLMSG_PAYLOAD >= sizeof(nlmsgerr)(深度防御)

## Task 3 Deferred Minor（交 final review 分诊）

- T3#1 route_manager_macos.cpp:117-120 短写分支 strerror(errno) 用陈旧 errno → 固定文案
- T3#2 :128 EAGAIN==EWOULDBLOCK(35) 条件冗余 → 注释或单条件
- T3#3 测试缺 RTM_CHANGE 正向断言(EEXIST 幂等降级无保护)→ testRejectsInvalidInput 补 CHECK
- T3#4 测试边界 CHECK 失败后继续走查可能越界读 → return 提前退出(可不改)
- T3#5 RTM_DELETE/CHANGE 携带 RTF_UP|RTF_STATIC 语义疑惑 → 加注释(可选)
- T3#6 信息项:daemon target 尚未链接 route_manager_macos.cpp → 属 Task 6 范围,派发时须确认

## Environment Notes

- 本机 macOS arm64;Linux 测试经 `docker run --rm -v $PWD:/src -w /src gcc:14` 直编译运行
- 提交需 `git -c commit.gpgsign=false`(pinentry 不可用,19957e8 起未签名)
- **commit message 必须全英文**(用户明确要求,所有派发 prompt 必须包含此约束)
- review-fix round: Task 4 = 0/3
- 主 build/ 目录因无关原因损坏(CoreAudio blocks、OpenGL/gl.h),单文件验证走 compile_commands.json
