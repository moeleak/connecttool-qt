#include "steam_networking_manager.h"
#include <iostream>
#include <algorithm>

SteamNetworkingManager *SteamNetworkingManager::instance = nullptr;

// Static callback function
void SteamNetworkingManager::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *pInfo)
{
    if (instance)
    {
        instance->handleConnectionStatusChanged(pInfo);
    }
}

SteamNetworkingManager::SteamNetworkingManager()
    : m_pInterface(nullptr), hListenSock(k_HSteamListenSocket_Invalid), g_isHost(false), g_isClient(false), g_isConnected(false),
      g_hConnection(k_HSteamNetConnection_Invalid),
      io_context_(nullptr), clientMap_(nullptr), clientMutex_(nullptr), server_(nullptr), localPort_(nullptr), messageHandler_(nullptr),
      roomManager_(nullptr)
{
    std::cout << "Initialized SteamNetworkingManager" << std::endl;
}

SteamNetworkingManager::~SteamNetworkingManager()
{
    stopMessageHandler();
    delete messageHandler_;
    delete roomManager_;
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

    // 【新增】开启详细日志
    SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Msg,
                                                   [](ESteamNetworkingSocketsDebugOutputType nType, const char *pszMsg)
                                                   {
                                                       std::cout << "[SteamNet] " << pszMsg << std::endl;
                                                   });

    int32 logLevel = k_ESteamNetworkingSocketsDebugOutputType_Verbose;
    SteamNetworkingUtils()->SetConfigValue(
        k_ESteamNetworkingConfig_LogLevel_P2PRendezvous,
        k_ESteamNetworkingConfig_Global,
        0,
        k_ESteamNetworkingConfig_Int32,
        &logLevel);

    // 1. 允许 P2P (ICE) 直连
    // 默认情况下 Steam 可能会保守地只允许 LAN，这里设置为 "All" 允许公网 P2P
    int32 nIceEnable = k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Public | k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Private;
    SteamNetworkingUtils()->SetConfigValue(
        k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable,
        k_ESteamNetworkingConfig_Global, // <--- 关键：作用域选 Global
        0,                               // Global 时此参数填 0
        k_ESteamNetworkingConfig_Int32,
        &nIceEnable);

    // 2. (可选) 极度排斥中继
    // 如果你铁了心不想走中继，可以给中继路径增加巨大的虚拟延迟惩罚
    // 这样只有在直连完全打不通（比如防火墙太严格）时，Steam 才会无奈选择中继
    int32 nSdrPenalty = 10000; // 10000ms 惩罚
    SteamNetworkingUtils()->SetConfigValue(
        k_ESteamNetworkingConfig_P2P_Transport_SDR_Penalty,
        k_ESteamNetworkingConfig_Global,
        0,
        k_ESteamNetworkingConfig_Int32,
        &nSdrPenalty);

    // Allow connections from IPs without authentication
    int32 allowWithoutAuth = 2;
    SteamNetworkingUtils()->SetConfigValue(
        k_ESteamNetworkingConfig_IP_AllowWithoutAuth,
        k_ESteamNetworkingConfig_Global,
        0,
        k_ESteamNetworkingConfig_Int32,
        &allowWithoutAuth);

    // Manually set STUN server list
    std::string stunServers = "stun.l.google.com:19302,stun1.l.google.com:19302,stun2.l.google.com:19302,stun3.l.google.com:19302,stun4.l.google.com:19302";
    SteamNetworkingUtils()->SetConfigValue(
        k_ESteamNetworkingConfig_P2P_STUN_ServerList,
        k_ESteamNetworkingConfig_Global,
        0,
        k_ESteamNetworkingConfig_String,
        stunServers.c_str());

    // 打印当前配置的 TURN 和 STUN 服务器列表
    SteamNetworkingUtils()->GetConfigValueInfo(k_ESteamNetworkingConfig_P2P_TURN_ServerList, nullptr, nullptr);
    char turnServers[4096] = {};
    ESteamNetworkingConfigDataType turnType;
    size_t turnServersSize = sizeof(turnServers);
    ESteamNetworkingGetConfigValueResult turnResult = SteamNetworkingUtils()->GetConfigValue(
        k_ESteamNetworkingConfig_P2P_TURN_ServerList,
        k_ESteamNetworkingConfig_Global, 0,
        &turnType,
        turnServers, &turnServersSize);
    std::cout << "[SteamNet] TURN servers: " << turnServers << std::endl;

    char stunServersBuffer[4096] = {};
    ESteamNetworkingConfigDataType stunType;
    size_t stunServersSize = sizeof(stunServersBuffer);
    ESteamNetworkingGetConfigValueResult stunResult = SteamNetworkingUtils()->GetConfigValue(
        k_ESteamNetworkingConfig_P2P_STUN_ServerList,
        k_ESteamNetworkingConfig_Global, 0,
        &stunType,
        stunServersBuffer, &stunServersSize);
    std::cout << "[SteamNet] STUN servers: " << stunServersBuffer << std::endl;

    // Create callbacks after Steam API init
    roomManager_ = new SteamRoomManager(this);

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
    if (roomManager_)
        roomManager_->leaveLobby();
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
    if (roomManager_)
        return roomManager_->createLobby();
    return false;
}

void SteamNetworkingManager::leaveLobby()
{
    if (roomManager_)
        roomManager_->leaveLobby();
}

bool SteamNetworkingManager::searchLobbies()
{
    if (roomManager_)
        return roomManager_->searchLobbies();
    return false;
}

bool SteamNetworkingManager::joinLobby(CSteamID lobbyID)
{
    if (roomManager_)
        return roomManager_->joinLobby(lobbyID);
    return false;
}

const std::vector<CSteamID>& SteamNetworkingManager::getLobbies() const
{
    static std::vector<CSteamID> empty;
    if (roomManager_)
        return roomManager_->getLobbies();
    return empty;
}

CSteamID SteamNetworkingManager::getCurrentLobby() const
{
    if (roomManager_)
        return roomManager_->getCurrentLobby();
    return k_steamIDNil;
}

bool SteamNetworkingManager::startHosting()
{
    if (roomManager_)
        return roomManager_->startHosting();
    return false;
}

void SteamNetworkingManager::stopHosting()
{
    if (roomManager_)
        roomManager_->stopHosting();
}

bool SteamNetworkingManager::joinHost(uint64 hostID)
{
    CSteamID hostSteamID(hostID);
    g_isClient = true;
    g_hostSteamID = hostSteamID;
    SteamNetworkingIdentity identity;
    identity.SetSteamID(hostSteamID);

    g_hConnection = m_pInterface->ConnectP2P(identity, 0, 0, nullptr);

    if (g_hConnection != k_HSteamNetConnection_Invalid)
    {
        std::cout << "Attempting to connect to host " << hostSteamID.ConvertToUint64() << " with virtual port " << 0 << std::endl;
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
    }
}