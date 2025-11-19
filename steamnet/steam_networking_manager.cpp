#include "steam_networking_manager.h"
#include <iostream>
#include <algorithm>

SteamNetworkingManager *SteamNetworkingManager::instance = nullptr;

SteamNetworkingConfigValue_t g_connectionConfig;

// Static callback function
void SteamNetworkingManager::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *pInfo)
{
    if (instance)
    {
        instance->handleConnectionStatusChanged(pInfo);
    }
}

SteamFriendsCallbacks::SteamFriendsCallbacks(SteamNetworkingManager *manager) : manager_(manager)
{
    std::cout << "SteamFriendsCallbacks constructor called" << std::endl;
}

void SteamFriendsCallbacks::OnGameRichPresenceJoinRequested(GameRichPresenceJoinRequested_t *pCallback)
{
    std::cout << "GameRichPresenceJoinRequested received" << std::endl;
    if (manager_)
    {
        const char *connectStr = pCallback->m_rgchConnect;
        std::cout << "Connect string: '" << (connectStr ? connectStr : "null") << "'" << std::endl;
        if (connectStr && connectStr[0] != '\0')
        {
            try
            {
                uint64 id = std::stoull(connectStr);
                std::string str = connectStr;
                std::cout << "Parsed ID: " << id << std::endl;
                if (str.find("7656119") == 0)
                {
                    // It's a Steam ID, join host directly
                    std::cout << "Parsed Steam ID: " << id << ", joining host" << std::endl;
                    if (!manager_->isHost() && !manager_->isConnected())
                    {
                        manager_->joinHost(id);
                        // Start TCP Server if dependencies are set
                        if (manager_->server_ && !(*manager_->server_))
                        {
                            *manager_->server_ = std::make_unique<TCPServer>(8888, manager_);
                            if (!(*manager_->server_)->start())
                            {
                                std::cerr << "Failed to start TCP server" << std::endl;
                            }
                        }
                    }
                    else
                    {
                        std::cout << "Already host or connected, ignoring join request" << std::endl;
                    }
                }
                else
                {
                    // Assume it's a lobby ID
                    CSteamID lobbySteamID(id);
                    std::cout << "Parsed lobby ID: " << id << std::endl;
                    if (!manager_->isHost() && !manager_->isConnected())
                    {
                        std::cout << "Joining lobby from invite: " << id << std::endl;
                        manager_->joinLobby(lobbySteamID);
                    }
                    else
                    {
                        std::cout << "Already host or connected, ignoring invite" << std::endl;
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Failed to parse connect string: " << connectStr << " error: " << e.what() << std::endl;
            }
        }
        else
        {
            std::cerr << "Empty connect string in join request" << std::endl;
        }
    }
    else
    {
        std::cout << "Manager is null" << std::endl;
    }
}

void SteamFriendsCallbacks::OnGameLobbyJoinRequested(GameLobbyJoinRequested_t *pCallback)
{
    std::cout << "GameLobbyJoinRequested received" << std::endl;
    if (manager_)
    {
        CSteamID lobbyID = pCallback->m_steamIDLobby;
        std::cout << "Lobby ID: " << lobbyID.ConvertToUint64() << std::endl;
        if (!manager_->isHost() && !manager_->isConnected())
        {
            std::cout << "Joining lobby from request: " << lobbyID.ConvertToUint64() << std::endl;
            manager_->joinLobby(lobbyID);
        }
        else
        {
            std::cout << "Already host or connected, ignoring lobby join request" << std::endl;
        }
    }
    else
    {
        std::cout << "Manager is null" << std::endl;
    }
}

SteamNetworkingManager::SteamNetworkingManager()
    : m_pInterface(nullptr), hListenSock(k_HSteamListenSocket_Invalid), g_isHost(false), g_isClient(false), g_isConnected(false),
      g_hConnection(k_HSteamNetConnection_Invalid), g_retryCount(0), g_currentVirtualPort(0),
      io_context_(nullptr), clientMap_(nullptr), clientMutex_(nullptr), server_(nullptr), localPort_(nullptr), messageHandler_(nullptr),
      steamFriendsCallbacks(nullptr), steamMatchmakingCallbacks(nullptr), currentLobby(k_steamIDNil)
{
    // Initialize connection config
    g_connectionConfig[0].SetInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_All);
    g_connectionConfig[1].SetInt32(k_ESteamNetworkingConfig_P2P_Transport_SDR_Penalty, 300);
    std::cout << "Initialized SteamNetworkingManager" << std::endl;
}

SteamNetworkingManager::~SteamNetworkingManager()
{
    stopMessageHandler();
    delete messageHandler_;
    delete steamFriendsCallbacks;
    delete steamMatchmakingCallbacks;
    shutdown();
}

bool SteamNetworkingManager::initialize()
{
    instance = this;
    if (!SteamAPI_Init())
    {
        std::cerr << "Failed to initialize Steam API" << std::endl;
        return false;
    }

    // Create callbacks after Steam API init
    steamFriendsCallbacks = new SteamFriendsCallbacks(this);
    steamMatchmakingCallbacks = new SteamMatchmakingCallbacks(this);

    SteamNetworkingUtils()->InitRelayNetworkAccess();
    SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(OnSteamNetConnectionStatusChanged);

    m_pInterface = SteamNetworkingSockets();

    // Clear Rich Presence on startup
    SteamFriends()->ClearRichPresence();
    std::cout << "Cleared Rich Presence on startup" << std::endl;

    // Check if callbacks are registered
    std::cout << "Steam API initialized" << std::endl;

    // Get friends list
    int friendCount = SteamFriends()->GetFriendCount(k_EFriendFlagAll);
    for (int i = 0; i < friendCount; ++i)
    {
        CSteamID friendID = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagAll);
        const char *name = SteamFriends()->GetFriendPersonaName(friendID);
        friendsList.push_back({friendID, name});
    }

    return true;
}

