#pragma once
#include <functional>
#include <iostream>
#include <mutex>
#include <steam_api.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <tuple>

inline constexpr const char kLobbyKeyMode[] = "ct_mode";

class SteamNetworkingManager; // Forward declaration
class SteamVpnNetworkingManager;
class SteamRoomManager;       // Forward declaration for callbacks

class SteamFriendsCallbacks {
public:
  SteamFriendsCallbacks(SteamNetworkingManager *manager,
                        SteamRoomManager *roomManager);

private:
  SteamNetworkingManager *manager_;
  SteamRoomManager *roomManager_;

  STEAM_CALLBACK(SteamFriendsCallbacks, OnGameLobbyJoinRequested,
                 GameLobbyJoinRequested_t);
};

class SteamMatchmakingCallbacks {
public:
  SteamMatchmakingCallbacks(SteamNetworkingManager *manager,
                            SteamRoomManager *roomManager);

  CCallResult<SteamMatchmakingCallbacks, LobbyCreated_t>
      m_CallResultLobbyCreated;
  CCallResult<SteamMatchmakingCallbacks, LobbyMatchList_t>
      m_CallResultLobbyMatchList;

  void OnLobbyCreated(LobbyCreated_t *pCallback, bool bIOFailure);
  void OnLobbyListReceived(LobbyMatchList_t *pCallback, bool bIOFailure);

private:
  SteamNetworkingManager *manager_;
  SteamRoomManager *roomManager_;

  STEAM_CALLBACK(SteamMatchmakingCallbacks, OnLobbyDataUpdate,
                 LobbyDataUpdate_t);
  STEAM_CALLBACK(SteamMatchmakingCallbacks, OnLobbyEntered, LobbyEnter_t);
  STEAM_CALLBACK(SteamMatchmakingCallbacks, OnLobbyChatUpdate,
                 LobbyChatUpdate_t);
  STEAM_CALLBACK(SteamMatchmakingCallbacks, OnLobbyChatMsg, LobbyChatMsg_t);
};

class SteamRoomManager {
public:
  struct LobbyInfo {
    CSteamID id;
    CSteamID ownerId;
    std::string name;
    std::string ownerName;
    int memberCount = 0;
    int pingMs = -1;
  };

  SteamRoomManager(SteamNetworkingManager *networkingManager);
  ~SteamRoomManager();

  bool createLobby();
  void leaveLobby();
  bool searchLobbies();
  bool joinLobby(CSteamID lobbyID);
  bool startHosting();
  void stopHosting();
  void setLobbyName(const std::string &name);
  void setAdvertisedMode(bool wantsTun) { advertisedWantsTun_ = wantsTun; }
  void setPublishLobby(bool publish);
  std::string getLobbyName() const;
  void setLobbyListCallback(
      std::function<void(const std::vector<LobbyInfo> &)> callback);
  void setHostLeftCallback(std::function<void()> callback);
  void setChatMessageCallback(
      std::function<void(const CSteamID &, const std::string &)> callback);
  bool sendChatMessage(const std::string &message);
  void broadcastPings(
      const std::vector<std::tuple<uint64_t, int, std::string>> &pings);
  bool getRemotePing(const CSteamID &id, int &ping, std::string &relay) const;

  CSteamID getCurrentLobby() const { return currentLobby; }
  const std::vector<CSteamID> &getLobbies() const { return lobbies; }
  const std::vector<LobbyInfo> &getLobbyInfos() const { return lobbyInfos; }
  std::vector<CSteamID> getLobbyMembers() const;

  void setCurrentLobby(CSteamID lobby) { currentLobby = lobby; }
  void addLobby(CSteamID lobby) { lobbies.push_back(lobby); }
  void clearLobbies() { lobbies.clear(); }
  void setVpnMode(bool enabled, SteamVpnNetworkingManager *vpnManager);
  bool vpnMode() const { return vpnMode_; }
  void setLobbyModeChangedCallback(
      std::function<void(bool wantsTun, const CSteamID &lobby)> callback);
  void setLobbyInviteCallback(
      std::function<void(const CSteamID &lobby)> callback);

private:
  friend class SteamMatchmakingCallbacks;
  friend class SteamFriendsCallbacks;
  friend class Backend;
  void refreshLobbyMetadata();
  void decideTransportForCurrentLobby();
  void notifyLobbyListUpdated();
  void handlePingMessage(const std::string &payload);
  void handleChatMessage(const CSteamID &sender, const std::string &payload);
  bool lobbyWantsTun(CSteamID lobby) const;

  SteamNetworkingManager *networkingManager_;
  SteamVpnNetworkingManager *vpnNetworkingManager_ = nullptr;
  CSteamID currentLobby;
  std::vector<CSteamID> lobbies;
  std::vector<LobbyInfo> lobbyInfos;
  SteamFriendsCallbacks *steamFriendsCallbacks;
  SteamMatchmakingCallbacks *steamMatchmakingCallbacks;
  std::function<void(const std::vector<LobbyInfo> &)> lobbyListCallback_;
  std::string lobbyName_;
  bool publishLobby_ = true;
  bool advertisedWantsTun_ = false;
  struct PingInfo {
    int ping = -1;
    std::string relay;
    std::chrono::steady_clock::time_point updatedAt;
  };
  std::unordered_map<uint64_t, PingInfo> remotePings_;
  bool vpnMode_ = false;
  std::function<void(bool wantsTun, const CSteamID &lobby)>
      lobbyModeChangedCallback_;
  std::function<void(const CSteamID &)> lobbyInviteCallback_;
  std::function<void()> hostLeftCallback_;
  std::function<void(const CSteamID &, const std::string &)>
      chatMessageCallback_;
};
