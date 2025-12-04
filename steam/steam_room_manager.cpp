#include "steam_room_manager.h"
#include "steam_networking_manager.h"
#include "steam_vpn_networking_manager.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <isteamnetworkingutils.h>
#include <utility>

namespace {
constexpr const char *kLobbyKeyName = "ct_name";
constexpr const char *kLobbyKeyHostName = "ct_host_name";
constexpr const char *kLobbyKeyHostId = "ct_host_id";
constexpr const char *kLobbyKeyPingLocation = "ct_ping_loc";
constexpr const char *kLobbyKeyTag = "ct_tag";
constexpr const char *kLobbyTagValue = "1";
constexpr const char *kPingPrefix = "PING|";
constexpr const char *kLobbyModeTun = "tun";
constexpr const char *kLobbyModeTcp = "tcp";
} // namespace

SteamFriendsCallbacks::SteamFriendsCallbacks(SteamNetworkingManager *manager,
                                             SteamRoomManager *roomManager)
    : manager_(manager), roomManager_(roomManager) {
  std::cout << "SteamFriendsCallbacks constructor called" << std::endl;
}

void SteamFriendsCallbacks::OnGameLobbyJoinRequested(
    GameLobbyJoinRequested_t *pCallback) {
  std::cout << "GameLobbyJoinRequested received" << std::endl;
  if (manager_) {
    CSteamID lobbyID = pCallback->m_steamIDLobby;
    std::cout << "Lobby ID: " << lobbyID.ConvertToUint64() << std::endl;
    if (!manager_->isHost() && !manager_->isConnected()) {
      std::cout << "Joining lobby from request: " << lobbyID.ConvertToUint64()
                << std::endl;
      roomManager_->joinLobby(lobbyID);
    } else {
      std::cout << "Already host or connected, ignoring lobby join request"
                << std::endl;
    }
  } else {
    std::cout << "Manager is null" << std::endl;
  }
}

SteamMatchmakingCallbacks::SteamMatchmakingCallbacks(
    SteamNetworkingManager *manager, SteamRoomManager *roomManager)
    : manager_(manager), roomManager_(roomManager) {}

void SteamMatchmakingCallbacks::OnLobbyChatMsg(LobbyChatMsg_t *pCallback) {
  if (!roomManager_) {
    return;
  }
  CSteamID lobbyId = roomManager_->getCurrentLobby();
  if (!lobbyId.IsValid() ||
      lobbyId.ConvertToUint64() != pCallback->m_ulSteamIDLobby) {
    return;
  }

  char data[2048]{};
  EChatEntryType type;
  CSteamID sender;
  const int read = SteamMatchmaking()->GetLobbyChatEntry(
      pCallback->m_ulSteamIDLobby, pCallback->m_iChatID, &sender, data,
      sizeof(data) - 1, &type);
  if (read <= 0 || type != k_EChatEntryTypeChatMsg) {
    return;
  }
  CSteamID owner = SteamMatchmaking()->GetLobbyOwner(pCallback->m_ulSteamIDLobby);
  if (owner.IsValid() && sender.IsValid() && sender != owner) {
    return; // only trust host broadcast
  }
  data[std::min<int>(read, sizeof(data) - 1)] = '\0';
  const std::string payload(data);
  roomManager_->handlePingMessage(payload);
}

void SteamMatchmakingCallbacks::OnLobbyCreated(LobbyCreated_t *pCallback,
                                               bool bIOFailure) {
  if (bIOFailure) {
    std::cerr << "Failed to create lobby - IO Failure" << std::endl;
    return;
  }
  if (pCallback->m_eResult == k_EResultOK) {
    roomManager_->setCurrentLobby(pCallback->m_ulSteamIDLobby);
    std::cout << "Lobby created: "
              << roomManager_->getCurrentLobby().ConvertToUint64() << std::endl;

    // Set Rich Presence to enable invite functionality
    SteamFriends()->SetRichPresence("steam_display", "#Status_InLobby");
    SteamFriends()->SetRichPresence(
        "connect", std::to_string(pCallback->m_ulSteamIDLobby).c_str());
    roomManager_->refreshLobbyMetadata();
  } else {
    std::cerr << "Failed to create lobby" << std::endl;
  }
}

