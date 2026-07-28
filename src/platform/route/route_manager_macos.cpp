#ifdef __APPLE__

#include "platform/route/route_manager.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace tun {
namespace {

constexpr size_t kRound = sizeof(uint32_t);

size_t rtaRoundup(size_t n) { return (n + kRound - 1) & ~(kRound - 1); }

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

void appendSockaddr(std::vector<uint8_t> &msg, const void *sa, size_t saLen) {
  const size_t offset = msg.size();
  msg.resize(offset + rtaRoundup(saLen), 0);
  std::memcpy(msg.data() + offset, sa, saLen);
}

// 写入报文并从同一 socket 读回本进程的回执（按 rtm_pid + rtm_seq 匹配，
// 跳过其他进程的内核通告）；rtm_errno 经 outErrno 返回。
bool sendRouteMessage(const std::vector<uint8_t> &msg, int32_t pid, int32_t seq,
                      int *outErrno, std::string *error) {
  const int fd = ::socket(PF_ROUTE, SOCK_RAW, AF_INET);
  if (fd < 0) {
    *error = std::string("routing socket: ") + std::strerror(errno);
    return false;
  }
  // 设置接收超时，避免内核回执丢失时永久阻塞 VPN 启停路径
  const timeval rcvTimeout{1, 0};
  if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout,
                   sizeof(rcvTimeout)) < 0) {
    *error = std::string("routing socket setsockopt: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  *outErrno = EIO;
  bool done = false;
  ssize_t sent = -1;
  do {
    sent = ::write(fd, msg.data(), msg.size());
  } while (sent < 0 && errno == EINTR);
  if (sent != static_cast<ssize_t>(msg.size())) {
    *error = std::string("routing socket write: ") + std::strerror(errno);
    done = true;
  }
  while (!done) {
    alignas(rt_msghdr) uint8_t buffer[2048];
    const ssize_t n = ::read(fd, buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        *error = "routing socket read: timed out waiting for reply";
      } else {
        *error = std::string("routing socket read: ") + std::strerror(errno);
      }
      break;
    }
    if (n == 0) {
      *error = "routing socket read: unexpected EOF";
      break;
    }
    if (n < static_cast<ssize_t>(sizeof(rt_msghdr))) {
      continue;
    }
    const auto *hdr = reinterpret_cast<const rt_msghdr *>(buffer);
    if (hdr->rtm_pid != pid || hdr->rtm_seq != seq) {
      continue; // 其他进程/请求的内核通告
    }
    *outErrno = hdr->rtm_errno;
    done = true;
  }
  ::close(fd);
  if (*outErrno != 0) {
    if (error->empty()) {
      *error = std::string("routing socket: ") + std::strerror(*outErrno);
    }
    return false;
  }
  return true;
}

int32_t nextSeq() {
  static std::atomic<int32_t> seq{1};
  return seq.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

namespace detail {

std::vector<uint8_t> buildRouteMessage(uint8_t type, int32_t pid, int32_t seq,
                                       const Ipv4Route &route,
                                       uint16_t ifindex) {
  if (type != RTM_ADD && type != RTM_CHANGE && type != RTM_DELETE) {
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

  const bool isHost = (prefix == 32);
  std::vector<uint8_t> msg(sizeof(rt_msghdr), 0);
  {
    auto *hdr = reinterpret_cast<rt_msghdr *>(msg.data());
    hdr->rtm_version = RTM_VERSION;
    hdr->rtm_type = type;
    hdr->rtm_pid = pid;
    hdr->rtm_seq = seq;
    hdr->rtm_flags = RTF_UP | RTF_STATIC | (isHost ? RTF_HOST : 0);
    hdr->rtm_addrs = RTA_DST | RTA_GATEWAY | (isHost ? 0 : RTA_NETMASK);
  }

  sockaddr_in dst{};
  dst.sin_len = sizeof(dst);
  dst.sin_family = AF_INET;
  dst.sin_addr = network;
  appendSockaddr(msg, &dst, sizeof(dst));

  sockaddr_dl gateway{};
  gateway.sdl_len = static_cast<uint8_t>(offsetof(sockaddr_dl, sdl_data));
  gateway.sdl_family = AF_LINK;
  gateway.sdl_index = ifindex;
  appendSockaddr(msg, &gateway, gateway.sdl_len);

  if (!isHost) {
    sockaddr_in mask{};
    mask.sin_family = 0; // BSD 掩码约定 family 置 0
    const uint32_t m = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    mask.sin_addr.s_addr = htonl(m);
    mask.sin_len = static_cast<uint8_t>(offsetof(sockaddr_in, sin_addr) +
                                        (prefix + 7) / 8);
    appendSockaddr(msg, &mask, mask.sin_len);
  }

  // 注意：appendSockaddr 可能触发 vector 重新分配，必须在追加完成后
  // 重新取指针写 rtm_msglen，不能复用前面的 hdr（悬垂指针）。
  reinterpret_cast<rt_msghdr *>(msg.data())->rtm_msglen =
      static_cast<u_short>(msg.size());
  return msg;
}

} // namespace detail

namespace {

class RouteManagerMacOS final : public RouteManager {
public:
  explicit RouteManagerMacOS(uint16_t ifindex) : ifindex_(ifindex) {}

  bool addRoute(const Ipv4Route &route, std::string *error) override {
    const int32_t pid = static_cast<int32_t>(::getpid());
    int32_t seq = nextSeq();
    auto msg = detail::buildRouteMessage(RTM_ADD, pid, seq, route, ifindex_);
    if (msg.empty()) {
      *error = "Invalid route " + route.network + "/" + route.netmask;
      return false;
    }
    int routeErrno = 0;
    if (sendRouteMessage(msg, pid, seq, &routeErrno, error)) {
      return true;
    }
    if (routeErrno != EEXIST) {
      return false;
    }
    // 等价路由已存在：以 RTM_CHANGE 重试一次（幂等收敛）
    error->clear();
    seq = nextSeq();
    msg = detail::buildRouteMessage(RTM_CHANGE, pid, seq, route, ifindex_);
    return sendRouteMessage(msg, pid, seq, &routeErrno, error);
  }

  bool removeRoute(const Ipv4Route &route, std::string *error) override {
    const int32_t pid = static_cast<int32_t>(::getpid());
    const int32_t seq = nextSeq();
    const auto msg =
        detail::buildRouteMessage(RTM_DELETE, pid, seq, route, ifindex_);
    if (msg.empty()) {
      *error = "Invalid route " + route.network + "/" + route.netmask;
      return false;
    }
    int routeErrno = 0;
    if (sendRouteMessage(msg, pid, seq, &routeErrno, error)) {
      return true;
    }
    if (routeErrno == ESRCH) {
      error->clear();
      return true; // 幂等：不存在视为成功
    }
    return false;
  }

private:
  uint16_t ifindex_;
};

} // namespace

std::unique_ptr<RouteManager> createRouteManager(const std::string &ifname) {
  const unsigned index = if_nametoindex(ifname.c_str());
  if (index == 0) {
    return nullptr;
  }
  return std::make_unique<RouteManagerMacOS>(static_cast<uint16_t>(index));
}

} // namespace tun

#endif // __APPLE__
