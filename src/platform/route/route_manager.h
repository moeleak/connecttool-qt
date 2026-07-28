#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tun {

struct Ipv4Route {
  std::string network; // 例: "224.0.0.0"
  std::string netmask; // 例: "240.0.0.0"
};

// 跨平台路由增删抽象。幂等语义：addRoute 对已存在的等价路由视为成功；
// removeRoute 对不存在的路由视为成功。无异常，经 error 回传失败原因。
class RouteManager {
public:
  virtual ~RouteManager() = default;
  virtual bool addRoute(const Ipv4Route &route, std::string *error) = 0;
  virtual bool removeRoute(const Ipv4Route &route, std::string *error) = 0;
};

// 创建绑定到指定接口的 RouteManager（Linux/macOS 解析 ifindex；
// Windows 经接口别名解析 LUID/index）。失败返回 nullptr。
std::unique_ptr<RouteManager> createRouteManager(const std::string &ifname);

namespace detail {
#if defined(__linux__)
// 构造 rtnetlink 请求报文（nlmsghdr + rtmsg + RTA_DST + RTA_OIF）。
// type 仅接受 RTM_NEWROUTE / RTM_DELROUTE；地址或掩码非法时返回空 vector。
// 抽成纯函数以便无 root 单测。
std::vector<uint8_t> buildRouteRequest(uint16_t type, uint32_t seq,
                                       const Ipv4Route &route,
                                       uint32_t ifindex);
#elif defined(__APPLE__)
// 构造 PF_ROUTE 写报文（rt_msghdr + sockaddr_in dst + sockaddr_dl 网关
// + 截断 netmask sockaddr）。type 仅接受 RTM_ADD / RTM_CHANGE / RTM_DELETE；
// 地址或掩码非法时返回空 vector。抽成纯函数以便无 root 单测。
std::vector<uint8_t> buildRouteMessage(uint8_t type, int32_t pid, int32_t seq,
                                       const Ipv4Route &route,
                                       uint16_t ifindex);
#endif
} // namespace detail

} // namespace tun