void SteamMatchmakingCallbacks::OnLobbyListReceived(LobbyMatchList_t *pCallback,
                                                    bool bIOFailure) {
  if (!roomManager_) {
    return;
  }
  if (bIOFailure) {
    std::cerr << "Failed to receive lobby list - IO Failure" << std::endl;
    roomManager_->clearLobbies();
    roomManager_->lobbyInfos.clear();
    roomManager_->notifyLobbyListUpdated();
    return;
  }
  roomManager_->clearLobbies();
  roomManager_->lobbyInfos.clear();

  SteamNetworkPingLocation_t localPingLocation{};
  const bool hasLocalPing = SteamNetworkingUtils() &&
                            SteamNetworkingUtils()->GetLocalPingLocation(
                                localPingLocation) >= 0.0f;

  std::vector<SteamRoomManager::LobbyInfo> infos;
  infos.reserve(pCallback->m_nLobbiesMatching);

  for (uint32 i = 0; i < pCallback->m_nLobbiesMatching; ++i) {
    CSteamID lobbyID = SteamMatchmaking()->GetLobbyByIndex(i);
    roomManager_->addLobby(lobbyID);
    SteamRoomManager::LobbyInfo info;
    info.id = lobbyID;
    info.ownerId = SteamMatchmaking()->GetLobbyOwner(lobbyID);
    info.memberCount = SteamMatchmaking()->GetNumLobbyMembers(lobbyID);

    const char *name = SteamMatchmaking()->GetLobbyData(lobbyID, kLobbyKeyName);
    if (name) {
      info.name = name;
    }

    const char *ownerName =
        SteamMatchmaking()->GetLobbyData(lobbyID, kLobbyKeyHostName);
    if (ownerName) {
      info.ownerName = ownerName;
    } else if (info.ownerId.IsValid() && SteamFriends()) {
      const char *fallback =
          SteamFriends()->GetFriendPersonaName(info.ownerId);
      if (fallback) {
        info.ownerName = fallback;
      }
    }

    const char *pingLoc =
        SteamMatchmaking()->GetLobbyData(lobbyID, kLobbyKeyPingLocation);
    if (hasLocalPing && pingLoc && pingLoc[0] != '\0' &&
        SteamNetworkingUtils()) {
      SteamNetworkPingLocation_t remote;
      if (SteamNetworkingUtils()->ParsePingLocationString(pingLoc, remote)) {
        info.pingMs =
            SteamNetworkingUtils()->EstimatePingTimeFromLocalHost(remote);
      }
    }

    infos.push_back(std::move(info));
  }

  // Ensure current lobby is present even if not returned by filter (e.g. host
  //未设置标签或区域过滤不同). Skip when lobby is intentionally unlisted.
  CSteamID current = roomManager_->getCurrentLobby();
  if (current.IsValid() && roomManager_->publishLobby_) {
    const uint64 currentVal = current.ConvertToUint64();
    const bool exists = std::any_of(
        infos.begin(), infos.end(),
        [currentVal](const SteamRoomManager::LobbyInfo &li) {
          return li.id.ConvertToUint64() == currentVal;
        });
    if (!exists) {
      SteamRoomManager::LobbyInfo info;
      info.id = current;
      info.ownerId = SteamMatchmaking()->GetLobbyOwner(current);
      info.memberCount = SteamMatchmaking()->GetNumLobbyMembers(current);
      const char *name = SteamMatchmaking()->GetLobbyData(current, kLobbyKeyName);
      if (name) {
        info.name = name;
      }
      const char *ownerName =
          SteamMatchmaking()->GetLobbyData(current, kLobbyKeyHostName);
      if (ownerName) {
        info.ownerName = ownerName;
      } else if (info.ownerId.IsValid() && SteamFriends()) {
        const char *fallback =
            SteamFriends()->GetFriendPersonaName(info.ownerId);
        if (fallback) {
          info.ownerName = fallback;
        }
      }

      const char *pingLoc =
          SteamMatchmaking()->GetLobbyData(current, kLobbyKeyPingLocation);
      if (hasLocalPing && pingLoc && pingLoc[0] != '\0' &&
          SteamNetworkingUtils()) {
        SteamNetworkPingLocation_t remote;
        if (SteamNetworkingUtils()->ParsePingLocationString(pingLoc, remote)) {
          info.pingMs = SteamNetworkingUtils()->EstimatePingTimeFromLocalHost(
              remote);
        }
      }

      infos.push_back(std::move(info));
    }
  }

  roomManager_->lobbyInfos = std::move(infos);
  roomManager_->notifyLobbyListUpdated();

  std::cout << "Received " << pCallback->m_nLobbiesMatching << " lobbies"
            << std::endl;
}

