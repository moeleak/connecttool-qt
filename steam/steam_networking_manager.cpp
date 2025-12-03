#include "steam_networking_manager.h"
#include <algorithm>
#include <iostream>

SteamNetworkingManager *SteamNetworkingManager::instance = nullptr;

// Static callback function
void SteamNetworkingManager::OnSteamNetConnectionStatusChanged(
    SteamNetConnectionStatusChangedCallback_t *pInfo) {
  if (instance) {
    instance->handleConnectionStatusChanged(pInfo);
  }
}

SteamNetworkingManager::SteamNetworkingManager()
    : m_pInterface(nullptr), hListenSock(k_HSteamListenSocket_Invalid),
      g_isHost(false), g_isClient(false), g_isConnected(false),
      g_hConnection(k_HSteamNetConnection_Invalid), io_context_(nullptr),
      server_(nullptr), localPort_(nullptr), localBindPort_(nullptr),
      messageHandler_(nullptr), hostPing_(0) {}

SteamNetworkingManager::~SteamNetworkingManager() {
  stopMessageHandler();
  delete messageHandler_;
  shutdown();
}

bool SteamNetworkingManager::initialize() {
  instance = this;

  // Steam API should already be initialized before calling this
  if (!SteamAPI_IsSteamRunning()) {
    std::cerr << "Steam is not running" << std::endl;
    return false;
  }

  // 【新增】开启详细日志
  SteamNetworkingUtils()->SetDebugOutputFunction(
      k_ESteamNetworkingSocketsDebugOutputType_Msg,
      [](ESteamNetworkingSocketsDebugOutputType nType, const char *pszMsg) {
        std::cout << "[SteamNet] " << pszMsg << std::endl;
      });

  int32 logLevel = k_ESteamNetworkingSocketsDebugOutputType_Verbose;
  SteamNetworkingUtils()->SetConfigValue(
      k_ESteamNetworkingConfig_LogLevel_P2PRendezvous,
      k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Int32,
      &logLevel);

  // Increase default reliable send buffer to better handle large bursts
  int32 sendBufferSize = 2 * 1024 * 1024;
  SteamNetworkingUtils()->SetConfigValue(
      k_ESteamNetworkingConfig_SendBufferSize, k_ESteamNetworkingConfig_Global,
      0, k_ESteamNetworkingConfig_Int32, &sendBufferSize);

  // Receive buffers tuned for moderate bandwidth to avoid runaway queues
  int32 recvBufferSize = 2 * 1024 * 1024; // 2 MB
  SteamNetworkingUtils()->SetConfigValue(
      k_ESteamNetworkingConfig_RecvBufferSize, k_ESteamNetworkingConfig_Global,
      0, k_ESteamNetworkingConfig_Int32, &recvBufferSize);
  int32 recvBufferMsgs = 2048;
  SteamNetworkingUtils()->SetConfigValue(
      k_ESteamNetworkingConfig_RecvBufferMessages,
      k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Int32,
      &recvBufferMsgs);

  // Cap send rate to a conservative value to keep reliable window stable
  int32 sendRate = 2 * 1024 * 1024; // 3 MB/s
  SteamNetworkingUtils()->SetConfigValue(
      k_ESteamNetworkingConfig_SendRateMin, k_ESteamNetworkingConfig_Global, 0,
      k_ESteamNetworkingConfig_Int32, &sendRate);
  SteamNetworkingUtils()->SetConfigValue(
      k_ESteamNetworkingConfig_SendRateMax, k_ESteamNetworkingConfig_Global, 0,
      k_ESteamNetworkingConfig_Int32, &sendRate);

  // Disable Nagle to reduce latency for tunneled traffic
  int32 nagleTime = 0;
  SteamNetworkingUtils()->SetConfigValue(
      k_ESteamNetworkingConfig_NagleTime, k_ESteamNetworkingConfig_Global, 0,
      k_ESteamNetworkingConfig_Int32, &nagleTime);

  std::cout << "[SteamNet] SendBuffer=" << (sendBufferSize / 1024 / 1024)
            << "MB, SendRate=" << (sendRate / 1024 / 1024)
            << "MB/s, RecvBuffer=" << (recvBufferSize / 1024 / 1024)
            << "MB, RecvMsgs=" << recvBufferMsgs << ", Nagle=" << nagleTime
            << std::endl;

  // 1. 允许 P2P (ICE) 直连
  // 默认情况下 Steam 可能会保守地只允许 LAN，这里设置为 "All" 允许公网 P2P
  int32 nIceEnable = k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Public |
                     k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Private;
  SteamNetworkingUtils()->SetConfigValue(
      k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable,
      k_ESteamNetworkingConfig_Global, // <--- 关键：作用域选 Global
      0,                               // Global 时此参数填 0
      k_ESteamNetworkingConfig_Int32, &nIceEnable);

  // 2. (可选) 极度排斥中继
  // 如果你铁了心不想走中继，可以给中继路径增加巨大的虚拟延迟惩罚
  // 这样只有在直连完全打不通（比如防火墙太严格）时，Steam 才会无奈选择中继
  int32 nSdrPenalty = 0; // 允许中继正常参与路由选择，避免直连打不通时吞吐骤降
  SteamNetworkingUtils()->SetConfigValue(
      k_ESteamNetworkingConfig_P2P_Transport_SDR_Penalty,
      k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Int32,
      &nSdrPenalty);

  // Allow connections from IPs without authentication
  int32 allowWithoutAuth = 2;
  SteamNetworkingUtils()->SetConfigValue(
      k_ESteamNetworkingConfig_IP_AllowWithoutAuth,
      k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Int32,
      &allowWithoutAuth);

  // Create callbacks after Steam API init
  SteamNetworkingUtils()->InitRelayNetworkAccess();
  SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(
      OnSteamNetConnectionStatusChanged);

  m_pInterface = SteamNetworkingSockets();

  // Check if callbacks are registered
  std::cout << "Steam Networking Manager initialized successfully" << std::endl;

  return true;
}

