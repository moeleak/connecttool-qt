#ifdef __linux__

#include "platform/route/route_manager.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace tun {
namespace {

int maskToPrefix(const std::string &mask) {
  in_addr addr{};
  if (inet_pton(AF_INET, mask.c_str(), &addr) != 1) {
    return -1;
  }
  uint32_t m = ntohl(addr.s_addr);
  int prefix = 0;
  while (m & 0x80000000) {
    prefix++;
    m <<= 1;
  }
  if (m != 0) {
    return -1; // 非连续掩码
  }
  return prefix;
}

template <typename T>
void appendAttribute(std::vector<uint8_t> &msg, uint16_t type, const T &value) {
  const size_t offset = msg.size();
  msg.resize(offset + NLA_ALIGN(sizeof(rtattr) + sizeof(T)), 0);
  auto *rta = reinterpret_cast<rtattr *>(msg.data() + offset);
  rta->rta_type = type;
  rta->rta_len = RTA_LENGTH(sizeof(T));
  std::memcpy(RTA_DATA(rta), &value, sizeof(T));
}

bool sendAndAck(const std::vector<uint8_t> &msg, uint32_t seq,
                bool treatNotFoundAsSuccess, std::string *error) {
  const int fd = ::socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (fd < 0) {
    *error = std::string("netlink socket: ") + std::strerror(errno);
    return false;
  }
  // 设置接收超时，避免缺失 ACK 时永久阻塞 VPN 启停路径
  const timeval rcvTimeout{1, 0};
  if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout,
                   sizeof(rcvTimeout)) < 0) {
    *error = std::string("netlink setsockopt: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  bool ok = false;
  bool done = false;
  ssize_t sent = -1;
  do {
    sent = ::send(fd, msg.data(), msg.size(), 0);
  } while (sent < 0 && errno == EINTR);
  if (sent != static_cast<ssize_t>(msg.size())) {
    *error = std::string("netlink send: ") + std::strerror(errno);
    done = true;
  }
  while (!done) {
    alignas(nlmsghdr) uint8_t buffer[512];
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      *error = std::string("netlink recv: ") + std::strerror(errno);
      break;
    }
    if (n == 0) {
      *error = "netlink recv: unexpected EOF";
      break;
    }
    int len = static_cast<int>(n);
    for (nlmsghdr *nh = reinterpret_cast<nlmsghdr *>(buffer);
         NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
      if (nh->nlmsg_seq != seq || nh->nlmsg_type != NLMSG_ERROR) {
        continue;
      }
      const auto *err = static_cast<nlmsgerr *>(NLMSG_DATA(nh));
      if (err->error == 0 ||
          (treatNotFoundAsSuccess && err->error == -ESRCH)) {
        ok = true;
      } else {
        *error = std::string("rtnetlink: ") + std::strerror(-err->error);
      }
      done = true;
      break;
    }
    // 未匹配的通告消息继续读，直到拿到本请求 ACK
  }
  ::close(fd);
  return ok;
}

uint32_t nextSeq() {
  static std::atomic<uint32_t> seq{1};
  return seq.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

namespace detail {

std::vector<uint8_t> buildRouteRequest(uint16_t type, uint32_t seq,
                                       const Ipv4Route &route,
                                       uint32_t ifindex) {
  if (type != RTM_NEWROUTE && type != RTM_DELROUTE) {
    return {};
  }
  in_addr network{};
  if (inet_pton(AF_INET, route.network.c_str(), &network) != 1) {
    return {};
  }
  const int prefix = maskToPrefix(route.netmask);
  if (prefix < 0) {
    return {};
  }
  // 归一化：清掉 host 位
  if (prefix < 32) {
    const uint32_t mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    network.s_addr = htonl(ntohl(network.s_addr) & mask);
  }

  std::vector<uint8_t> msg(sizeof(nlmsghdr) + sizeof(rtmsg), 0);
  auto *nh = reinterpret_cast<nlmsghdr *>(msg.data());
  nh->nlmsg_type = type;
  nh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  if (type == RTM_NEWROUTE) {
    nh->nlmsg_flags |= NLM_F_CREATE | NLM_F_REPLACE;
  }
  nh->nlmsg_seq = seq;

  auto *rtm = reinterpret_cast<rtmsg *>(msg.data() + sizeof(nlmsghdr));
  rtm->rtm_family = AF_INET;
  rtm->rtm_dst_len = static_cast<uint8_t>(prefix);
  rtm->rtm_table = RT_TABLE_MAIN;
  rtm->rtm_protocol = RTPROT_STATIC;
  rtm->rtm_scope = RT_SCOPE_LINK;
  rtm->rtm_type = RTN_UNICAST;

  if (prefix > 0) {
    appendAttribute(msg, RTA_DST, network);
  }
  appendAttribute(msg, RTA_OIF, ifindex);
  // 注意：appendAttribute 可能触发 vector 重新分配，必须在追加完成后
  // 重新取指针写 nlmsg_len，不能复用前面的 nh（悬垂指针）。
  reinterpret_cast<nlmsghdr *>(msg.data())->nlmsg_len =
      static_cast<uint32_t>(msg.size());
  return msg;
}

} // namespace detail

namespace {

class RouteManagerLinux final : public RouteManager {
public:
  explicit RouteManagerLinux(uint32_t ifindex) : ifindex_(ifindex) {}

  bool addRoute(const Ipv4Route &route, std::string *error) override {
    const uint32_t seq = nextSeq();
    const auto msg =
        detail::buildRouteRequest(RTM_NEWROUTE, seq, route, ifindex_);
    if (msg.empty()) {
      *error = "Invalid route " + route.network + "/" + route.netmask;
      return false;
    }
    return sendAndAck(msg, seq, false, error);
  }

  bool removeRoute(const Ipv4Route &route, std::string *error) override {
    const uint32_t seq = nextSeq();
    const auto msg =
        detail::buildRouteRequest(RTM_DELROUTE, seq, route, ifindex_);
    if (msg.empty()) {
      *error = "Invalid route " + route.network + "/" + route.netmask;
      return false;
    }
    return sendAndAck(msg, seq, true, error); // ESRCH 视为成功
  }

private:
  uint32_t ifindex_;
};

} // namespace

std::unique_ptr<RouteManager> createRouteManager(const std::string &ifname) {
  const unsigned index = if_nametoindex(ifname.c_str());
  if (index == 0) {
    return nullptr;
  }
  return std::make_unique<RouteManagerLinux>(index);
}

} // namespace tun

#endif // __linux__