void SteamMatchmakingCallbacks::OnLobbyEntered(LobbyEnter_t *pCallback) {
  if (pCallback->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess) {
    roomManager_->setCurrentLobby(pCallback->m_ulSteamIDLobby);
    if (manager_) {
      CSteamID hostID =
          SteamMatchmaking()->GetLobbyOwner(pCallback->m_ulSteamIDLobby);
      manager_->setHostSteamID(hostID);
    }
    std::cout << "Entered lobby: " << pCallback->m_ulSteamIDLobby << std::endl;

    const bool lobbyIsTun = roomManager_->lobbyWantsTun(
        CSteamID(pCallback->m_ulSteamIDLobby));
    if (lobbyIsTun) {
      roomManager_->vpnMode_ = true;
    }
    if (roomManager_->lobbyModeChangedCallback_) {
      roomManager_->lobbyModeChangedCallback_(lobbyIsTun,
                                              roomManager_->getCurrentLobby());
    }

    // Set Rich Presence to enable invite functionality
    SteamFriends()->SetRichPresence("steam_display", "#Status_InLobby");
    SteamFriends()->SetRichPresence(
        "connect", std::to_string(pCallback->m_ulSteamIDLobby).c_str());

    if (roomManager_->vpnMode_) {
      const CSteamID hostID =
          SteamMatchmaking()->GetLobbyOwner(pCallback->m_ulSteamIDLobby);
      if (roomManager_->vpnNetworkingManager_) {
        const CSteamID mySteamID = SteamUser()->GetSteamID();
        const int numMembers =
            SteamMatchmaking()->GetNumLobbyMembers(pCallback->m_ulSteamIDLobby);
        for (int i = 0; i < numMembers; ++i) {
          CSteamID memberID =
              SteamMatchmaking()->GetLobbyMemberByIndex(
                  pCallback->m_ulSteamIDLobby, i);
          if (memberID != mySteamID) {
            roomManager_->vpnNetworkingManager_->addPeer(memberID);
          }
        }
      }
      if (SteamUser() && hostID == SteamUser()->GetSteamID()) {
        roomManager_->refreshLobbyMetadata();
      }
      return;
    }

    // Only join host if not the host
    if (!manager_->isHost()) {
      if (roomManager_) {
        roomManager_->decideTransportForCurrentLobby();
      }
      CSteamID hostID =
          SteamMatchmaking()->GetLobbyOwner(pCallback->m_ulSteamIDLobby);
      if (manager_->joinHost(hostID.ConvertToUint64())) {
        // Start TCP Server if dependencies are set
        if (manager_->getServer() && !(*manager_->getServer())) {
          const int bindPort = manager_->getBindPort();
          *manager_->getServer() =
              std::make_unique<TCPServer>(bindPort, manager_);
          if (!(*manager_->getServer())->start()) {
            std::cerr << "Failed to start TCP server" << std::endl;
          }
        }
      }
    } else {
      roomManager_->refreshLobbyMetadata();
    }
  } else {
    std::cerr << "Failed to enter lobby" << std::endl;
  }
}

