#include "platform/route/route_manager.h"

#include <arpa/inet.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/socket.h>

using tun::Ipv4Route;
using tun::detail::buildRouteMessage;

namespace {
int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

constexpr size_t kRound = sizeof(uint32_t);
size_t roundup(size_t n) { return (n + kRound - 1) & ~(kRound - 1); }

void testAddMulticast() {
  const Ipv4Route route{"224.0.0.0", "240.0.0.0"};
  const auto msg = buildRouteMessage(RTM_ADD, 1234, 7, route, 9);
  CHECK(!msg.empty());
  CHECK(msg.size() > sizeof(rt_msghdr));

  const auto *hdr = reinterpret_cast<const rt_msghdr *>(msg.data());
  CHECK(hdr->rtm_msglen == msg.size());
  CHECK(hdr->rtm_version == RTM_VERSION);
  CHECK(hdr->rtm_type == RTM_ADD);
  CHECK(hdr->rtm_pid == 1234);
  CHECK(hdr->rtm_seq == 7);
  CHECK((hdr->rtm_flags & RTF_UP) != 0);
  CHECK((hdr->rtm_flags & RTF_STATIC) != 0);
  CHECK((hdr->rtm_flags & RTF_HOST) == 0);
  CHECK(hdr->rtm_addrs == (RTA_DST | RTA_GATEWAY | RTA_NETMASK));

  // 按 rtm_addrs 位序走查三个 sockaddr
  const uint8_t *p = msg.data() + sizeof(rt_msghdr);
  const uint8_t *end = msg.data() + msg.size();

  // RTA_DST
  CHECK(p + sizeof(sockaddr_in) <= end);
  const auto *dst = reinterpret_cast<const sockaddr_in *>(p);
  CHECK(dst->sin_family == AF_INET);
  in_addr expected{};
  inet_pton(AF_INET, "224.0.0.0", &expected);
  CHECK(std::memcmp(&dst->sin_addr, &expected, sizeof(expected)) == 0);
  p += roundup(dst->sin_len);

  // RTA_GATEWAY: sockaddr_dl
  CHECK(p + offsetof(sockaddr_dl, sdl_data) <= end);
  const auto *gw = reinterpret_cast<const sockaddr_dl *>(p);
  CHECK(gw->sdl_family == AF_LINK);
  CHECK(gw->sdl_index == 9);
  CHECK(gw->sdl_len == offsetof(sockaddr_dl, sdl_data));
  CHECK(gw->sdl_nlen == 0);
  p += roundup(gw->sdl_len);

  // RTA_NETMASK: 截断 sockaddr_in,240.0.0.0 只有 1 个有效字节
  CHECK(p < end);
  const auto *mask = reinterpret_cast<const sockaddr_in *>(p);
  CHECK(mask->sin_family == 0);
  CHECK(mask->sin_len == offsetof(sockaddr_in, sin_addr) + 1);
  const auto *maskBytes = reinterpret_cast<const uint8_t *>(&mask->sin_addr);
  CHECK(maskBytes[0] == 0xF0);
  p += roundup(mask->sin_len);

  CHECK(p == end); // 报文恰好消费完，无拖尾
}

void testHostRouteOmitsNetmask() {
  const auto msg =
      buildRouteMessage(RTM_ADD, 1, 1, {"10.0.0.7", "255.255.255.255"}, 3);
  CHECK(!msg.empty());
  const auto *hdr = reinterpret_cast<const rt_msghdr *>(msg.data());
  CHECK((hdr->rtm_flags & RTF_HOST) != 0);
  CHECK(hdr->rtm_addrs == (RTA_DST | RTA_GATEWAY));
}

void testChangeMessage() {
  // EEXIST 幂等降级路径：RTM_CHANGE 报文须被构造器接受且结构与 ADD 同形
  const Ipv4Route route{"224.0.0.0", "240.0.0.0"};
  const auto msg = buildRouteMessage(RTM_CHANGE, 1234, 8, route, 9);
  CHECK(!msg.empty());
  CHECK(msg.size() > sizeof(rt_msghdr));

  const auto *hdr = reinterpret_cast<const rt_msghdr *>(msg.data());
  CHECK(hdr->rtm_msglen == msg.size());
  CHECK(hdr->rtm_version == RTM_VERSION);
  CHECK(hdr->rtm_type == RTM_CHANGE);
  CHECK(hdr->rtm_pid == 1234);
  CHECK(hdr->rtm_seq == 8);
  CHECK((hdr->rtm_flags & RTF_UP) != 0);
  CHECK((hdr->rtm_flags & RTF_STATIC) != 0);
  CHECK((hdr->rtm_flags & RTF_HOST) == 0);
  CHECK(hdr->rtm_addrs == (RTA_DST | RTA_GATEWAY | RTA_NETMASK));

  // 与 ADD 相同的三段 sockaddr 走查，报文须恰好消费完
  const uint8_t *p = msg.data() + sizeof(rt_msghdr);
  const uint8_t *end = msg.data() + msg.size();

  // RTA_DST
  CHECK(p + sizeof(sockaddr_in) <= end);
  const auto *dst = reinterpret_cast<const sockaddr_in *>(p);
  CHECK(dst->sin_family == AF_INET);
  in_addr expected{};
  inet_pton(AF_INET, "224.0.0.0", &expected);
  CHECK(std::memcmp(&dst->sin_addr, &expected, sizeof(expected)) == 0);
  p += roundup(dst->sin_len);

  // RTA_GATEWAY: sockaddr_dl
  CHECK(p + offsetof(sockaddr_dl, sdl_data) <= end);
  const auto *gw = reinterpret_cast<const sockaddr_dl *>(p);
  CHECK(gw->sdl_family == AF_LINK);
  CHECK(gw->sdl_index == 9);
  CHECK(gw->sdl_len == offsetof(sockaddr_dl, sdl_data));
  p += roundup(gw->sdl_len);

  // RTA_NETMASK: 截断 sockaddr_in
  CHECK(p < end);
  const auto *mask = reinterpret_cast<const sockaddr_in *>(p);
  CHECK(mask->sin_family == 0);
  CHECK(mask->sin_len == offsetof(sockaddr_in, sin_addr) + 1);
  p += roundup(mask->sin_len);

  CHECK(p == end); // 报文恰好消费完，无拖尾
}

void testDeleteMessage() {
  const auto msg =
      buildRouteMessage(RTM_DELETE, 1, 2, {"224.0.0.0", "240.0.0.0"}, 9);
  CHECK(!msg.empty());
  const auto *hdr = reinterpret_cast<const rt_msghdr *>(msg.data());
  CHECK(hdr->rtm_type == RTM_DELETE);
  CHECK(hdr->rtm_seq == 2);
}

void testHostBitsMasked() {
  const auto msg =
      buildRouteMessage(RTM_ADD, 1, 1, {"224.0.0.5", "240.0.0.0"}, 3);
  CHECK(!msg.empty());
  const auto *dst = reinterpret_cast<const sockaddr_in *>(
      msg.data() + sizeof(rt_msghdr));
  in_addr expected{};
  inet_pton(AF_INET, "224.0.0.0", &expected);
  CHECK(std::memcmp(&dst->sin_addr, &expected, sizeof(expected)) == 0);
}

void testRejectsInvalidInput() {
  CHECK(buildRouteMessage(RTM_ADD, 1, 1, {"224.0.0.0", "255.0.255.0"}, 3)
            .empty()); // 非连续掩码
  CHECK(buildRouteMessage(RTM_ADD, 1, 1, {"bad", "240.0.0.0"}, 3).empty());
  CHECK(buildRouteMessage(RTM_GET, 1, 1, {"224.0.0.0", "240.0.0.0"}, 3)
            .empty());
}
} // namespace

int main() {
  testAddMulticast();
  testHostRouteOmitsNetmask();
  testChangeMessage();
  testDeleteMessage();
  testHostBitsMasked();
  testRejectsInvalidInput();
  if (failures == 0) {
    std::printf("route_message_macos_test: all passed\n");
    return 0;
  }
  std::printf("route_message_macos_test: %d failure(s)\n", failures);
  return 1;
}
