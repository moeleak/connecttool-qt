#include "node_identity.h"
#include "network/vpn/sha256.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

NodeID NodeIdentity::generate(connecttool::domain::PeerId peerId) {
  NodeID nodeId{};
  const uint64_t steamId64 = peerId.value();

  std::vector<std::byte> input;
  input.resize(sizeof(steamId64) + std::strlen(APP_SECRET_SALT));
  std::memcpy(input.data(), &steamId64, sizeof(steamId64));
  std::memcpy(input.data() + sizeof(steamId64), APP_SECRET_SALT,
              std::strlen(APP_SECRET_SALT));

  const auto hash = connecttool::network::detail::sha256(input);
  std::memcpy(nodeId.data(), hash.data(), NODE_ID_SIZE);
  return nodeId;
}

int NodeIdentity::compare(const NodeID &a, const NodeID &b) {
  for (std::size_t i = 0; i < NODE_ID_SIZE; ++i) {
    if (a[i] < b[i]) {
      return -1;
    }
    if (a[i] > b[i]) {
      return 1;
    }
  }
  return 0;
}

std::string NodeIdentity::toString(const NodeID &nodeId, bool full) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  const std::size_t len = full ? NODE_ID_SIZE : 8;
  for (std::size_t i = 0; i < len; ++i) {
    oss << std::setw(2) << static_cast<int>(nodeId[i]);
  }
  if (!full) {
    oss << "...";
  }
  return oss.str();
}

bool NodeIdentity::isEmpty(const NodeID &nodeId) {
  for (auto byte : nodeId) {
    if (byte != 0) {
      return false;
    }
  }
  return true;
}