void SteamMatchmakingCallbacks::OnLobbyChatUpdate(
    LobbyChatUpdate_t *pCallback) {
  if (!roomManager_ || !manager_) {
    return;
  }

  CSteamID lobbyId = roomManager_->getCurrentLobby();
  if (!lobbyId.IsValid() ||
      lobbyId.ConvertToUint64() != pCallback->m_ulSteamIDLobby) {
    return;
  }

  std::cout << "Lobby chat updated for lobby " << pCallback->m_ulSteamIDLobby
            << " change flags " << pCallback->m_rgfChatMemberStateChange
            << std::endl;

  const uint32 changeFlags = pCallback->m_rgfChatMemberStateChange;
  const bool memberLeft =
      (changeFlags & k_EChatMemberStateChangeLeft) ||
      (changeFlags & k_EChatMemberStateChangeDisconnected) ||
      (changeFlags & k_EChatMemberStateChangeKicked) ||
      (changeFlags & k_EChatMemberStateChangeBanned);
  CSteamID changedUser = CSteamID(pCallback->m_ulSteamIDUserChanged);

  if (roomManager_->vpnMode_) {
    const CSteamID mySteamId = SteamUser()->GetSteamID();
    if ((changeFlags & k_EChatMemberStateChangeEntered) &&
        roomManager_->vpnNetworkingManager_ && changedUser != mySteamId) {
      roomManager_->vpnNetworkingManager_->addPeer(changedUser);
    } else if (memberLeft && roomManager_->vpnNetworkingManager_) {
      roomManager_->vpnNetworkingManager_->removePeer(changedUser);
    }
    if (memberLeft && changedUser == manager_->getHostSteamID() &&
        !manager_->isHost() && roomManager_->hostLeftCallback_) {
      roomManager_->hostLeftCallback_();
    }
    return;
  }

  if (!memberLeft) {
    return;
  }

  if (changedUser == manager_->getHostSteamID() && !manager_->isHost()) {
    std::cout << "Host left lobby, disconnecting client locally" << std::endl;
    manager_->disconnect();
    roomManager_->leaveLobby();
  }
}

SteamRoomManager::SteamRoomManager(SteamNetworkingManager *networkingManager)
    : networkingManager_(networkingManager), currentLobby(k_steamIDNil),
      steamFriendsCallbacks(nullptr), steamMatchmakingCallbacks(nullptr) {
  steamFriendsCallbacks = new SteamFriendsCallbacks(networkingManager_, this);
  steamMatchmakingCallbacks =
      new SteamMatchmakingCallbacks(networkingManager_, this);

  // Clear Rich Presence on initialization to prevent "Invite to game" showing
  // when not in a lobby
  SteamFriends()->ClearRichPresence();
}

void SteamRoomManager::setVpnMode(bool enabled,
                                  SteamVpnNetworkingManager *vpnManager) {
  vpnMode_ = enabled;
  vpnNetworkingManager_ = vpnManager;
}

void SteamRoomManager::setLobbyModeChangedCallback(
    std::function<void(bool wantsTun, const CSteamID &lobby)> callback) {
  lobbyModeChangedCallback_ = std::move(callback);
}

SteamRoomManager::~SteamRoomManager() {
  delete steamFriendsCallbacks;
  delete steamMatchmakingCallbacks;
}

bool SteamRoomManager::createLobby() {
  SteamAPICall_t hSteamAPICall =
      SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, 4);
  if (hSteamAPICall == k_uAPICallInvalid) {
    std::cerr << "Failed to create lobby" << std::endl;
    return false;
  }
  // Register the call result
  steamMatchmakingCallbacks->m_CallResultLobbyCreated.Set(
      hSteamAPICall, steamMatchmakingCallbacks,
      &SteamMatchmakingCallbacks::OnLobbyCreated);
  return true;
}

