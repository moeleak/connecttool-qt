#pragma once
#include <string>
#include <vector>
#include <steam_api.h>

class SteamUtils {
public:
    struct FriendInfo
    {
        CSteamID id;
        std::string name;
        std::string avatarDataUrl;
        EPersonaState personaState;
        bool online;
    };

    static std::vector<FriendInfo> getFriendsList();
};
