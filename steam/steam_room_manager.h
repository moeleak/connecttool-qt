#pragma once
#include <steam_api.h>
#include <vector>
#include <iostream>
#include <mutex>

class SteamNetworkingManager; // Forward declaration

class SteamFriendsCallbacks
{
public:
    SteamFriendsCallbacks(SteamNetworkingManager *manager);
    void OnGameRichPresenceJoinRequested(GameRichPresenceJoinRequested_t *pCallback);
    void OnGameLobbyJoinRequested(GameLobbyJoinRequested_t *pCallback);

private:
    SteamNetworkingManager *manager_;
};

class SteamMatchmakingCallbacks
{
public:
    SteamMatchmakingCallbacks(SteamNetworkingManager *manager);
    void OnLobbyCreated(LobbyCreated_t *pCallback);
    void OnLobbyListReceived(LobbyMatchList_t *pCallback);
    void OnLobbyEntered(LobbyEnter_t *pCallback);

private:
    SteamNetworkingManager *manager_;
};

class SteamRoomManager
{
public:
    SteamRoomManager(SteamNetworkingManager *networkingManager);
    ~SteamRoomManager();

    bool createLobby();
    void leaveLobby();
    bool searchLobbies();
    bool joinLobby(CSteamID lobbyID);
    bool startHosting();
    void stopHosting();

    CSteamID getCurrentLobby() const { return currentLobby; }
    const std::vector<CSteamID>& getLobbies() const { return lobbies; }

    void setCurrentLobby(CSteamID lobby) { currentLobby = lobby; }
    void addLobby(CSteamID lobby) { lobbies.push_back(lobby); }
    void clearLobbies() { lobbies.clear(); }

private:
    SteamNetworkingManager *networkingManager_;
    CSteamID currentLobby;
    std::vector<CSteamID> lobbies;
    SteamFriendsCallbacks *steamFriendsCallbacks;
    SteamMatchmakingCallbacks *steamMatchmakingCallbacks;
};