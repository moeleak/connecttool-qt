#ifndef STEAM_NETWORKING_MANAGER_H
#define STEAM_NETWORKING_MANAGER_H

#include "steam_message_handler.h"
#include <isteamnetworkingsockets.h>
#include <isteamnetworkingutils.h>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <steam_api.h>
#include <steamnetworkingtypes.h>
#include <vector>

// Forward declarations
class TCPServer;
class SteamNetworkingManager;
class SteamRoomManager;

// User info structure
struct UserInfo {
  CSteamID steamID;
  std::string name;
  int ping;
  bool isRelay;
};

class SteamNetworkingManager {
public:
  static SteamNetworkingManager *instance;
  SteamNetworkingManager();
  ~SteamNetworkingManager();

  bool initialize();
  void shutdown();

  // Joining
  bool joinHost(uint64 hostID);
  void disconnect();

  // Getters
  bool isHost() const { return g_isHost; }
  bool isClient() const { return g_isClient; }
  bool isConnected() const { return g_isConnected; }
  const std::vector<HSteamNetConnection> &getConnections() const {
    return connections;
  }
  std::mutex &getConnectionsMutex() { return connectionsMutex; }
  void closeConnectionToPeer(const CSteamID &peer);
  int getHostPing() const { return hostPing_; }
  int getConnectionPing(HSteamNetConnection conn) const;
  HSteamNetConnection getConnection() const { return g_hConnection; }
  ISteamNetworkingSockets *getInterface() const { return m_pInterface; }
  std::string getConnectionRelayInfo(HSteamNetConnection conn) const;
  int estimateRelayPingMs() const;
  void applyTransportPreference(int directPingMs, int relayPingMs);
  void setRoomManager(SteamRoomManager *roomManager) { roomManager_ = roomManager; }

  // For SteamRoomManager access
  std::unique_ptr<TCPServer> *&getServer() { return server_; }
  int *&getLocalPort() { return localPort_; }
  int getBindPort() const { return localBindPort_ ? *localBindPort_ : 8888; }
  boost::asio::io_context *&getIOContext() { return io_context_; }
  HSteamListenSocket &getListenSock() { return hListenSock; }
  ISteamNetworkingSockets *getInterface() { return m_pInterface; }
  bool &getIsHost() { return g_isHost; }

  void setMessageHandlerDependencies(boost::asio::io_context &io_context,
                                     std::unique_ptr<TCPServer> &server,
                                     int &localPort, int &localBindPort);

  // Message handler
  void startMessageHandler();
  void stopMessageHandler();
  SteamMessageHandler *getMessageHandler() { return messageHandler_; }

  // Update user info (ping, relay status)
  void update();

  // For callbacks
  void setHostSteamID(CSteamID id) { g_hostSteamID = id; }
  CSteamID getHostSteamID() const { return g_hostSteamID; }

private:
  bool connectToHostInternal(const CSteamID &hostSteamID, bool relayOnly);

  // Steam API
  ISteamNetworkingSockets *m_pInterface;

  // Hosting
  HSteamListenSocket hListenSock;
  bool g_isHost;
  bool g_isClient;
  bool g_isConnected;
  HSteamNetConnection g_hConnection;
  CSteamID g_hostSteamID;

  // Connections
  std::vector<HSteamNetConnection> connections;
  std::mutex connectionsMutex;
  int hostPing_; // Ping to host (for clients) or average ping (for host)

  // Connection config
  int g_retryCount;
  const int MAX_RETRIES = 3;
  int g_currentVirtualPort;

  // Message handler dependencies
  boost::asio::io_context *io_context_;
  std::unique_ptr<TCPServer> *server_;
  int *localPort_;
  int *localBindPort_;
  SteamMessageHandler *messageHandler_;
  SteamRoomManager *roomManager_;

  bool relayFallbackPending_;
  bool relayFallbackTried_;
  int consecutiveBadIceSamples_ = 0;
  std::chrono::steady_clock::time_point lastRelayFallback_;
  std::chrono::steady_clock::time_point lastIceTimeout_;

  // Callback
  static void OnSteamNetConnectionStatusChanged(
      SteamNetConnectionStatusChangedCallback_t *pInfo);
  void handleConnectionStatusChanged(
      SteamNetConnectionStatusChangedCallback_t *pInfo);

  friend class SteamRoomManager;
};

#endif // STEAM_NETWORKING_MANAGER_H
