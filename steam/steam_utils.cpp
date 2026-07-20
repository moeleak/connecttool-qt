#include "steam_utils.h"
#include <QBuffer>
#include <QByteArray>
#include <QIODevice>
#include <QImage>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <unordered_map>

namespace {
using AvatarRequestClock = std::chrono::steady_clock;

constexpr auto kAvatarRequestRetryInterval = std::chrono::seconds(5);

std::mutex avatarRequestMutex;
std::unordered_map<uint64_t, AvatarRequestClock::time_point> avatarRequests;

void requestAvatarIfNeeded(const CSteamID &id) {
  const auto key = id.ConvertToUint64();
  const auto now = AvatarRequestClock::now();
  {
    std::lock_guard lock(avatarRequestMutex);
    const auto request = avatarRequests.find(key);
    if (request != avatarRequests.end() && now - request->second < kAvatarRequestRetryInterval) {
      return;
    }
    avatarRequests[key] = now;
  }

  // Passing false is important: true requests only the persona name and leaves
  // non-friend avatars unavailable.
  SteamFriends()->RequestUserInformation(id, false);
}

void finishAvatarRequest(const CSteamID &id) {
  std::lock_guard lock(avatarRequestMutex);
  avatarRequests.erase(id.ConvertToUint64());
}

std::string buildAvatarDataUrl(int imageHandle) {
  if (imageHandle <= 0 || ::SteamUtils() == nullptr) {
    return {};
  }
  uint32 width = 0;
  uint32 height = 0;
  if (!::SteamUtils()->GetImageSize(imageHandle, &width, &height) ||
      width == 0 || height == 0) {
    return {};
  }
  const size_t bytes =
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  std::vector<uint8_t> rgba(bytes);
  if (!::SteamUtils()->GetImageRGBA(imageHandle, rgba.data(),
                                    static_cast<int>(rgba.size()))) {
    return {};
  }

  QImage image(width, height, QImage::Format_RGBA8888);
  std::memcpy(image.bits(), rgba.data(), rgba.size());

  QByteArray pngData;
  QBuffer buffer(&pngData);
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, "PNG")) {
    return {};
  }

  QByteArray dataUrl("data:image/png;base64,");
  dataUrl.append(pngData.toBase64());
  return dataUrl.toStdString();
}
} // namespace

std::vector<SteamUtils::FriendInfo> SteamUtils::getFriendsList() {
  std::vector<FriendInfo> friendsList;
  if (!SteamFriends()) {
    return friendsList;
  }
  int friendCount = SteamFriends()->GetFriendCount(k_EFriendFlagAll);
  friendsList.reserve(friendCount);
  for (int i = 0; i < friendCount; ++i) {
    CSteamID friendID = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagAll);
    const char *name = SteamFriends()->GetFriendPersonaName(friendID);
    std::string avatar = getAvatarDataUrl(friendID);
    EPersonaState persona = SteamFriends()->GetFriendPersonaState(friendID);
    const bool isOnline = persona != k_EPersonaStateOffline &&
                          persona != k_EPersonaStateInvisible;
    friendsList.push_back(
        {friendID, name ? name : "", std::move(avatar), persona, isOnline});
  }
  return friendsList;
}

std::string SteamUtils::getAvatarDataUrl(const CSteamID &id) {
  if (!SteamFriends()) {
    return {};
  }

  // The 64 px source stays sharp in the 42 px Material avatar while avoiding
  // the memory cost of keeping full-size profile images for every room member.
  const int handle = SteamFriends()->GetMediumFriendAvatar(id);
  if (handle <= 0) {
    requestAvatarIfNeeded(id);
    return {};
  }

  finishAvatarRequest(id);
  return buildAvatarDataUrl(handle);
}
