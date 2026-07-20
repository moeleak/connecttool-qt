#include "integrations/steam/steam_reliable_channel.h"

#include <cstdint>
#include <limits>

namespace connecttool::steam {

network::SendStatus SteamReliableChannel::send(std::span<const std::byte> payload) {
  if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return network::SendStatus::Dropped;
  }

  const auto result = sockets_.SendMessageToConnection(
      connection_, payload.data(), static_cast<std::uint32_t>(payload.size()),
      k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_NoNagle, nullptr);
  if (result == k_EResultOK) {
    return network::SendStatus::Sent;
  }
  if (result == k_EResultLimitExceeded) {
    return network::SendStatus::Backpressured;
  }
  return network::SendStatus::Dropped;
}

std::size_t SteamReliableChannel::pendingReliableBytes() const {
  SteamNetConnectionRealTimeStatus_t status{};
  if (sockets_.GetConnectionRealTimeStatus(connection_, &status, 0, nullptr) != k_EResultOK ||
      status.m_cbPendingReliable <= 0) {
    return 0;
  }
  return static_cast<std::size_t>(status.m_cbPendingReliable);
}

} // namespace connecttool::steam
