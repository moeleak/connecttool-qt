#include "steam_room_manager.h"
#include "steam_networking_manager.h"
#include <iostream>
#include <algorithm>

SteamFriendsCallbacks::SteamFriendsCallbacks(SteamNetworkingManager *manager, SteamRoomManager *roomManager) 
    : manager_(manager), roomManager_(roomManager)
{
    std::cout << "SteamFriendsCallbacks constructor called" << std::endl;
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
            roomManager_->joinLobby(lobbyID);
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

SteamMatchmakingCallbacks::SteamMatchmakingCallbacks(SteamNetworkingManager *manager, SteamRoomManager *roomManager) 
    : manager_(manager), roomManager_(roomManager)
{}

void SteamMatchmakingCallbacks::OnLobbyCreated(LobbyCreated_t *pCallback, bool bIOFailure)
{
    if (bIOFailure)
    {
        std::cerr << "Failed to create lobby - IO Failure" << std::endl;
        return;
    }
    if (pCallback->m_eResult == k_EResultOK)
    {
        roomManager_->setCurrentLobby(pCallback->m_ulSteamIDLobby);
        std::cout << "Lobby created: " << roomManager_->getCurrentLobby().ConvertToUint64() << std::endl;
        
        // Set Rich Presence to enable invite functionality
        SteamFriends()->SetRichPresence("steam_display", "#Status_InLobby");
        SteamFriends()->SetRichPresence("connect", std::to_string(pCallback->m_ulSteamIDLobby).c_str());
    }
    else
    {
        std::cerr << "Failed to create lobby" << std::endl;
    }
}

void SteamMatchmakingCallbacks::OnLobbyListReceived(LobbyMatchList_t *pCallback, bool bIOFailure)
{
    if (bIOFailure)
    {
        std::cerr << "Failed to receive lobby list - IO Failure" << std::endl;
        return;
    }
    roomManager_->clearLobbies();
    for (uint32 i = 0; i < pCallback->m_nLobbiesMatching; ++i)
    {
        CSteamID lobbyID = SteamMatchmaking()->GetLobbyByIndex(i);
        roomManager_->addLobby(lobbyID);
    }
    std::cout << "Received " << pCallback->m_nLobbiesMatching << " lobbies" << std::endl;
}

void SteamMatchmakingCallbacks::OnLobbyEntered(LobbyEnter_t *pCallback)
{
    if (pCallback->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess)
    {
        roomManager_->setCurrentLobby(pCallback->m_ulSteamIDLobby);
        if (manager_)
        {
            CSteamID hostID = SteamMatchmaking()->GetLobbyOwner(pCallback->m_ulSteamIDLobby);
            manager_->setHostSteamID(hostID);
        }
        std::cout << "Entered lobby: " << pCallback->m_ulSteamIDLobby << std::endl;
        
        // Set Rich Presence to enable invite functionality
        SteamFriends()->SetRichPresence("steam_display", "#Status_InLobby");
        SteamFriends()->SetRichPresence("connect", std::to_string(pCallback->m_ulSteamIDLobby).c_str());
        
        // Only join host if not the host
        if (!manager_->isHost())
        {
            CSteamID hostID = SteamMatchmaking()->GetLobbyOwner(pCallback->m_ulSteamIDLobby);
            if (manager_->joinHost(hostID.ConvertToUint64()))
            {
                // Start TCP Server if dependencies are set
                if (manager_->getServer() && !(*manager_->getServer()))
                {
                    const int bindPort = manager_->getBindPort();
                    *manager_->getServer() = std::make_unique<TCPServer>(bindPort, manager_);
                    if (!(*manager_->getServer())->start())
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
    steamFriendsCallbacks = new SteamFriendsCallbacks(networkingManager_, this);
    steamMatchmakingCallbacks = new SteamMatchmakingCallbacks(networkingManager_, this);

    // Clear Rich Presence on initialization to prevent "Invite to game" showing when not in a lobby
    SteamFriends()->ClearRichPresence();
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
    // Register the call result
    steamMatchmakingCallbacks->m_CallResultLobbyCreated.Set(hSteamAPICall, steamMatchmakingCallbacks, &SteamMatchmakingCallbacks::OnLobbyCreated);
    return true;
}

void SteamRoomManager::leaveLobby()
{
    if (currentLobby != k_steamIDNil)
    {
        SteamMatchmaking()->LeaveLobby(currentLobby);
        currentLobby = k_steamIDNil;
        if (networkingManager_)
        {
            networkingManager_->setHostSteamID(CSteamID());
        }
        
        // Clear Rich Presence when leaving lobby
        SteamFriends()->ClearRichPresence();
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
    // Register the call result
    steamMatchmakingCallbacks->m_CallResultLobbyMatchList.Set(hSteamAPICall, steamMatchmakingCallbacks, &SteamMatchmakingCallbacks::OnLobbyListReceived);
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

    networkingManager_->getListenSock() = networkingManager_->getInterface()->CreateListenSocketP2P(0, 0, nullptr);

    if (networkingManager_->getListenSock() != k_HSteamListenSocket_Invalid)
    {
        networkingManager_->getIsHost() = true;
        std::cout << "Created listen socket for hosting game room" << std::endl;
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
    if (networkingManager_->getListenSock() != k_HSteamListenSocket_Invalid)
    {
        networkingManager_->getInterface()->CloseListenSocket(networkingManager_->getListenSock());
        networkingManager_->getListenSock() = k_HSteamListenSocket_Invalid;
    }
    leaveLobby();
    networkingManager_->getIsHost() = false;
}

std::vector<CSteamID> SteamRoomManager::getLobbyMembers() const
{
    std::vector<CSteamID> members;
    if (currentLobby != k_steamIDNil)
    {
        int numMembers = SteamMatchmaking()->GetNumLobbyMembers(currentLobby);
        for (int i = 0; i < numMembers; ++i)
        {
            CSteamID memberID = SteamMatchmaking()->GetLobbyMemberByIndex(currentLobby, i);
            members.push_back(memberID);
        }
    }
    return members;
}
