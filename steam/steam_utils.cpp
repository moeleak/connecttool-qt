#include "steam_utils.h"
#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QIODevice>
#include <cstring>
#include <iostream>

namespace
{
std::string buildAvatarDataUrl(int imageHandle)
{
    if (imageHandle <= 0 || ::SteamUtils() == nullptr)
    {
        return {};
    }
    uint32 width = 0;
    uint32 height = 0;
    if (!::SteamUtils()->GetImageSize(imageHandle, &width, &height) || width == 0 || height == 0)
    {
        return {};
    }
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    std::vector<uint8_t> rgba(bytes);
    if (!::SteamUtils()->GetImageRGBA(imageHandle, rgba.data(), static_cast<int>(rgba.size())))
    {
        return {};
    }

    QImage image(width, height, QImage::Format_RGBA8888);
    std::memcpy(image.bits(), rgba.data(), rgba.size());

    QByteArray pngData;
    QBuffer buffer(&pngData);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
    {
        return {};
    }

    QByteArray dataUrl("data:image/png;base64,");
    dataUrl.append(pngData.toBase64());
    return dataUrl.toStdString();
}
} // namespace

std::vector<SteamUtils::FriendInfo> SteamUtils::getFriendsList()
{
    std::vector<FriendInfo> friendsList;
    if (!SteamFriends())
    {
        return friendsList;
    }
    int friendCount = SteamFriends()->GetFriendCount(k_EFriendFlagAll);
    friendsList.reserve(friendCount);
    for (int i = 0; i < friendCount; ++i)
    {
        CSteamID friendID = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagAll);
        const char *name = SteamFriends()->GetFriendPersonaName(friendID);
        std::string avatar = buildAvatarDataUrl(SteamFriends()->GetSmallFriendAvatar(friendID));
        EPersonaState persona = SteamFriends()->GetFriendPersonaState(friendID);
        const bool isOnline = persona != k_EPersonaStateOffline && persona != k_EPersonaStateInvisible;
        friendsList.push_back({friendID, name ? name : "", std::move(avatar), persona, isOnline});
    }
    return friendsList;
}
