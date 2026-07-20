#pragma once

#include <string>

namespace tun {

struct HelperOpenResult {
  int fd = -1;
  std::string interfaceName;
  std::string error;
};

const char *helperSocketPath();
bool helperAvailable();
bool helperOpen(const std::string &requestedName, int mtu,
                HelperOpenResult *result);
bool helperSetIp(const std::string &ifname, const std::string &ip,
                 const std::string &netmask, std::string *error);
bool helperSetMtu(const std::string &ifname, int mtu, std::string *error);
bool helperSetUp(const std::string &ifname, bool up, std::string *error);
bool helperAddRoute(const std::string &network, const std::string &netmask,
                    const std::string &ifname, std::string *error);

} // namespace tun
