#pragma once

#include "domain/strong_id.h"

#include <steam_api.h>

namespace connecttool::steam {

[[nodiscard]] inline domain::PeerId toPeerId(CSteamID id) {
  return domain::PeerId{id.ConvertToUint64()};
}

[[nodiscard]] inline CSteamID toSteamId(domain::PeerId id) {
  return CSteamID{static_cast<uint64>(id.value())};
}

} // namespace connecttool::steam
