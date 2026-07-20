#pragma once

#include "network/tcp/reliable_channel.h"

#include <isteamnetworkingsockets.h>
#include <steamnetworkingtypes.h>

namespace connecttool::steam {

class SteamReliableChannel final : public network::ReliableChannel {
public:
  SteamReliableChannel(ISteamNetworkingSockets &sockets, HSteamNetConnection connection)
      : sockets_(sockets), connection_(connection) {}

  [[nodiscard]] network::SendStatus send(std::span<const std::byte> payload) override;
  [[nodiscard]] std::size_t pendingReliableBytes() const override;

private:
  ISteamNetworkingSockets &sockets_;
  HSteamNetConnection connection_;
};

} // namespace connecttool::steam
