#ifdef __APPLE__

#include "tun_interface.h"
#include "tun_privileged_helper.h"
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <net/if.h>
#include <net/if_utun.h>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tun {
namespace {
bool validName(const std::string &name) {
  if (name.empty()) {
    return true;
  }
  for (char c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' &&
        c != '-') {
      return false;
    }
  }
  return true;
}

bool validAddress(const std::string &text) {
  if (text.empty()) {
    return false;
  }
  for (char c : text) {
    if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') {
      return false;
    }
  }
  return true;
}

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
    return -1;
  }
  return prefix;
}
} // namespace

class TunMacOS : public TunInterface {
public:
  TunMacOS() : fd_(-1), mtu_(1500), usingHelper_(false) {}
  ~TunMacOS() override { close(); }

  bool open(const std::string &deviceName, int mtu) override {
    if (!validName(deviceName)) {
      lastError_ = "Invalid interface name";
      return false;
    }
    if (fd_ >= 0) {
      lastError_ = "Already open";
      return false;
    }
    if (geteuid() != 0 && helperAvailable()) {
      HelperOpenResult result;
      if (!helperOpen(deviceName, mtu, &result)) {
        lastError_ =
            result.error.empty() ? "Helper open failed" : result.error;
        return false;
      }
      if (result.fd < 0 || result.interfaceName.empty()) {
        lastError_ = "Helper returned invalid TUN device";
        return false;
      }
      fd_ = result.fd;
      name_ = result.interfaceName;
      mtu_ = mtu;
      usingHelper_ = true;
      return true;
    }
    fd_ = ::socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd_ < 0) {
      lastError_ = "Failed to create utun socket";
      return false;
    }

    struct ctl_info info {};
    std::strncpy(info.ctl_name, UTUN_CONTROL_NAME, sizeof(info.ctl_name));
    if (ioctl(fd_, CTLIOCGINFO, &info) == -1) {
      lastError_ = "CTLIOCGINFO failed";
      ::close(fd_);
      fd_ = -1;
      return false;
    }

    struct sockaddr_ctl addr {};
    addr.sc_len = sizeof(addr);
    addr.sc_family = AF_SYSTEM;
    addr.ss_sysaddr = AF_SYS_CONTROL;
    addr.sc_id = info.ctl_id;
    addr.sc_unit = 0; // auto-select utunX

    if (::connect(fd_, reinterpret_cast<struct sockaddr *>(&addr),
                  sizeof(addr)) == -1) {
      lastError_ = "connect utun failed";
      ::close(fd_);
      fd_ = -1;
      return false;
    }

    char ifName[IFNAMSIZ] = {};
    socklen_t ifNameLen = sizeof(ifName);
    if (getsockopt(fd_, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifName, &ifNameLen) <
        0) {
      lastError_ = "getsockopt(UTUN_OPT_IFNAME) failed";
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    name_ = ifName;
    mtu_ = mtu;
    if (mtu_ > 0) {
      set_mtu(mtu_);
    }
    return true;
  }

  void close() override {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
    usingHelper_ = false;
  }

  bool is_open() const override { return fd_ >= 0; }

  int read(uint8_t *buffer, size_t size) override {
    if (fd_ < 0) {
      return -1;
    }
    if (size < sizeof(uint32_t)) {
      return -1;
    }
    const ssize_t n = ::read(fd_, buffer, size);
    if (n <= static_cast<ssize_t>(sizeof(uint32_t))) {
      return 0;
    }
    // Drop 4-byte address family
    const ssize_t payload = n - sizeof(uint32_t);
    std::memmove(buffer, buffer + sizeof(uint32_t),
                 static_cast<std::size_t>(payload));
    return static_cast<int>(payload);
  }

  int write(const uint8_t *buffer, size_t size) override {
    if (fd_ < 0) {
      return -1;
    }
    uint8_t frame[1500 + sizeof(uint32_t)];
    if (size + sizeof(uint32_t) > sizeof(frame)) {
      lastError_ = "Packet too large";
      return -1;
    }
    uint32_t family = htonl(AF_INET);
    std::memcpy(frame, &family, sizeof(family));
    std::memcpy(frame + sizeof(family), buffer, size);
    const ssize_t n =
        ::write(fd_, frame, size + sizeof(uint32_t));
    return n >= 0 ? static_cast<int>(n - sizeof(uint32_t)) : -1;
  }

