#ifdef __linux__

#include "tun_interface.h"
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
// Avoid including <net/if.h> with the kernel headers to prevent struct redefs
// on some glibc versions.
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tun {

namespace {
bool validInterfaceName(const std::string &name) {
  if (name.empty()) {
    return true;
  }
  for (char c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

int openControlSocket(std::string &error) {
  const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    error = "Failed to open control socket";
  }
  return sock;
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

class TunLinux : public TunInterface {
public:
  TunLinux() : fd_(-1), mtu_(1500) {}
  ~TunLinux() override { close(); }

  bool open(const std::string &deviceName, int mtu) override {
    if (fd_ >= 0) {
      lastError_ = "Already open";
      return false;
    }
    if (!validInterfaceName(deviceName)) {
      lastError_ = "Invalid interface name";
      return false;
    }

    fd_ = ::open("/dev/net/tun", O_RDWR);
    if (fd_ < 0) {
      lastError_ = "Failed to open /dev/net/tun";
      return false;
    }

    struct ifreq ifr {};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (!deviceName.empty()) {
      std::strncpy(ifr.ifr_name, deviceName.c_str(), IFNAMSIZ - 1);
    }

    if (ioctl(fd_, TUNSETIFF, &ifr) < 0) {
      lastError_ = "ioctl(TUNSETIFF) failed";
      ::close(fd_);
      fd_ = -1;
      return false;
    }

    name_ = ifr.ifr_name;
    mtu_ = mtu;
    if (mtu > 0) {
      set_mtu(mtu_);
    }
    return true;
  }

  void close() override {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool is_open() const override { return fd_ >= 0; }

  int read(uint8_t *buffer, size_t size) override {
    if (fd_ < 0) {
      return -1;
    }
    const ssize_t n = ::read(fd_, buffer, size);
    return n >= 0 ? static_cast<int>(n) : -1;
  }

  int write(const uint8_t *buffer, size_t size) override {
    if (fd_ < 0) {
      return -1;
    }
    const ssize_t n = ::write(fd_, buffer, size);
    return n >= 0 ? static_cast<int>(n) : -1;
  }

  std::string get_device_name() const override { return name_; }

  bool set_ip(const std::string &ip, const std::string &netmask) override {
    if (fd_ < 0) {
      lastError_ = "Interface not open";
      return false;
    }
    int sockErr = 0;
    std::string errMsg;
    const int sock = openControlSocket(errMsg);
    if (sock < 0) {
      lastError_ = errMsg;
      return false;
    }

    struct ifreq ifr {};
    std::strncpy(ifr.ifr_name, name_.c_str(), IFNAMSIZ - 1);

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
      lastError_ = "Invalid IP address";
      ::close(sock);
      return false;
    }
    std::memcpy(&ifr.ifr_addr, &addr, sizeof(sockaddr_in));
    sockErr = ioctl(sock, SIOCSIFADDR, &ifr);
    if (sockErr < 0) {
      lastError_ = "Failed to set IP address";
      ::close(sock);
      return false;
    }

    if (inet_pton(AF_INET, netmask.c_str(), &addr.sin_addr) != 1) {
      lastError_ = "Invalid netmask";
      ::close(sock);
      return false;
    }
    std::memcpy(&ifr.ifr_netmask, &addr, sizeof(sockaddr_in));
    sockErr = ioctl(sock, SIOCSIFNETMASK, &ifr);
    if (sockErr < 0) {
      lastError_ = "Failed to set netmask";
      ::close(sock);
      return false;
    }

    ::close(sock);
    return true;
  }

  bool add_route(const std::string &network,
                 const std::string &netmask) override {
    const int prefix = maskToPrefix(netmask);
    const std::string cidr =
        prefix > 0 ? network + "/" + std::to_string(prefix) : network;
    // Best-effort: add or replace a connected route via ip(8)
    std::string cmd = "ip route replace " + cidr + " dev " + name_ +
                      " proto static 2>/dev/null";
    if (::system(cmd.c_str()) == 0) {
      return true;
    }
    // Fallback: try classic route add/change, ignore if it already exists.
    cmd = "route add -net " + network + " netmask " + netmask + " dev " +
          name_ + " 2>/dev/null";
    if (::system(cmd.c_str()) == 0) {
      return true;
    }
    cmd = "route change -net " + network + " netmask " + netmask + " dev " +
          name_ + " 2>/dev/null";
    if (::system(cmd.c_str()) == 0) {
      return true;
    }
    lastError_ = "Failed to add route " + network;
    return false;
  }

  bool set_mtu(int mtu) override {
    if (fd_ < 0) {
      lastError_ = "Interface not open";
      return false;
    }
    int sockErr = 0;
    std::string errMsg;
    const int sock = openControlSocket(errMsg);
    if (sock < 0) {
      lastError_ = errMsg;
      return false;
    }
    struct ifreq ifr {};
    std::strncpy(ifr.ifr_name, name_.c_str(), IFNAMSIZ - 1);
    ifr.ifr_mtu = mtu;
    sockErr = ioctl(sock, SIOCSIFMTU, &ifr);
    ::close(sock);
    if (sockErr < 0) {
      lastError_ = "Failed to set MTU";
      return false;
    }
    mtu_ = mtu;
    return true;
  }

  bool set_up(bool up) override {
    if (fd_ < 0) {
      lastError_ = "Interface not open";
      return false;
    }
    int sockErr = 0;
    std::string errMsg;
    const int sock = openControlSocket(errMsg);
    if (sock < 0) {
      lastError_ = errMsg;
      return false;
    }
    struct ifreq ifr {};
    std::strncpy(ifr.ifr_name, name_.c_str(), IFNAMSIZ - 1);
    sockErr = ioctl(sock, SIOCGIFFLAGS, &ifr);
    if (sockErr < 0) {
      lastError_ = "Failed to read interface flags";
      ::close(sock);
      return false;
    }
    if (up) {
      ifr.ifr_flags |= IFF_UP;
    } else {
      ifr.ifr_flags &= ~IFF_UP;
    }
    sockErr = ioctl(sock, SIOCSIFFLAGS, &ifr);
    ::close(sock);
    if (sockErr < 0) {
      lastError_ = "Failed to set interface flags";
      return false;
    }
    return true;
  }

  bool set_non_blocking(bool nonBlocking) override {
    if (fd_ < 0) {
      lastError_ = "Interface not open";
      return false;
    }
    const int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
      lastError_ = "Failed to get flags";
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
};

std::unique_ptr<TunInterface> create_tun() {
  return std::make_unique<TunLinux>();
}

} // namespace tun

#endif // __linux__