void SteamRoomManager::leaveLobby() {
  if (currentLobby != k_steamIDNil) {
    SteamMatchmaking()->LeaveLobby(currentLobby);
    currentLobby = k_steamIDNil;
    if (networkingManager_) {
      networkingManager_->setHostSteamID(CSteamID());
    }
    if (vpnMode_ && vpnNetworkingManager_) {
      vpnNetworkingManager_->clearPeers();
    }
    remotePings_.clear();

    // Clear Rich Presence when leaving lobby
    SteamFriends()->ClearRichPresence();
  }
}

bool SteamRoomManager::searchLobbies() {
  lobbies.clear();
  lobbyInfos.clear();
  if (!SteamMatchmaking()) {
    std::cerr << "Failed to request lobby list - matchmaking unavailable"
              << std::endl;
    return false;
  }
  SteamMatchmaking()->AddRequestLobbyListStringFilter(
      kLobbyKeyTag, kLobbyTagValue, k_ELobbyComparisonEqual);
  SteamMatchmaking()->AddRequestLobbyListDistanceFilter(
      k_ELobbyDistanceFilterWorldwide);
  SteamMatchmaking()->AddRequestLobbyListResultCountFilter(100);
  SteamAPICall_t hSteamAPICall = SteamMatchmaking()->RequestLobbyList();
  if (hSteamAPICall == k_uAPICallInvalid) {
    std::cerr << "Failed to request lobby list" << std::endl;
    return false;
  }
  // Register the call result
  steamMatchmakingCallbacks->m_CallResultLobbyMatchList.Set(
      hSteamAPICall, steamMatchmakingCallbacks,
      &SteamMatchmakingCallbacks::OnLobbyListReceived);
  return true;
}

bool SteamRoomManager::joinLobby(CSteamID lobbyID) {
  SteamMatchmaking()->JoinLobby(lobbyID);
  // Connection result will be delivered asynchronously via OnLobbyEntered.
  return true;
}

bool SteamRoomManager::startHosting() {
  if (!createLobby()) {
    return false;
  }

  if (vpnMode_) {
    return true;
  }

  networkingManager_->getListenSock() =
      networkingManager_->getInterface()->CreateListenSocketP2P(0, 0, nullptr);

  if (networkingManager_->getListenSock() != k_HSteamListenSocket_Invalid) {
    networkingManager_->getIsHost() = true;
    std::cout << "Created listen socket for hosting game room" << std::endl;
    return true;
  } else {
    std::cerr << "Failed to create listen socket for hosting" << std::endl;
    leaveLobby();
    return false;
  }
}

void SteamRoomManager::stopHosting() {
  if (networkingManager_->getListenSock() != k_HSteamListenSocket_Invalid) {
    networkingManager_->getInterface()->CloseListenSocket(
        networkingManager_->getListenSock());
    networkingManager_->getListenSock() = k_HSteamListenSocket_Invalid;
  }
  leaveLobby();
  networkingManager_->getIsHost() = false;
}

std::vector<CSteamID> SteamRoomManager::getLobbyMembers() const {
  std::vector<CSteamID> members;
  if (currentLobby != k_steamIDNil) {
    int numMembers = SteamMatchmaking()->GetNumLobbyMembers(currentLobby);
    for (int i = 0; i < numMembers; ++i) {
      CSteamID memberID =
          SteamMatchmaking()->GetLobbyMemberByIndex(currentLobby, i);
      members.push_back(memberID);
    }
  }
  return members;
}

void SteamRoomManager::setLobbyName(const std::string &name) {
  lobbyName_ = name;
  refreshLobbyMetadata();
}

void SteamRoomManager::setPublishLobby(bool publish) {
  publishLobby_ = publish;
  refreshLobbyMetadata();
}