void SteamNetworkingManager::shutdown()
{
    leaveLobby();
    if (g_hConnection != k_HSteamNetConnection_Invalid)
    {
        m_pInterface->CloseConnection(g_hConnection, 0, nullptr, false);
    }
    if (hListenSock != k_HSteamListenSocket_Invalid)
    {
        m_pInterface->CloseListenSocket(hListenSock);
    }
    // Clear Rich Presence on shutdown
    SteamFriends()->ClearRichPresence();
    SteamAPI_Shutdown();
}

bool SteamNetworkingManager::createLobby()
{
    SteamAPICall_t hSteamAPICall = SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, 4);
    if (hSteamAPICall == k_uAPICallInvalid)
    {
        std::cerr << "Failed to create lobby" << std::endl;
        return false;
    }
    // Call result will be handled by callback
    return true;
}

void SteamNetworkingManager::leaveLobby()
{
    if (currentLobby != k_steamIDNil)
    {
        SteamMatchmaking()->LeaveLobby(currentLobby);
        currentLobby = k_steamIDNil;
    }
}

bool SteamNetworkingManager::searchLobbies()
{
    lobbies.clear();
    SteamAPICall_t hSteamAPICall = SteamMatchmaking()->RequestLobbyList();
    if (hSteamAPICall == k_uAPICallInvalid)
    {
        std::cerr << "Failed to request lobby list" << std::endl;
        return false;
    }
    // Results will be handled by callback
    return true;
}

bool SteamNetworkingManager::joinLobby(CSteamID lobbyID)
{
    if (SteamMatchmaking()->JoinLobby(lobbyID) != k_EResultOK)
    {
        std::cerr << "Failed to join lobby" << std::endl;
        return false;
    }
    // Connection will be handled by callback
    return true;
}

bool SteamNetworkingManager::startHosting()
{
    if (!createLobby())
    {
        return false;
    }
    hListenSock = m_pInterface->CreateListenSocketP2P(0, 0, g_connectionConfig);
    if (hListenSock != k_HSteamListenSocket_Invalid)
    {
        g_isHost = true;
        std::cout << "Created listen socket for hosting game room" << std::endl;
        // Rich Presence is set in OnLobbyCreated callback
        return true;
    }
    else
    {
        std::cerr << "Failed to create listen socket for hosting" << std::endl;
        leaveLobby();
        return false;
    }
}

void SteamNetworkingManager::stopHosting()
{
    if (hListenSock != k_HSteamListenSocket_Invalid)
    {
        m_pInterface->CloseListenSocket(hListenSock);
        hListenSock = k_HSteamListenSocket_Invalid;
    }
    leaveLobby();
    g_isHost = false;
}

