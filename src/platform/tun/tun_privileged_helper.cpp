#include "tun_privileged_helper.h"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

namespace tun {
namespace {
constexpr const char *kDefaultHelperSocket = "/var/run/connecttool-tun.sock";

int connectToHelper(std::string *error) {
  const char *path = helperSocketPath();
  if (!path || path[0] == '\0') {
    if (error) {
      *error = "Helper socket path is empty";
    }
    return -1;
  }
  if (std::strlen(path) >= sizeof(sockaddr_un::sun_path)) {
    if (error) {
      *error = "Helper socket path too long";
    }
    return -1;
  }
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    if (error) {
      *error = std::string("Failed to create helper socket: ") +
               std::strerror(errno);
    }
    return -1;
  }
  int noSigPipe = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));

  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
  const auto addrLen = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + std::strlen(addr.sun_path) + 1);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), addrLen) < 0) {
    if (error) {
      *error = std::string("Failed to connect helper: ") +
               std::strerror(errno);
    }
    ::close(fd);
    return -1;
  }
  return fd;
}

bool sendAll(int fd, const std::string &data, std::string *error) {
  size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t sent =
        ::send(fd, data.data() + offset, data.size() - offset, 0);
    if (sent <= 0) {
      if (error) {
        *error = std::string("Failed to send helper request: ") +
                 std::strerror(errno);
      }
      return false;
    }
    offset += static_cast<size_t>(sent);
  }
  return true;
}

bool recvLine(int fd, std::string *line, std::string *error) {
  line->clear();
  char buffer[256];
  while (line->size() < 4096) {
    const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
    if (received == 0) {
      if (error) {
        *error = "Helper connection closed";
      }
      return false;
    }
    if (received < 0) {
      if (error) {
        *error = std::string("Failed to read helper response: ") +
                 std::strerror(errno);
      }
      return false;
    }
    line->append(buffer, static_cast<size_t>(received));
    const auto newline = line->find('\n');
    if (newline != std::string::npos) {
      line->erase(newline);
      return true;
    }
  }
  if (error) {
    *error = "Helper response too long";
  }
  return false;
}

bool recvLineWithFd(int fd, std::string *line, int *receivedFd,
                    std::string *error) {
  char buffer[512];
  char cmsgBuffer[CMSG_SPACE(sizeof(int))];
  std::memset(cmsgBuffer, 0, sizeof(cmsgBuffer));

  iovec iov {};
  iov.iov_base = buffer;
  iov.iov_len = sizeof(buffer) - 1;

  msghdr msg {};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsgBuffer;
  msg.msg_controllen = sizeof(cmsgBuffer);

  const ssize_t received = ::recvmsg(fd, &msg, 0);
  if (received <= 0) {
    if (error) {
      *error = std::string("Failed to read helper response: ") +
               std::strerror(errno);
    }
    return false;
  }
  buffer[received] = '\0';
  line->assign(buffer);
  const auto newline = line->find('\n');
  if (newline != std::string::npos) {
    line->erase(newline);
  }

  int outFd = -1;
  for (cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
       cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
        cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
      std::memcpy(&outFd, CMSG_DATA(cmsg), sizeof(int));
      break;
    }
  }
  if (receivedFd) {
    *receivedFd = outFd;
  }
  return true;
}

bool parseResponse(const std::string &line, std::string *payload,
                   std::string *error) {
  const bool okLine = (line == "OK") || line.rfind("OK ", 0) == 0;
  if (okLine) {
    if (payload) {
      if (line.size() > 2 && line[2] == ' ') {
        *payload = line.substr(3);
      } else {
        payload->clear();
      }
    }
    return true;
  }
  const bool errLine = (line == "ERR") || line.rfind("ERR ", 0) == 0;
  if (errLine) {
    if (error) {
      if (line.size() > 4 && line[3] == ' ') {
        *error = line.substr(4);
      } else {
        *error = "Helper error";
      }
    }
    return false;
  }
  if (error) {
    *error = "Unexpected helper response: " + line;
  }
  return false;
}

