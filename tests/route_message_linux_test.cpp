#include "platform/route/route_manager.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <linux/rtnetlink.h>

using tun::Ipv4Route;
using tun::detail::buildRouteRequest;

namespace {
int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testNewRouteMulticast() {
  const Ipv4Route route{"224.0.0.0", "240.0.0.0"};
  const auto msg = buildRouteRequest(RTM_NEWROUTE, 42, route, 5);
  CHECK(!msg.empty());
  CHECK(msg.size() >= sizeof(nlmsghdr) + sizeof(rtmsg));

  const auto *nh = reinterpret_cast<const nlmsghdr *>(msg.data());
  CHECK(nh->nlmsg_len == msg.size());
  CHECK(nh->nlmsg_type == RTM_NEWROUTE);
  CHECK(nh->nlmsg_flags ==
        (NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE));
  CHECK(nh->nlmsg_seq == 42);

  const auto *rtm =
      reinterpret_cast<const rtmsg *>(msg.data() + sizeof(nlmsghdr));
  CHECK(rtm->rtm_family == AF_INET);
  CHECK(rtm->rtm_dst_len == 4);
  CHECK(rtm->rtm_table == RT_TABLE_MAIN);
  CHECK(rtm->rtm_protocol == RTPROT_STATIC);
  CHECK(rtm->rtm_scope == RT_SCOPE_LINK);
  CHECK(rtm->rtm_type == RTN_UNICAST);

  int remaining =
      static_cast<int>(msg.size()) - static_cast<int>(NLMSG_LENGTH(sizeof(rtmsg)));
  const rtattr *rta = reinterpret_cast<const rtattr *>(
      msg.data() + NLMSG_LENGTH(sizeof(rtmsg)));
  bool sawDst = false;
  bool sawOif = false;
  for (; RTA_OK(rta, remaining); rta = RTA_NEXT(rta, remaining)) {
    if (rta->rta_type == RTA_DST) {
      in_addr expected{};
      inet_pton(AF_INET, "224.0.0.0", &expected);
      CHECK(std::memcmp(RTA_DATA(rta), &expected, sizeof(expected)) == 0);
      sawDst = true;
    } else if (rta->rta_type == RTA_OIF) {
      uint32_t index = 0;
      std::memcpy(&index, RTA_DATA(rta), sizeof(index));
      CHECK(index == 5);
      sawOif = true;
    }
  }
  CHECK(sawDst);
  CHECK(sawOif);
}

void testDelRouteFlags() {
  const auto msg =
      buildRouteRequest(RTM_DELROUTE, 7, {"224.0.0.0", "240.0.0.0"}, 5);
  CHECK(!msg.empty());
  const auto *nh = reinterpret_cast<const nlmsghdr *>(msg.data());
  CHECK(nh->nlmsg_type == RTM_DELROUTE);
  CHECK(nh->nlmsg_flags == (NLM_F_REQUEST | NLM_F_ACK));
}

void testHostRoute() {
  const auto msg =
      buildRouteRequest(RTM_NEWROUTE, 1, {"10.0.0.7", "255.255.255.255"}, 3);
  CHECK(!msg.empty());
  const auto *rtm =
      reinterpret_cast<const rtmsg *>(msg.data() + sizeof(nlmsghdr));
  CHECK(rtm->rtm_dst_len == 32);
}

void testHostBitsMasked() {
  // 224.0.0.5/4 必须归一化为 224.0.0.0
  const auto msg =
      buildRouteRequest(RTM_NEWROUTE, 1, {"224.0.0.5", "240.0.0.0"}, 3);
  CHECK(!msg.empty());
  int remaining =
      static_cast<int>(msg.size()) - static_cast<int>(NLMSG_LENGTH(sizeof(rtmsg)));
  const rtattr *rta = reinterpret_cast<const rtattr *>(
      msg.data() + NLMSG_LENGTH(sizeof(rtmsg)));
  bool sawDst = false;
  for (; RTA_OK(rta, remaining); rta = RTA_NEXT(rta, remaining)) {
    if (rta->rta_type == RTA_DST) {
      in_addr expected{};
      inet_pton(AF_INET, "224.0.0.0", &expected);
      CHECK(std::memcmp(RTA_DATA(rta), &expected, sizeof(expected)) == 0);
      sawDst = true;
    }
  }
  CHECK(sawDst);
}

void testRejectsInvalidInput() {
  // 非连续掩码
  CHECK(buildRouteRequest(RTM_NEWROUTE, 1, {"224.0.0.0", "255.0.255.0"}, 3)
            .empty());
  // 非法地址
  CHECK(buildRouteRequest(RTM_NEWROUTE, 1, {"not-an-ip", "240.0.0.0"}, 3)
            .empty());
  // 非法消息类型
  CHECK(buildRouteRequest(RTM_GETROUTE, 1, {"224.0.0.0", "240.0.0.0"}, 3)
            .empty());
}
} // namespace

int main() {
  testNewRouteMulticast();
  testDelRouteFlags();
  testHostRoute();
  testHostBitsMasked();
  testRejectsInvalidInput();
  if (failures == 0) {
    std::printf("route_message_linux_test: all passed\n");
    return 0;
  }
  std::printf("route_message_linux_test: %d failure(s)\n", failures);
  return 1;
}