bool SteamNetworkingManager::joinHost(uint64 hostID)
{
    CSteamID hostSteamID(hostID);
    g_isClient = true;
    g_hostSteamID = hostSteamID;
    g_retryCount = 0;
    g_currentVirtualPort = 0;
    SteamNetworkingIdentity identity;
    identity.SetSteamID(hostSteamID);
    g_hConnection = m_pInterface->ConnectP2P(identity, g_currentVirtualPort, 2, g_connectionConfig);
    if (g_hConnection != k_HSteamNetConnection_Invalid)
    {
        std::cout << "Attempting to connect to host " << hostSteamID.ConvertToUint64() << " with virtual port " << g_currentVirtualPort << std::endl;
        return true;
    }
    else
    {
        std::cerr << "Failed to initiate connection" << std::endl;
        return false;
    }
}

void SteamNetworkingManager::setMessageHandlerDependencies(boost::asio::io_context &io_context, std::map<HSteamNetConnection, std::shared_ptr<TCPClient>> &clientMap, std::mutex &clientMutex, std::unique_ptr<TCPServer> &server, int &localPort)
{
    io_context_ = &io_context;
    clientMap_ = &clientMap;
    clientMutex_ = &clientMutex;
    server_ = &server;
    localPort_ = &localPort;
    messageHandler_ = new SteamMessageHandler(io_context, m_pInterface, connections, clientMap, clientMutex, connectionsMutex, server, g_isHost, localPort);
}

void SteamNetworkingManager::startMessageHandler()
{
    if (messageHandler_)
    {
        messageHandler_->start();
    }
}

void SteamNetworkingManager::stopMessageHandler()
{
    if (messageHandler_)
    {
        messageHandler_->stop();
    }
}

void SteamNetworkingManager::update()
{
    std::lock_guard<std::mutex> lock(connectionsMutex);
    for (auto &pair : userMap)
    {
        HSteamNetConnection conn = pair.first;
        UserInfo &userInfo = pair.second;
        SteamNetConnectionInfo_t info;
        SteamNetConnectionRealTimeStatus_t status;
        if (m_pInterface->GetConnectionInfo(conn, &info) && m_pInterface->GetConnectionRealTimeStatus(conn, &status, 0, nullptr))
        {
            userInfo.ping = status.m_nPing;
            userInfo.isRelay = (info.m_idPOPRelay != 0);
        }
    }
}

void SteamNetworkingManager::handleConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *pInfo)
{
    std::lock_guard<std::mutex> lock(connectionsMutex);
    std::cout << "Connection status changed: " << pInfo->m_info.m_eState << " for connection " << pInfo->m_hConn << std::endl;
    if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
    {
        std::cout << "Connection failed: " << pInfo->m_info.m_szEndDebug << std::endl;
    }
    if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_None && pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting)
    {
        m_pInterface->AcceptConnection(pInfo->m_hConn);
        connections.push_back(pInfo->m_hConn);
        g_hConnection = pInfo->m_hConn;
        g_isConnected = true;
        std::cout << "Accepted incoming connection from " << pInfo->m_info.m_identityRemote.GetSteamID().ConvertToUint64() << std::endl;
        // Add user info
        SteamNetConnectionInfo_t info;
        SteamNetConnectionRealTimeStatus_t status;
        if (m_pInterface->GetConnectionInfo(pInfo->m_hConn, &info) && m_pInterface->GetConnectionRealTimeStatus(pInfo->m_hConn, &status, 0, nullptr))
        {
            UserInfo userInfo;
            userInfo.steamID = pInfo->m_info.m_identityRemote.GetSteamID();
            userInfo.name = SteamFriends()->GetFriendPersonaName(userInfo.steamID);
            userInfo.ping = status.m_nPing;
            userInfo.isRelay = (info.m_idPOPRelay != 0);
            userMap[pInfo->m_hConn] = userInfo;
            std::cout << "Incoming connection details: ping=" << status.m_nPing << "ms, relay=" << (info.m_idPOPRelay != 0 ? "yes" : "no") << std::endl;
        }
    }
    else if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connecting && pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected)
    {
        g_isConnected = true;
        std::cout << "Connected to host" << std::endl;
        // Add user info
        SteamNetConnectionInfo_t info;
        SteamNetConnectionRealTimeStatus_t status;
        if (m_pInterface->GetConnectionInfo(pInfo->m_hConn, &info) && m_pInterface->GetConnectionRealTimeStatus(pInfo->m_hConn, &status, 0, nullptr))
        {
            UserInfo userInfo;
            userInfo.steamID = pInfo->m_info.m_identityRemote.GetSteamID();
            userInfo.name = SteamFriends()->GetFriendPersonaName(userInfo.steamID);
            userInfo.ping = status.m_nPing;
            userInfo.isRelay = (info.m_idPOPRelay != 0);
            userMap[pInfo->m_hConn] = userInfo;
            std::cout << "Outgoing connection details: ping=" << status.m_nPing << "ms, relay=" << (info.m_idPOPRelay != 0 ? "yes" : "no") << std::endl;
        }
    }
    else if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer || pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
    {
        g_isConnected = false;
        g_hConnection = k_HSteamNetConnection_Invalid;
        // Remove from connections
        auto it = std::find(connections.begin(), connections.end(), pInfo->m_hConn);
        if (it != connections.end())
        {
            connections.erase(it);
        }
        userMap.erase(pInfo->m_hConn);
        std::cout << "Connection closed" << std::endl;

        // Retry if client
        if (g_isClient && !g_isConnected && g_retryCount < MAX_RETRIES)
        {
            g_retryCount++;
            g_currentVirtualPort++;
            SteamNetworkingIdentity identity;
            identity.SetSteamID(g_hostSteamID);
            HSteamNetConnection newConn = m_pInterface->ConnectP2P(identity, g_currentVirtualPort, 2, g_connectionConfig);
            if (newConn != k_HSteamNetConnection_Invalid)
            {
                g_hConnection = newConn;
                std::cout << "Retrying connection attempt " << g_retryCount << " with virtual port " << g_currentVirtualPort << std::endl;
            }
            else
            {
                std::cerr << "Failed to initiate retry connection" << std::endl;
            }
        }
    }
}

