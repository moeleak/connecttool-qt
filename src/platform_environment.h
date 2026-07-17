#pragma once

#include <QString>

namespace connecttool::platform {

// Normalizes the process environment before SteamAPI_Init. This is a no-op on
// platforms that do not need a Steam client lookup workaround.
void prepareSteamEnvironment();

[[nodiscard]] bool currentUserIsAdministrator();
[[nodiscard]] bool hasTunPrivileges();

[[nodiscard]] QString appleScriptEscape(QString value);
[[nodiscard]] QString shellEscape(QString value);
[[nodiscard]] QString locateBundledHelperAsset(const QString &fileName);

} // namespace connecttool::platform