std::string SteamRoomManager::getLobbyName() const {
  if (currentLobby == k_steamIDNil || !SteamMatchmaking()) {
    return {};
  }
  const char *name = SteamMatchmaking()->GetLobbyData(currentLobby,
                                                      kLobbyKeyName);
  if (name && name[0] != '\0') {
    return name;
  }
  return {};
}

void SteamRoomManager::setLobbyListCallback(
    std::function<void(const std::vector<LobbyInfo> &)> callback) {
  lobbyListCallback_ = std::move(callback);
}

void SteamRoomManager::setHostLeftCallback(std::function<void()> callback) {
  hostLeftCallback_ = std::move(callback);
}

void SteamRoomManager::refreshLobbyMetadata() {
  if (currentLobby == k_steamIDNil || !SteamMatchmaking()) {
    return;
  }
  const bool isOwner = SteamUser() &&
                       SteamMatchmaking()->GetLobbyOwner(currentLobby) ==
                           SteamUser()->GetSteamID();
  if (!networkingManager_ ||
      (!networkingManager_->isHost() && !(vpnMode_ && isOwner))) {
    return;
  }

  if (!publishLobby_) {
    SteamMatchmaking()->DeleteLobbyData(currentLobby, kLobbyKeyTag);
    SteamMatchmaking()->DeleteLobbyData(currentLobby, kLobbyKeyName);
    SteamMatchmaking()->DeleteLobbyData(currentLobby, kLobbyKeyHostId);
    SteamMatchmaking()->DeleteLobbyData(currentLobby, kLobbyKeyHostName);
    SteamMatchmaking()->DeleteLobbyData(currentLobby, kLobbyKeyPingLocation);
    SteamMatchmaking()->DeleteLobbyData(currentLobby, kLobbyKeyMode);
    return;
  }

  SteamMatchmaking()->SetLobbyData(currentLobby, kLobbyKeyTag, kLobbyTagValue);

  std::string nameToUse = lobbyName_;
  if (nameToUse.empty() && SteamFriends()) {
    const char *persona = SteamFriends()->GetPersonaName();
    if (persona) {
      nameToUse = persona;
    }
  }
  if (!nameToUse.empty()) {
    SteamMatchmaking()->SetLobbyData(currentLobby, kLobbyKeyName,
                                     nameToUse.c_str());
  }

  CSteamID owner = SteamMatchmaking()->GetLobbyOwner(currentLobby);
  if (owner.IsValid()) {
    SteamMatchmaking()->SetLobbyData(
        currentLobby, kLobbyKeyHostId,
        std::to_string(owner.ConvertToUint64()).c_str());
  }
  if (SteamFriends()) {
    const char *ownerName = SteamFriends()->GetPersonaName();
    if (ownerName) {
      SteamMatchmaking()->SetLobbyData(currentLobby, kLobbyKeyHostName,
                                       ownerName);
    }
  }

  if (SteamNetworkingUtils()) {
    SteamNetworkPingLocation_t local;
    SteamNetworkingUtils()->GetLocalPingLocation(local);
    char buffer[k_cchMaxSteamNetworkingPingLocationString]{};
    SteamNetworkingUtils()->ConvertPingLocationToString(
        local, buffer, sizeof(buffer));
    SteamMatchmaking()->SetLobbyData(currentLobby, kLobbyKeyPingLocation,
                                     buffer);
  }
  SteamMatchmaking()->SetLobbyData(currentLobby, kLobbyKeyMode,
                                   vpnMode_ ? kLobbyModeTun : kLobbyModeTcp);
}