SteamMatchmakingCallbacks::SteamMatchmakingCallbacks(SteamNetworkingManager *manager) : manager_(manager) {}

void SteamMatchmakingCallbacks::OnLobbyCreated(LobbyCreated_t *pCallback)
{
    if (pCallback->m_eResult == k_EResultOK)
    {
        manager_->currentLobby = pCallback->m_ulSteamIDLobby;
        std::cout << "Lobby created: " << manager_->currentLobby.ConvertToUint64() << std::endl;
        // Set Rich Presence with lobby ID
        std::string lobbyStr = std::to_string(manager_->currentLobby.ConvertToUint64());
        SteamFriends()->SetRichPresence("connect", lobbyStr.c_str());
        SteamFriends()->SetRichPresence("status", "主持游戏房间");
        SteamFriends()->SetRichPresence("steam_display", "#StatusWithConnectFormat");
        std::cout << "Set Rich Presence connect to: " << lobbyStr << std::endl;
    }
    else
    {
        std::cerr << "Failed to create lobby" << std::endl;
    }
}

void SteamMatchmakingCallbacks::OnLobbyListReceived(LobbyMatchList_t *pCallback)
{
    manager_->lobbies.clear();
    for (uint32 i = 0; i < pCallback->m_nLobbiesMatching; ++i)
    {
        CSteamID lobbyID = SteamMatchmaking()->GetLobbyByIndex(i);
        manager_->lobbies.push_back(lobbyID);
    }
    std::cout << "Received " << pCallback->m_nLobbiesMatching << " lobbies" << std::endl;
}

void SteamMatchmakingCallbacks::OnLobbyEntered(LobbyEnter_t *pCallback)
{
    if (pCallback->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess)
    {
        manager_->currentLobby = pCallback->m_ulSteamIDLobby;
        std::cout << "Entered lobby: " << pCallback->m_ulSteamIDLobby << std::endl;
        // Only join host if not the host
        if (!manager_->isHost())
        {
            CSteamID hostID = SteamMatchmaking()->GetLobbyOwner(pCallback->m_ulSteamIDLobby);
            if (manager_->joinHost(hostID.ConvertToUint64()))
            {
                // Start TCP Server if dependencies are set
                if (manager_->server_ && !(*manager_->server_))
                {
                    *manager_->server_ = std::make_unique<TCPServer>(8888, manager_);
                    if (!(*manager_->server_)->start())
                    {
                        std::cerr << "Failed to start TCP server" << std::endl;
                    }
                }
            }
        }
    }
    else
    {
        std::cerr << "Failed to enter lobby" << std::endl;
    }
}