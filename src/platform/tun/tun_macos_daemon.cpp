#include "tun_privileged_helper.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <grp.h>
#include <net/if.h>
#include <net/if_utun.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sys_domain.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

extern "C" char **environ;

namespace {
constexpr size_t kMaxRequestSize = 1024;

bool readLine(int fd, std::string *line) {
  line->clear();
  char buffer[256];
  while (line->size() < kMaxRequestSize) {
    const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
    if (received == 0) {
      return false;
    }
    if (received < 0) {
      if (errno == EINTR) {
        continue;
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
  return false;
}

bool sendAll(int fd, const std::string &data) {
  size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t sent =
        ::send(fd, data.data() + offset, data.size() - offset, 0);
    if (sent <= 0) {
      return false;
    }
    offset += static_cast<size_t>(sent);
  }
  return true;
}

bool sendResponse(int fd, const std::string &message) {
  std::string line = message;
  if (line.empty() || line.back() != '\n') {
    line += "\n";
  }
  return sendAll(fd, line);
}

bool sendResponseWithFd(int fd, const std::string &message, int sendFd) {
  std::string line = message;
  if (line.empty() || line.back() != '\n') {
    line += "\n";
  }

  iovec iov {};
  iov.iov_base = const_cast<char *>(line.data());
  iov.iov_len = line.size();

  char cmsgBuffer[CMSG_SPACE(sizeof(int))];
  std::memset(cmsgBuffer, 0, sizeof(cmsgBuffer));

  msghdr msg {};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsgBuffer;
  msg.msg_controllen = sizeof(cmsgBuffer);

  cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  std::memcpy(CMSG_DATA(cmsg), &sendFd, sizeof(int));

  return ::sendmsg(fd, &msg, 0) >= 0;
}

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

bool validIfName(const std::string &name) {
  if (name.size() <= 4) {
    return false;
  }
  if (name.rfind("utun", 0) != 0) {
    return false;
  }
  for (size_t i = 4; i < name.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
      return false;
    }
  }
  return true;
}

int maskToPrefix(const std::string &mask) {
  in_addr addr {};
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

std::unordered_map<std::string, std::string>
parseArgs(const std::string &line, std::string *verb) {
  std::unordered_map<std::string, std::string> args;
  std::istringstream iss(line);
  if (!(iss >> *verb)) {
    verb->clear();
    return args;
  }
  std::string token;
  while (iss >> token) {
    const auto eq = token.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    args[token.substr(0, eq)] = token.substr(eq + 1);
  }
  return args;
}

bool parseInt(const std::string &text, int *out) {
  if (!out || text.empty()) {
    return false;
  }
  try {
    size_t pos = 0;
    int value = std::stoi(text, &pos);
    if (pos != text.size()) {
      return false;
    }
    *out = value;
    return true;
  } catch (...) {
    return false;
  }
}

bool runCommand(const std::vector<std::string> &args, std::string *error) {
  if (args.empty()) {
    if (error) {
      *error = "Empty command";
    }
    return false;
  }
  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (const auto &arg : args) {
    argv.push_back(const_cast<char *>(arg.c_str()));
  }
  argv.push_back(nullptr);

  pid_t pid = 0;
  const int spawnResult = ::posix_spawn(&pid, args[0].c_str(), nullptr, nullptr,
                                       argv.data(), environ);
  if (spawnResult != 0) {
    if (error) {
      *error = std::string("posix_spawn failed: ") +
               std::strerror(spawnResult);
    }
    return false;
  }

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    if (error) {
      *error = std::string("waitpid failed: ") + std::strerror(errno);
    }
    return false;
  }

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return true;
  }
  if (error) {
    *error = "Command failed";
  }
  return false;
}

bool runIfconfig(const std::vector<std::string> &args, std::string *error) {
  std::vector<std::string> cmd;
  cmd.reserve(args.size() + 1);
  cmd.emplace_back("/sbin/ifconfig");
  cmd.insert(cmd.end(), args.begin(), args.end());
  return runCommand(cmd, error);
}

bool runRoute(const std::vector<std::string> &args, std::string *error) {
  std::vector<std::string> cmd;
  cmd.reserve(args.size() + 1);
  cmd.emplace_back("/sbin/route");
  cmd.insert(cmd.end(), args.begin(), args.end());
  return runCommand(cmd, error);
}

bool openUtun(int *outFd, std::string *ifname, std::string *error) {
  const int fd = ::socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
  if (fd < 0) {
    if (error) {
      *error = "Failed to create utun socket";
    }
    return false;
  }

  ctl_info info {};
  std::strncpy(info.ctl_name, UTUN_CONTROL_NAME, sizeof(info.ctl_name));
  if (::ioctl(fd, CTLIOCGINFO, &info) == -1) {
    if (error) {
      *error = "CTLIOCGINFO failed";
    }
    ::close(fd);
    return false;
  }

  sockaddr_ctl addr {};
  addr.sc_len = sizeof(addr);
  addr.sc_family = AF_SYSTEM;
  addr.ss_sysaddr = AF_SYS_CONTROL;
  addr.sc_id = info.ctl_id;
  addr.sc_unit = 0;

  if (::connect(fd, reinterpret_cast<struct sockaddr *>(&addr),
                sizeof(addr)) == -1) {
    if (error) {
      *error = "connect utun failed";
    }
    ::close(fd);
    return false;
  }

  char nameBuf[IFNAMSIZ] = {};
  socklen_t nameLen = sizeof(nameBuf);
  if (getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, nameBuf, &nameLen) < 0) {
    if (error) {
      *error = "getsockopt(UTUN_OPT_IFNAME) failed";
    }
    ::close(fd);
    return false;
  }