void SteamRoomManager::decideTransportForCurrentLobby() {
  if (vpnMode_) {
    return;
  }
  if (!networkingManager_ || networkingManager_->isHost()) {
    return;
  }
  if (currentLobby == k_steamIDNil || !SteamMatchmaking() ||
      !SteamNetworkingUtils()) {
    return;
  }

  const char *pingLocStr =
      SteamMatchmaking()->GetLobbyData(currentLobby, kLobbyKeyPingLocation);
  SteamNetworkPingLocation_t remote{};
  SteamNetworkPingLocation_t local{};
  int directPing = -1;

  if (pingLocStr && pingLocStr[0] != '\0' &&
      SteamNetworkingUtils()->ParsePingLocationString(pingLocStr, remote) &&
      SteamNetworkingUtils()->GetLocalPingLocation(local) >= 0.0f) {
    directPing =
        SteamNetworkingUtils()->EstimatePingTimeBetweenTwoLocations(local,
                                                                     remote);
  }

  const int relayPing = networkingManager_->estimateRelayPingMs();
  networkingManager_->applyTransportPreference(directPing, relayPing);
}

void SteamRoomManager::notifyLobbyListUpdated() {
  if (lobbyListCallback_) {
    lobbyListCallback_(lobbyInfos);
  }
}

void SteamRoomManager::broadcastPings(
    const std::vector<std::tuple<uint64_t, int, std::string>> &pings) {
  if (!networkingManager_ || !networkingManager_->isHost() ||
      currentLobby == k_steamIDNil) {
    return;
  }
  if (!SteamMatchmaking()) {
    return;
  }
  std::string payload(kPingPrefix);
  bool first = true;
  for (const auto &entry : pings) {
    const uint64_t id = std::get<0>(entry);
    const int ping = std::get<1>(entry);
    if (ping < 0) {
      continue;
    }
    if (!first) {
      payload.push_back(';');
    }
    first = false;
    payload += std::to_string(id);
    payload.push_back(':');
    payload += std::to_string(ping);
    payload.push_back(':');
    payload += std::get<2>(entry);
  }
  if (payload.size() <= strlen(kPingPrefix)) {
    return;
  }
  SteamMatchmaking()->SendLobbyChatMsg(
      currentLobby, payload.c_str(), static_cast<int>(payload.size()));
}

void SteamRoomManager::handlePingMessage(const std::string &payload) {
  if (payload.rfind(kPingPrefix, 0) != 0) {
    return;
  }
  const std::string data = payload.substr(strlen(kPingPrefix));
  size_t start = 0;
  const auto now = std::chrono::steady_clock::now();
  while (start < data.size()) {
    const size_t next = data.find(';', start);
    const std::string_view part(
        data.c_str() + start,
        (next == std::string::npos ? data.size() : next) - start);
    if (!part.empty()) {
      const size_t first = part.find(':');
      const size_t second = part.find(':', first == std::string::npos
                                             ? std::string::npos
                                             : first + 1);
      if (first != std::string::npos && second != std::string::npos &&
          second > first) {
        const std::string idStr(part.substr(0, first));
        const std::string pingStr(part.substr(first + 1, second - first - 1));
        const std::string relayStr(part.substr(second + 1));
        try {
          const uint64_t id = std::stoull(idStr);
          const int ping = std::stoi(pingStr);
          if (id > 0 && ping >= 0) {
            PingInfo &info = remotePings_[id];
            info.ping = ping;
            info.relay = relayStr;
            info.updatedAt = now;
          }
        } catch (...) {
        }
      }
    }
    if (next == std::string::npos) {
      break;
    }
    start = next + 1;
  }
}

bool SteamRoomManager::getRemotePing(const CSteamID &id, int &ping,
                                     std::string &relay) const {
  const uint64_t key = id.ConvertToUint64();
  auto it = remotePings_.find(key);
  if (it == remotePings_.end()) {
    return false;
  }
  ping = it->second.ping;
  relay = it->second.relay;
  return ping >= 0;
}

bool SteamRoomManager::lobbyWantsTun(CSteamID lobby) const {
  if (!SteamMatchmaking()) {
    return false;
  }
  const char *mode = SteamMatchmaking()->GetLobbyData(lobby, kLobbyKeyMode);
  return mode && strcmp(mode, kLobbyModeTun) == 0;
}
