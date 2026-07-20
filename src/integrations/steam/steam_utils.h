#pragma once
#include <steam_api.h>
#include <string>
#include <vector>

class SteamUtils {
public:
  struct FriendInfo {
    CSteamID id;
    std::string name;
    std::string avatarDataUrl;
    EPersonaState personaState;
    bool online;
  };

  static std::vector<FriendInfo> getFriendsList();
  static std::string getAvatarDataUrl(const CSteamID &id);
};
