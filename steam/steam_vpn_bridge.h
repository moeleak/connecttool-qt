#pragma once

#include "../net/heartbeat_manager.h"
#include "../net/ip_negotiator.h"
#include "../net/vpn_protocol.h"
#include "../tun/tun_interface.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class SteamVpnNetworkingManager;

class SteamVpnBridge {
public:
  SteamVpnBridge(SteamVpnNetworkingManager *steamManager);
  ~SteamVpnBridge();

  bool start(const std::string &tunDeviceName = "",
             const std::string &virtualSubnet = "10.0.0.0",
             const std::string &subnetMask = "255.0.0.0", int mtu = 1400);
  void stop();

  bool isRunning() const { return running_; }

  std::string getLocalIP() const;
  std::string getTunDeviceName() const;
  std::map<uint32_t, RouteEntry> getRoutingTable() const;

  void handleVpnMessage(const uint8_t *data, size_t length,
                        CSteamID senderSteamID);
  void onUserJoined(CSteamID steamID);
  void onUserLeft(CSteamID steamID);
  // Force-send our current address/route to all peers (used after reconnect).
  void rebroadcastState();
  static std::string ipToString(uint32_t ip);

  struct Statistics {
    uint64_t packetsSent = 0;
    uint64_t packetsReceived = 0;
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint64_t packetsDropped = 0;
  };
  Statistics getStatistics() const;

private:
  void tunReadThread();

  static uint32_t stringToIp(const std::string &ipStr);
  static uint32_t extractDestIP(const uint8_t *packet, size_t length);
  static uint32_t extractSourceIP(const uint8_t *packet, size_t length);
  bool isBroadcastAddress(uint32_t ip) const;

  void sendVpnMessage(VpnMessageType type, const uint8_t *payload,
                      size_t payloadLength, CSteamID targetSteamID,
                      bool reliable = true);
  void broadcastVpnMessage(VpnMessageType type, const uint8_t *payload,
                           size_t payloadLength, bool reliable = true);

  void onNegotiationSuccess(uint32_t ipAddress, const NodeID &nodeId);
  void onNodeExpired(const NodeID &nodeId, uint32_t ipAddress);
  void updateRoute(const NodeID &nodeId, CSteamID steamId, uint32_t ipAddress,
                   const std::string &name);
  void removeRoute(uint32_t ipAddress);
  void broadcastRouteUpdate();
  void sendRouteUpdateTo(CSteamID targetSteamID);

  SteamVpnNetworkingManager *steamManager_;
  std::unique_ptr<tun::TunInterface> tunDevice_;
  std::atomic<bool> running_;
  std::unique_ptr<std::thread> tunReadThread_;

  std::map<uint32_t, RouteEntry> routingTable_;
  mutable std::mutex routingMutex_;

  uint32_t baseIP_;
  uint32_t subnetMask_;
  uint32_t localIP_;

  Statistics stats_;
  mutable std::mutex statsMutex_;

  IpNegotiator ipNegotiator_;
  HeartbeatManager heartbeatManager_;
};