void SteamNetworkingManager::shutdown() {
  if (g_hConnection != k_HSteamNetConnection_Invalid) {
    m_pInterface->CloseConnection(g_hConnection, 0, nullptr, false);
  }
  if (hListenSock != k_HSteamListenSocket_Invalid) {
    m_pInterface->CloseListenSocket(hListenSock);
  }
  SteamAPI_Shutdown();
}

bool SteamNetworkingManager::joinHost(uint64 hostID) {
  CSteamID hostSteamID(hostID);
  g_isClient = true;
  g_hostSteamID = hostSteamID;
  SteamNetworkingIdentity identity;
  identity.SetSteamID(hostSteamID);

  g_hConnection = m_pInterface->ConnectP2P(identity, 0, 0, nullptr);

  if (g_hConnection != k_HSteamNetConnection_Invalid) {
    std::cout << "Attempting to connect to host "
              << hostSteamID.ConvertToUint64() << " with virtual port " << 0
              << std::endl;
    return true;
  } else {
    std::cerr << "Failed to initiate connection" << std::endl;
    return false;
  }
}

void SteamNetworkingManager::disconnect() {
  std::lock_guard<std::mutex> lock(connectionsMutex);

  // Close client connection
  if (g_hConnection != k_HSteamNetConnection_Invalid) {
    m_pInterface->CloseConnection(g_hConnection, 0, nullptr, false);
    g_hConnection = k_HSteamNetConnection_Invalid;
  }

  // Close all host connections
  for (auto conn : connections) {
    m_pInterface->CloseConnection(conn, 0, nullptr, false);
  }
  connections.clear();

  // Close listen socket
  if (hListenSock != k_HSteamListenSocket_Invalid) {
    m_pInterface->CloseListenSocket(hListenSock);
    hListenSock = k_HSteamListenSocket_Invalid;
  }

  // Reset state
  g_isHost = false;
  g_isClient = false;
  g_isConnected = false;
  hostPing_ = 0;

  std::cout << "Disconnected from network" << std::endl;
}

void SteamNetworkingManager::setMessageHandlerDependencies(
    boost::asio::io_context &io_context, std::unique_ptr<TCPServer> &server,
    int &localPort, int &localBindPort) {
  io_context_ = &io_context;
  server_ = &server;
  localPort_ = &localPort;
  localBindPort_ = &localBindPort;
  messageHandler_ =
      new SteamMessageHandler(io_context, m_pInterface, connections,
                              connectionsMutex, g_isHost, localPort);
}

