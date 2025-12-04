#pragma once

#include <mutex>
#include <set>
#include <steam_api.h>
#include <isteamnetworkingmessages.h>
#include <steamnetworkingtypes.h>
#include <string>

class VpnMessageHandler;
class SteamVpnBridge;

class SteamVpnNetworkingManager {
public:
  static constexpr int VPN_CHANNEL = 0;

  SteamVpnNetworkingManager();
  ~SteamVpnNetworkingManager();

  bool initialize();
  void shutdown();

  bool sendMessageToUser(CSteamID peerID, const void *data, uint32_t size,
                         int flags);
  void broadcastMessage(const void *data, uint32_t size, int flags);

  void addPeer(CSteamID peerID);
  void removePeer(CSteamID peerID);
  void clearPeers();
  void syncPeers(const std::set<CSteamID> &desiredPeers);
  std::set<CSteamID> getPeers() const;

  int getPeerPing(CSteamID peerID) const;
  bool isPeerConnected(CSteamID peerID) const;
  std::string getPeerConnectionType(CSteamID peerID) const;

  void startMessageHandler();
  void stopMessageHandler();

  void setVpnBridge(SteamVpnBridge *vpnBridge) { vpnBridge_ = vpnBridge; }
  SteamVpnBridge *getVpnBridge() { return vpnBridge_; }

  void handleIncomingVpnMessage(const uint8_t *data, size_t size,
                                CSteamID senderSteamID);

  void setHostSteamID(CSteamID id) { hostSteamID_ = id; }
  CSteamID getHostSteamID() const { return hostSteamID_; }

private:
  ISteamNetworkingMessages *messagesInterface_;
  std::set<CSteamID> peers_;
  mutable std::mutex peersMutex_;

  VpnMessageHandler *messageHandler_;
  SteamVpnBridge *vpnBridge_;
  CSteamID hostSteamID_;

  STEAM_CALLBACK(SteamVpnNetworkingManager, OnSessionRequest,
                 SteamNetworkingMessagesSessionRequest_t);
  STEAM_CALLBACK(SteamVpnNetworkingManager, OnSessionFailed,
                 SteamNetworkingMessagesSessionFailed_t);
};
