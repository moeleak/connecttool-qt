#ifndef STEAM_NETWORKING_MANAGER_H
#define STEAM_NETWORKING_MANAGER_H

#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <steam_api.h>
#include <isteamnetworkingsockets.h>
#include <isteamnetworkingutils.h>
#include <steamnetworkingtypes.h>
#include <isteammatchmaking.h>
#include "steam_message_handler.h"
#include "steam_room_manager.h"

// Forward declarations
class TCPClient;
class TCPServer;
class SteamNetworkingManager;

// User info structure
struct UserInfo {
    CSteamID steamID;
    std::string name;
    int ping;
    bool isRelay;
};

class SteamNetworkingManager {
public:
    static SteamNetworkingManager* instance;
    SteamNetworkingManager();
    ~SteamNetworkingManager();

    bool initialize();
    void shutdown();

    // Hosting
    bool startHosting();
    void stopHosting();

    // Lobby
    bool createLobby();
    void leaveLobby();
    bool searchLobbies();
    bool joinLobby(CSteamID lobbyID);
    const std::vector<CSteamID>& getLobbies() const;
    CSteamID getCurrentLobby() const;

    // Joining
    bool joinHost(uint64 hostID);
    void disconnect();

    // Getters
    bool isHost() const { return g_isHost; }
    bool isClient() const { return g_isClient; }
    bool isConnected() const { return g_isConnected; }
    const std::vector<std::pair<CSteamID, std::string>>& getFriendsList() const { return friendsList; }
    const std::map<HSteamNetConnection, UserInfo>& getUserMap() const { return userMap; }
    const std::vector<HSteamNetConnection>& getConnections() const { return connections; }
    HSteamNetConnection getConnection() const { return g_hConnection; }
    ISteamNetworkingSockets* getInterface() const { return m_pInterface; }

    void setMessageHandlerDependencies(boost::asio::io_context& io_context, std::map<HSteamNetConnection, std::shared_ptr<TCPClient>>& clientMap, std::mutex& clientMutex, std::unique_ptr<TCPServer>& server, int& localPort);

    // Message handler
    void startMessageHandler();
    void stopMessageHandler();

    // Update user info (ping, relay status)
    void update();

    // For callbacks
    void setHostSteamID(CSteamID id) { g_hostSteamID = id; }
    CSteamID getHostSteamID() const { return g_hostSteamID; }

    friend class SteamFriendsCallbacks;
    friend class SteamMatchmakingCallbacks;

private:
    // Steam API
    ISteamNetworkingSockets* m_pInterface;

    // Hosting
    HSteamListenSocket hListenSock;
    bool g_isHost;
    bool g_isClient;
    bool g_isConnected;
    HSteamNetConnection g_hConnection;
    CSteamID g_hostSteamID;

    // Connections
    std::vector<HSteamNetConnection> connections;
    std::map<HSteamNetConnection, UserInfo> userMap;
    std::mutex connectionsMutex;

    // Connection config
    int g_retryCount;
    const int MAX_RETRIES = 3;
    int g_currentVirtualPort;

    // Friends
    std::vector<std::pair<CSteamID, std::string>> friendsList;

    // Room manager
    SteamRoomManager* roomManager_;

    // Message handler dependencies
    boost::asio::io_context* io_context_;
    std::map<HSteamNetConnection, std::shared_ptr<TCPClient>>* clientMap_;
    std::mutex* clientMutex_;
    std::unique_ptr<TCPServer>* server_;
    int* localPort_;
    SteamMessageHandler* messageHandler_;

    // Callback
    static void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *pInfo);
    void handleConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *pInfo);

    friend class SteamRoomManager;
};

#endif // STEAM_NETWORKING_MANAGER_H