  if (ifname) {
    *ifname = nameBuf;
  }
  if (outFd) {
    *outFd = fd;
  }
  return true;
}

bool configureSocketPermissions(const char *path) {
  if (!path) {
    return false;
  }
  gid_t gid = 0;
  bool hasAdminGroup = false;
  if (group *grp = ::getgrnam("admin")) {
    gid = grp->gr_gid;
    hasAdminGroup = true;
  }
  if (::chown(path, 0, gid) < 0) {
    syslog(LOG_WARNING, "chown(%s) failed: %s", path, std::strerror(errno));
  }
  const mode_t mode = hasAdminGroup ? 0660 : 0600;
  if (::chmod(path, mode) < 0) {
    syslog(LOG_WARNING, "chmod(%s) failed: %s", path, std::strerror(errno));
  }
  return true;
}

void handleClient(int fd) {
  std::string line;
  if (!readLine(fd, &line)) {
    return;
  }
  std::string verb;
  const auto args = parseArgs(line, &verb);
  if (verb == "PING") {
    sendResponse(fd, "OK");
    return;
  }
  if (verb == "OPEN") {
    const std::string name = args.count("name") ? args.at("name") : "";
    const std::string mtuText = args.count("mtu") ? args.at("mtu") : "0";
    int mtu = 0;
    if (!validName(name) || !parseInt(mtuText, &mtu) || mtu < 0) {
      sendResponse(fd, "ERR invalid OPEN arguments");
      return;
    }
    int tunFd = -1;
    std::string ifname;
    std::string error;
    if (!openUtun(&tunFd, &ifname, &error)) {
      sendResponse(fd, "ERR " + error);
      return;
    }
    if (mtu > 0) {
      if (!runIfconfig({ifname, "mtu", std::to_string(mtu)}, &error)) {
        ::close(tunFd);
        sendResponse(fd, "ERR " + error);
        return;
      }
    }
    const bool ok = sendResponseWithFd(fd, "OK if=" + ifname, tunFd);
    ::close(tunFd);
    if (!ok) {
      syslog(LOG_ERR, "Failed to send utun fd");
    }
    return;
  }
  if (verb == "SET_IP") {
    const std::string ifname = args.count("if") ? args.at("if") : "";
    const std::string ip = args.count("ip") ? args.at("ip") : "";
    const std::string mask = args.count("mask") ? args.at("mask") : "";
    if (!validIfName(ifname) || !validAddress(ip) || !validAddress(mask)) {
      sendResponse(fd, "ERR invalid SET_IP arguments");
      return;
    }
    std::string error;
    if (!runIfconfig({ifname, ip, ip, "netmask", mask, "up"}, &error)) {
      sendResponse(fd, "ERR " + error);
      return;
    }
    sendResponse(fd, "OK");
    return;
  }
  if (verb == "SET_MTU") {
    const std::string ifname = args.count("if") ? args.at("if") : "";
    const std::string mtuText = args.count("mtu") ? args.at("mtu") : "";
    int mtu = 0;
    if (!validIfName(ifname) || !parseInt(mtuText, &mtu) || mtu <= 0) {
      sendResponse(fd, "ERR invalid SET_MTU arguments");
      return;
    }
    std::string error;
    if (!runIfconfig({ifname, "mtu", std::to_string(mtu)}, &error)) {
      sendResponse(fd, "ERR " + error);
      return;
    }
    sendResponse(fd, "OK");
    return;
  }
  if (verb == "SET_UP") {
    const std::string ifname = args.count("if") ? args.at("if") : "";
    const std::string upText = args.count("up") ? args.at("up") : "";
    if (!validIfName(ifname) || (upText != "0" && upText != "1")) {
      sendResponse(fd, "ERR invalid SET_UP arguments");
      return;
    }
    const bool up = (upText == "1");
    std::string error;
    if (!runIfconfig({ifname, up ? "up" : "down"}, &error)) {
      sendResponse(fd, "ERR " + error);
      return;
    }
    sendResponse(fd, "OK");
    return;
  }
  if (verb == "ADD_ROUTE") {
    const std::string ifname = args.count("if") ? args.at("if") : "";
    const std::string network = args.count("net") ? args.at("net") : "";
    const std::string mask = args.count("mask") ? args.at("mask") : "";
    if (!validIfName(ifname) || !validAddress(network) ||
        !validAddress(mask)) {
      sendResponse(fd, "ERR invalid ADD_ROUTE arguments");
      return;
    }
    std::string error;
    const int prefix = maskToPrefix(mask);
    const std::string cidr =
        prefix > 0 ? network + "/" + std::to_string(prefix) : network;
    if (!runRoute({"-n", "add", "-net", cidr, "-interface", ifname}, &error)) {
      if (!runRoute({"-n", "change", "-net", cidr, "-interface", ifname},
                    &error)) {
        sendResponse(fd, "ERR " + error);
        return;
      }
    }
    sendResponse(fd, "OK");
    return;
  }
  sendResponse(fd, "ERR unknown command");
}
} // namespace

