#include "steam_room_manager.h"
#include "steam_networking_manager.h"
#include <iostream>
#include <algorithm>

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

SteamMatchmakingCallbacks::SteamMatchmakingCallbacks(SteamNetworkingManager *manager) : manager_(manager) {}

void SteamMatchmakingCallbacks::OnLobbyCreated(LobbyCreated_t *pCallback)
{
    if (pCallback->m_eResult == k_EResultOK)
    {
        manager_->roomManager_->setCurrentLobby(pCallback->m_ulSteamIDLobby);
        std::cout << "Lobby created: " << manager_->roomManager_->getCurrentLobby().ConvertToUint64() << std::endl;
        // Set Rich Presence with lobby ID
        std::string lobbyStr = std::to_string(manager_->roomManager_->getCurrentLobby().ConvertToUint64());
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
    manager_->roomManager_->clearLobbies();
    for (uint32 i = 0; i < pCallback->m_nLobbiesMatching; ++i)
    {
        CSteamID lobbyID = SteamMatchmaking()->GetLobbyByIndex(i);
        manager_->roomManager_->addLobby(lobbyID);
    }
    std::cout << "Received " << pCallback->m_nLobbiesMatching << " lobbies" << std::endl;
}

void SteamMatchmakingCallbacks::OnLobbyEntered(LobbyEnter_t *pCallback)
{
    if (pCallback->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess)
    {
        manager_->roomManager_->setCurrentLobby(pCallback->m_ulSteamIDLobby);
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

SteamRoomManager::SteamRoomManager(SteamNetworkingManager *networkingManager)
    : networkingManager_(networkingManager), currentLobby(k_steamIDNil),
      steamFriendsCallbacks(nullptr), steamMatchmakingCallbacks(nullptr)
{
    steamFriendsCallbacks = new SteamFriendsCallbacks(networkingManager_);
    steamMatchmakingCallbacks = new SteamMatchmakingCallbacks(networkingManager_);
}

SteamRoomManager::~SteamRoomManager()
{
    delete steamFriendsCallbacks;
    delete steamMatchmakingCallbacks;
}

bool SteamRoomManager::createLobby()
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

void SteamRoomManager::leaveLobby()
{
    if (currentLobby != k_steamIDNil)
    {
        SteamMatchmaking()->LeaveLobby(currentLobby);
        currentLobby = k_steamIDNil;
    }
}

bool SteamRoomManager::searchLobbies()
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

bool SteamRoomManager::joinLobby(CSteamID lobbyID)
{
    if (SteamMatchmaking()->JoinLobby(lobbyID) != k_EResultOK)
    {
        std::cerr << "Failed to join lobby" << std::endl;
        return false;
    }
    // Connection will be handled by callback
    return true;
}

bool SteamRoomManager::startHosting()
{
    if (!createLobby())
    {
        return false;
    }

    networkingManager_->hListenSock = networkingManager_->m_pInterface->CreateListenSocketP2P(0, 0, nullptr);

    if (networkingManager_->hListenSock != k_HSteamListenSocket_Invalid)
    {
        networkingManager_->g_isHost = true;
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

void SteamRoomManager::stopHosting()
{
    if (networkingManager_->hListenSock != k_HSteamListenSocket_Invalid)
    {
        networkingManager_->m_pInterface->CloseListenSocket(networkingManager_->hListenSock);
        networkingManager_->hListenSock = k_HSteamListenSocket_Invalid;
    }
    leaveLobby();
    networkingManager_->g_isHost = false;
}