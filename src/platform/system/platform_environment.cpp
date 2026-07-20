#include "platform/system/platform_environment.h"

#ifdef Q_OS_MACOS
#include "platform/tun/tun_privileged_helper.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

#ifdef Q_OS_LINUX
#include <pwd.h>
#include <sys/types.h>
#endif

namespace connecttool::platform {

namespace {

#ifdef Q_OS_WIN
[[nodiscard]] QString normalizePathForCompare(const QString &path) {
  return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

[[nodiscard]] QString findSteamInstallDir() {
  QStringList candidates;
  const auto addCandidate = [&candidates](const QString &path) {
    if (!path.isEmpty()) {
      candidates.push_back(path);
    }
  };

  {
    QSettings steamRegistry("HKEY_CURRENT_USER\\Software\\Valve\\Steam", QSettings::NativeFormat);
    addCandidate(steamRegistry.value(QStringLiteral("SteamPath")).toString());
    const QString steamExecutable = steamRegistry.value(QStringLiteral("SteamExe")).toString();
    if (!steamExecutable.isEmpty()) {
      addCandidate(QFileInfo(steamExecutable).absolutePath());
    }
  }
  {
    QSettings steamRegistry("HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam", QSettings::NativeFormat);
    addCandidate(steamRegistry.value(QStringLiteral("InstallPath")).toString());
  }
  {
    QSettings steamRegistry("HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Valve\\Steam",
                            QSettings::NativeFormat);
    addCandidate(steamRegistry.value(QStringLiteral("InstallPath")).toString());
  }

  const QString programFiles = qEnvironmentVariable("ProgramFiles");
  if (!programFiles.isEmpty()) {
    addCandidate(QDir(programFiles).filePath(QStringLiteral("Steam")));
  }
  const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
  if (!programFilesX86.isEmpty()) {
    addCandidate(QDir(programFilesX86).filePath(QStringLiteral("Steam")));
  }

  const bool is64Bit = sizeof(void *) == 8;
  const QString preferredLibrary =
      is64Bit ? QStringLiteral("steamclient64.dll") : QStringLiteral("steamclient.dll");
  const QString fallbackLibrary =
      is64Bit ? QStringLiteral("steamclient.dll") : QStringLiteral("steamclient64.dll");

  const auto containing = [&candidates](const QString &fileName) {
    for (const QString &candidate : candidates) {
      const QString directory = QDir::cleanPath(QDir::fromNativeSeparators(candidate));
      if (!directory.isEmpty() && QFileInfo::exists(QDir(directory).filePath(fileName))) {
        return directory;
      }
    }
    return QString{};
  };

  if (const QString preferred = containing(preferredLibrary); !preferred.isEmpty()) {
    return preferred;
  }
  if (const QString fallback = containing(fallbackLibrary); !fallback.isEmpty()) {
    return fallback;
  }
  return containing(QStringLiteral("steam.exe"));
}

void prepareWindowsSteamEnvironment() {
  const QString steamDirectory = findSteamInstallDir();
  if (steamDirectory.isEmpty()) {
    return;
  }

  const QString normalizedSteam = normalizePathForCompare(steamDirectory);
  const QString currentPath = QString::fromLocal8Bit(qgetenv("PATH"));
  const QStringList entries = currentPath.split(';', Qt::SkipEmptyParts);
  const bool alreadyPresent =
      std::ranges::any_of(entries, [&normalizedSteam](const QString &entry) {
        return normalizePathForCompare(entry) == normalizedSteam;
      });
  if (!alreadyPresent) {
    qputenv("PATH", (QDir::toNativeSeparators(steamDirectory) + QStringLiteral(";") + currentPath)
                        .toLocal8Bit());
  }

  const std::wstring nativeDirectory = QDir::toNativeSeparators(steamDirectory).toStdWString();
  SetDllDirectoryW(nativeDirectory.c_str());
}
#endif

#ifdef Q_OS_LINUX
[[nodiscard]] bool isFlatpakRuntime() {
  return qEnvironmentVariableIsSet("FLATPAK_ID") ||
         QFileInfo::exists(QStringLiteral("/run/.flatpak-info"));
}

[[nodiscard]] bool steamClientExistsInHome(const QString &homePath) {
  if (homePath.isEmpty()) {
    return false;
  }
  const QDir home(homePath);
  constexpr std::array candidates{
      ".steam/sdk64/steamclient.so",
      ".steam/sdk32/steamclient.so",
      ".local/share/Steam/linux64/steamclient.so",
      ".local/share/Steam/linux32/steamclient.so",
      ".local/share/Steam/ubuntu12_64/steamclient.so",
      ".local/share/Steam/ubuntu12_32/steamclient.so",
  };
  return std::ranges::any_of(candidates, [&home](const char *relativePath) {
    return QFileInfo::exists(home.filePath(QString::fromLatin1(relativePath)));
  });
}

[[nodiscard]] bool steamPidIndicatesRunning(const QString &pidPath) {
  QFile pidFile(pidPath);
  if (!pidFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }
  bool ok = false;
  const qint64 pid = pidFile.readLine().trimmed().toLongLong(&ok);
  return ok && pid > 0;
}

[[nodiscard]] bool steamRuntimeActiveInHome(const QString &homePath) {
  if (homePath.isEmpty()) {
    return false;
  }
  const QDir home(homePath);
  const std::array pidCandidates{
      home.filePath(QStringLiteral(".steam/steam.pid")),
      home.filePath(QStringLiteral(".steam/root/steam.pid")),
      home.filePath(QStringLiteral(".local/share/Steam/steam.pid")),
  };
  if (std::ranges::any_of(pidCandidates, [](const QString &path) {
        return QFileInfo::exists(path) && steamPidIndicatesRunning(path);
      })) {
    return true;
  }
  return QFileInfo::exists(home.filePath(QStringLiteral(".steam/steam.pipe")));
}

void prepareFlatpakSteamEnvironment() {
  if (!isFlatpakRuntime()) {
    return;
  }
  const QByteArray currentHome = qgetenv("HOME");
  if (currentHome.isEmpty()) {
    return;
  }
  const QString currentHomePath = QString::fromLocal8Bit(currentHome);
  if (steamRuntimeActiveInHome(currentHomePath)) {
    return;
  }

  const QString flatpakHome =
      QDir(currentHomePath).filePath(QStringLiteral(".var/app/com.valvesoftware.Steam"));
  if (steamRuntimeActiveInHome(flatpakHome)) {
    qputenv("HOME", flatpakHome.toLocal8Bit());
    return;
  }
  if (!steamClientExistsInHome(currentHomePath) && steamClientExistsInHome(flatpakHome)) {
    qputenv("HOME", flatpakHome.toLocal8Bit());
  }
}

void prepareSudoSteamEnvironment() {
  if (geteuid() != 0) {
    return;
  }
  const QByteArray sudoUser = qgetenv("SUDO_USER");
  const QByteArray sudoHome = qgetenv("SUDO_HOME");
  if (sudoUser.isEmpty() && sudoHome.isEmpty()) {
    return;
  }

  QByteArray targetHome = sudoHome;
  uid_t targetUserId = 0;
  if (targetHome.isEmpty() && !sudoUser.isEmpty()) {
    struct passwd user{};
    struct passwd *result = nullptr;
    long bufferSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufferSize < 0) {
      bufferSize = 16'384;
    }
    std::vector<char> buffer(static_cast<std::size_t>(bufferSize));
    if (getpwnam_r(sudoUser.constData(), &user, buffer.data(), buffer.size(), &result) == 0 &&
        result != nullptr && result->pw_dir != nullptr) {
      targetHome = QByteArray(result->pw_dir);
      targetUserId = result->pw_uid;
    }
  } else if (!sudoUser.isEmpty()) {
    if (struct passwd *user = getpwnam(sudoUser.constData())) {
      targetUserId = user->pw_uid;
    }
  }

  if (!targetHome.isEmpty() && qgetenv("HOME") != targetHome) {
    qputenv("HOME", targetHome);
  }
  if (targetUserId != 0 && qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR")) {
    const QByteArray runtimeDirectory =
        QByteArray("/run/user/") +
        QByteArray::number(static_cast<unsigned long long>(targetUserId));
    if (access(runtimeDirectory.constData(), R_OK | X_OK) == 0) {
      qputenv("XDG_RUNTIME_DIR", runtimeDirectory);
    }
  }
}
#endif

} // namespace

void prepareSteamEnvironment() {
#ifdef Q_OS_WIN
  prepareWindowsSteamEnvironment();
#elif defined(Q_OS_LINUX)
  prepareSudoSteamEnvironment();
  prepareFlatpakSteamEnvironment();
#endif
}

bool currentUserIsAdministrator() {
#ifdef Q_OS_WIN
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return false;
  }
  TOKEN_ELEVATION elevation{};
  DWORD returnLength = 0;
  const BOOL succeeded =
      GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returnLength);
  CloseHandle(token);
  return succeeded && elevation.TokenIsElevated;
#elif defined(Q_OS_UNIX)
  return geteuid() == 0;
#else
  return true;
#endif
}

bool hasTunPrivileges() {
#ifdef Q_OS_MACOS
  return currentUserIsAdministrator() || tun::helperAvailable();
#else
  return currentUserIsAdministrator();
#endif
}

QString appleScriptEscape(QString value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  return value;
}

QString shellEscape(QString value) {
  value.replace("'", "'\\''");
  return QStringLiteral("'") + value + QStringLiteral("'");
}

QString locateBundledHelperAsset(const QString &fileName) {
  const QDir applicationDirectory(QCoreApplication::applicationDirPath());
  const std::array candidates{
      applicationDirectory.absoluteFilePath(QStringLiteral("../Resources/%1").arg(fileName)),
      applicationDirectory.absoluteFilePath(fileName),
  };
  const auto existing =
      std::ranges::find_if(candidates, [](const QString &path) { return QFileInfo::exists(path); });
  return existing == candidates.end() ? QString{} : QFileInfo(*existing).absoluteFilePath();
}

} // namespace connecttool::platform