  std::string get_device_name() const override { return name_; }

  bool set_ip(const std::string &ip, const std::string &netmask) override {
    if (!is_open()) {
      lastError_ = "Interface not open";
      return false;
    }
    if (!validAddress(ip) || !validAddress(netmask)) {
      lastError_ = "Invalid IP or netmask";
      return false;
    }
    if (usingHelper_) {
      std::string error;
      if (!helperSetIp(name_, ip, netmask, &error)) {
        lastError_ = error.empty() ? "Helper set_ip failed" : error;
        return false;
      }
      return true;
    }
    std::ostringstream cmd;
    // utun is point-to-point; set dstaddr same as local to satisfy ifconfig
    cmd << "/sbin/ifconfig " << name_ << " " << ip << " " << ip
        << " netmask " << netmask << " up";
    if (::system(cmd.str().c_str()) != 0) {
      lastError_ = "ifconfig failed";
      return false;
    }
    return true;
  }

  bool add_route(const std::string &network,
                 const std::string &netmask) override {
    const int prefix = maskToPrefix(netmask);
    const std::string cidr =
        prefix > 0 ? network + "/" + std::to_string(prefix) : network;
    if (usingHelper_) {
      std::string error;
      if (!helperAddRoute(network, netmask, name_, &error)) {
        lastError_ = error.empty() ? "Helper add_route failed" : error;
        return false;
      }
      return true;
    }
    // Point the subnet at this utun interface; best effort.
    std::ostringstream cmd;
    cmd << "/sbin/route -n add -net " << cidr << " -interface " << name_;
    if (::system(cmd.str().c_str()) == 0) {
      return true;
    }
    // Try to change existing route if add failed (e.g., already present).
    std::ostringstream cmdChange;
    cmdChange << "/sbin/route -n change -net " << cidr << " -interface "
              << name_;
    if (::system(cmdChange.str().c_str()) == 0) {
      return true;
    }
    lastError_ = "route add/change failed for " + network;
    return false;
  }

  bool set_mtu(int mtu) override {
    if (!is_open()) {
      lastError_ = "Interface not open";
      return false;
    }
    if (usingHelper_) {
      std::string error;
      if (!helperSetMtu(name_, mtu, &error)) {
        lastError_ = error.empty() ? "Helper set_mtu failed" : error;
        return false;
      }
      mtu_ = mtu;
      return true;
    }
    std::ostringstream cmd;
    cmd << "/sbin/ifconfig " << name_ << " mtu " << mtu;
    if (::system(cmd.str().c_str()) != 0) {
      lastError_ = "Failed to set MTU";
      return false;
    }
    mtu_ = mtu;
    return true;
  }

  bool set_up(bool up) override {
    if (!is_open()) {
      lastError_ = "Interface not open";
      return false;
    }
    if (usingHelper_) {
      std::string error;
      if (!helperSetUp(name_, up, &error)) {
        lastError_ = error.empty() ? "Helper set_up failed" : error;
        return false;
      }
      return true;
    }
    std::ostringstream cmd;
    cmd << "/sbin/ifconfig " << name_ << (up ? " up" : " down");
    if (::system(cmd.str().c_str()) != 0) {
      lastError_ = "Failed to change interface state";
      return false;
    }
    return true;
  }

  bool set_non_blocking(bool nonBlocking) override {
    if (!is_open()) {
      lastError_ = "Interface not open";
      return false;
    }
    const int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
      lastError_ = "Failed to read flags";
      return false;
    }
    if (fcntl(fd_, F_SETFL, nonBlocking ? flags | O_NONBLOCK
                                        : (flags & ~O_NONBLOCK)) < 0) {
      lastError_ = "Failed to set non-blocking";
      return false;
    }
    return true;
  }

  std::string get_last_error() const override { return lastError_; }

private:
  int fd_;
  std::string name_;
  std::string lastError_;
  int mtu_;
  bool usingHelper_;
};

std::unique_ptr<TunInterface> create_tun() {
  return std::make_unique<TunMacOS>();
}

} // namespace tun

#endif // __APPLE__
