#pragma once

#include "vpn_protocol.h"
#include <steam_api.h>
#include <string>

class NodeIdentity {
public:
  static NodeID generate(CSteamID steamID);
  static int compare(const NodeID &a, const NodeID &b);
  static bool hasPriority(const NodeID &a, const NodeID &b) {
    return compare(a, b) > 0;
  }
  static std::string toString(const NodeID &nodeId, bool full = false);
  static bool isEmpty(const NodeID &nodeId);
};