std::string extractValue(const std::string &payload,
                         const std::string &key) {
  std::istringstream iss(payload);
  std::string token;
  while (iss >> token) {
    const auto eq = token.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    if (token.substr(0, eq) == key) {
      return token.substr(eq + 1);
    }
  }
  return {};
}

bool helperCommand(const std::string &command, std::string *error) {
  int fd = connectToHelper(error);
  if (fd < 0) {
    return false;
  }
  std::string payload;
  const std::string cmdLine =
      (!command.empty() && command.back() == '\n') ? command : command + "\n";
  if (!sendAll(fd, cmdLine, error)) {
    ::close(fd);
    return false;
  }
  std::string line;
  bool ok = recvLine(fd, &line, error) && parseResponse(line, &payload, error);
  ::close(fd);
  return ok;
}
} // namespace

const char *helperSocketPath() {
  const char *overridePath = std::getenv("CONNECTTOOL_TUN_HELPER_SOCKET");
  if (overridePath && overridePath[0] != '\0') {
    return overridePath;
  }
  return kDefaultHelperSocket;
}

bool helperAvailable() {
  std::string error;
  int fd = connectToHelper(&error);
  if (fd < 0) {
    return false;
  }
  if (!sendAll(fd, "PING\n", &error)) {
    ::close(fd);
    return false;
  }
  std::string line;
  std::string payload;
  bool ok = recvLine(fd, &line, &error) && parseResponse(line, &payload, nullptr);
  ::close(fd);
  return ok;
}

bool helperOpen(const std::string &requestedName, int mtu,
                HelperOpenResult *result) {
  if (!result) {
    return false;
  }
  result->fd = -1;
  result->interfaceName.clear();
  result->error.clear();

  std::string error;
  int fd = connectToHelper(&error);
  if (fd < 0) {
    result->error = error.empty() ? "Helper not available" : error;
    return false;
  }
  std::ostringstream cmd;
  cmd << "OPEN name=" << requestedName << " mtu=" << mtu << "\n";
  if (!sendAll(fd, cmd.str(), &error)) {
    result->error = error;
    ::close(fd);
    return false;
  }
  std::string line;
  int receivedFd = -1;
  if (!recvLineWithFd(fd, &line, &receivedFd, &error)) {
    result->error = error;
    ::close(fd);
    return false;
  }
  std::string payload;
  if (!parseResponse(line, &payload, &error)) {
    result->error = error;
    if (receivedFd >= 0) {
      ::close(receivedFd);
    }
    ::close(fd);
    return false;
  }
  if (receivedFd < 0) {
    result->error = "Helper did not provide a TUN fd";
    ::close(fd);
    return false;
  }
  result->interfaceName = extractValue(payload, "if");
  if (result->interfaceName.empty()) {
    result->error = "Helper did not provide a device name";
    ::close(receivedFd);
    ::close(fd);
    return false;
  }
  result->fd = receivedFd;
  ::close(fd);
  return true;
}

bool helperSetIp(const std::string &ifname, const std::string &ip,
                 const std::string &netmask, std::string *error) {
  std::ostringstream cmd;
  cmd << "SET_IP if=" << ifname << " ip=" << ip << " mask=" << netmask;
  return helperCommand(cmd.str(), error);
}

bool helperSetMtu(const std::string &ifname, int mtu, std::string *error) {
  std::ostringstream cmd;
  cmd << "SET_MTU if=" << ifname << " mtu=" << mtu;
  return helperCommand(cmd.str(), error);
}

bool helperSetUp(const std::string &ifname, bool up, std::string *error) {
  std::ostringstream cmd;
  cmd << "SET_UP if=" << ifname << " up=" << (up ? 1 : 0);
  return helperCommand(cmd.str(), error);
}

bool helperAddRoute(const std::string &network, const std::string &netmask,
                    const std::string &ifname, std::string *error) {
  std::ostringstream cmd;
  cmd << "ADD_ROUTE if=" << ifname << " net=" << network
      << " mask=" << netmask;
  return helperCommand(cmd.str(), error);
}

} // namespace tun