int main() {
  ::openlog("connecttool-tun-daemon", LOG_PID | LOG_CONS, LOG_DAEMON);

  const char *socketPath = tun::helperSocketPath();
  if (!socketPath || socketPath[0] == '\0') {
    syslog(LOG_ERR, "Helper socket path missing");
    return 1;
  }
  if (std::strlen(socketPath) >= sizeof(sockaddr_un::sun_path)) {
    syslog(LOG_ERR, "Helper socket path too long");
    return 1;
  }

  ::umask(0077);
  ::unlink(socketPath);

  const int listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenFd < 0) {
    syslog(LOG_ERR, "Failed to create helper socket: %s", std::strerror(errno));
    return 1;
  }
  int noSigPipe = 1;
  ::setsockopt(listenFd, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe,
               sizeof(noSigPipe));

  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);
  const auto addrLen = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + std::strlen(addr.sun_path) + 1);
  if (::bind(listenFd, reinterpret_cast<sockaddr *>(&addr), addrLen) < 0) {
    syslog(LOG_ERR, "Failed to bind helper socket: %s", std::strerror(errno));
    ::close(listenFd);
    return 1;
  }
  configureSocketPermissions(socketPath);

  if (::listen(listenFd, 16) < 0) {
    syslog(LOG_ERR, "Failed to listen on helper socket: %s",
           std::strerror(errno));
    ::close(listenFd);
    return 1;
  }

  while (true) {
    const int clientFd = ::accept(listenFd, nullptr, nullptr);
    if (clientFd < 0) {
      if (errno == EINTR) {
        continue;
      }
      syslog(LOG_ERR, "accept failed: %s", std::strerror(errno));
      continue;
    }
    int noSigPipeClient = 1;
    ::setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipeClient,
                 sizeof(noSigPipeClient));
    handleClient(clientFd);
    ::close(clientFd);
  }

  return 0;
}