void SteamNetworkingManager::startMessageHandler() {
  if (messageHandler_) {
    messageHandler_->start();
  }
}

void SteamNetworkingManager::stopMessageHandler() {
  if (messageHandler_) {
    messageHandler_->stop();
  }
}

void SteamNetworkingManager::update() {
  std::lock_guard<std::mutex> lock(connectionsMutex);
  // Update ping to host/client connection
  if (g_hConnection != k_HSteamNetConnection_Invalid) {
    SteamNetConnectionRealTimeStatus_t status;
    if (m_pInterface->GetConnectionRealTimeStatus(g_hConnection, &status, 0,
                                                  nullptr)) {
      hostPing_ = status.m_nPing;
    }
  }
}

int SteamNetworkingManager::getConnectionPing(HSteamNetConnection conn) const {
  SteamNetConnectionRealTimeStatus_t status;
  if (m_pInterface->GetConnectionRealTimeStatus(conn, &status, 0, nullptr)) {
    return status.m_nPing;
  }
  return 0;
}

std::string
SteamNetworkingManager::getConnectionRelayInfo(HSteamNetConnection conn) const {
  SteamNetConnectionInfo_t info;
  if (m_pInterface->GetConnectionInfo(conn, &info)) {
    // Check if connection is using relay
    if (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) {
      return "中继";
    } else {
      return "直连";
    }
  }
  return "N/A";
}

void SteamNetworkingManager::handleConnectionStatusChanged(
    SteamNetConnectionStatusChangedCallback_t *pInfo) {
  std::lock_guard<std::mutex> lock(connectionsMutex);
  std::cout << "Connection status changed: " << pInfo->m_info.m_eState
            << " for connection " << pInfo->m_hConn << std::endl;
  if (pInfo->m_info.m_eState ==
      k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
    std::cout << "Connection failed: " << pInfo->m_info.m_szEndDebug
              << std::endl;
  }
  if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_None &&
      pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting) {
    m_pInterface->AcceptConnection(pInfo->m_hConn);
    connections.push_back(pInfo->m_hConn);
    g_hConnection = pInfo->m_hConn;
    g_isConnected = true;
    std::cout << "Accepted incoming connection from "
              << pInfo->m_info.m_identityRemote.GetSteamID().ConvertToUint64()
              << std::endl;
    // Log connection info
    SteamNetConnectionInfo_t info;
    SteamNetConnectionRealTimeStatus_t status;
    if (m_pInterface->GetConnectionInfo(pInfo->m_hConn, &info) &&
        m_pInterface->GetConnectionRealTimeStatus(pInfo->m_hConn, &status, 0,
                                                  nullptr)) {
      std::cout << "Incoming connection details: ping=" << status.m_nPing
                << "ms, relay=" << (info.m_idPOPRelay != 0 ? "yes" : "no")
                << std::endl;
    }
  } else if (pInfo->m_eOldState ==
                 k_ESteamNetworkingConnectionState_Connecting &&
             pInfo->m_info.m_eState ==
                 k_ESteamNetworkingConnectionState_Connected) {
    g_isConnected = true;
    std::cout << "Connected to host" << std::endl;
    // Log connection info
    SteamNetConnectionInfo_t info;
    SteamNetConnectionRealTimeStatus_t status;
    if (m_pInterface->GetConnectionInfo(pInfo->m_hConn, &info) &&
        m_pInterface->GetConnectionRealTimeStatus(pInfo->m_hConn, &status, 0,
                                                  nullptr)) {
      hostPing_ = status.m_nPing;
      std::cout << "Outgoing connection details: ping=" << status.m_nPing
                << "ms, relay=" << (info.m_idPOPRelay != 0 ? "yes" : "no")
                << std::endl;
    }
  } else if (pInfo->m_info.m_eState ==
                 k_ESteamNetworkingConnectionState_ClosedByPeer ||
             pInfo->m_info.m_eState ==
                 k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
    g_isConnected = false;
    g_hConnection = k_HSteamNetConnection_Invalid;
    // Remove from connections
    auto it = std::find(connections.begin(), connections.end(), pInfo->m_hConn);
    if (it != connections.end()) {
      connections.erase(it);
    }
    hostPing_ = 0;
    std::cout << "Connection closed" << std::endl;
  }
}
