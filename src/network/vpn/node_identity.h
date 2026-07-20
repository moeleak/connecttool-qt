#pragma once

#include "domain/strong_id.h"
#include "vpn_protocol.h"
#include <string>

class NodeIdentity {
public:
  static NodeID generate(connecttool::domain::PeerId peerId);
  static int compare(const NodeID &a, const NodeID &b);
  static bool hasPriority(const NodeID &a, const NodeID &b) {
    return compare(a, b) > 0;
  }
  static std::string toString(const NodeID &nodeId, bool full = false);
  static bool isEmpty(const NodeID &nodeId);
};
