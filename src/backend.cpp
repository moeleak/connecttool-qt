#include "backend.h"

#include "../net/tcp_server.h"
#include "../steam/steam_networking_manager.h"
#include "../steam/steam_room_manager.h"
#include "../steam/steam_utils.h"
#include "../steam/steam_vpn_bridge.h"
#include "../steam/steam_vpn_networking_manager.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QQmlEngine>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QWindow>
#include <QVariantMap>
#include <QtDebug>
#include <algorithm>
#include <array>
#include <chrono>
#include <isteamnetworkingutils.h>
#include <limits>
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
#include <mutex>
#include <set>
#include <sstream>
#include <steam_api.h>
#include <steamnetworkingtypes.h>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef CONNECTTOOL_VERSION
#define CONNECTTOOL_VERSION "0.0.0"
#endif

namespace {
struct PersonaDisplay {
  QString label;
  bool online;
  int priority;
};

bool currentUserIsAdmin() {
#ifdef Q_OS_WIN
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return false;
  }
  TOKEN_ELEVATION elevation{};
  DWORD returnLength = 0;
  const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
                                      sizeof(elevation), &returnLength);
  CloseHandle(token);
  return ok && elevation.TokenIsElevated;
#elif defined(Q_OS_UNIX)
  return geteuid() == 0;
#else
  return true;
#endif
}

PersonaDisplay personaStateDisplay(EPersonaState state) {
  switch (state) {
  case k_EPersonaStateOnline:
    return {QCoreApplication::translate("Backend", "在线"), true, 0};
  case k_EPersonaStateBusy:
    return {QCoreApplication::translate("Backend", "忙碌"), true, 1};
  case k_EPersonaStateLookingToPlay:
    return {QCoreApplication::translate("Backend", "想游戏"), true, 2};
  case k_EPersonaStateLookingToTrade:
    return {QCoreApplication::translate("Backend", "想交易"), true, 3};
  case k_EPersonaStateSnooze:
    return {QCoreApplication::translate("Backend", "小憩"), true, 4};
  case k_EPersonaStateAway:
    return {QCoreApplication::translate("Backend", "离开"), true, 5};
  case k_EPersonaStateInvisible:
    return {QCoreApplication::translate("Backend", "隐身"), false, 7};
  case k_EPersonaStateOffline:
  default:
    return {QCoreApplication::translate("Backend", "离线"), false, 8};
  }
}

QString defaultRoomName() {
  QString ownerName;
  if (SteamFriends()) {
    const char *persona = SteamFriends()->GetPersonaName();
    if (persona && persona[0] != '\0') {
      ownerName = QString::fromUtf8(persona);
    }
  }
  if (ownerName.isEmpty()) {
    ownerName = QCoreApplication::translate("Backend", "ConnectTool");
  }
  return QCoreApplication::translate("Backend", "%1 的房间").arg(ownerName);
}

void steamWarningHook(int /*severity*/, const char *msg) {
  qDebug() << "[SteamAPI]" << msg;
}

void steamNetDebugHook(ESteamNetworkingSocketsDebugOutputType /*type*/,
                       const char *msg) {
  static QString lastMsg;
  static int repeatCount = 0;
  static auto lastLog = std::chrono::steady_clock::now();

  const QString current = QString::fromUtf8(msg ? msg : "");
  const auto now = std::chrono::steady_clock::now();

  if (current == lastMsg &&
      now - lastLog < std::chrono::seconds(2)) {
    ++repeatCount;
    return;
  }

  if (repeatCount > 0 && !lastMsg.isEmpty()) {
    qDebug() << "[SteamNet]" << lastMsg
             << "(repeated" << repeatCount << "times)";
  }

  repeatCount = 0;
  lastMsg = current;
  lastLog = now;

  qDebug() << "[SteamNet]" << current;
}

QString renderPopId(SteamNetworkingPOPID pop) {
  SteamNetworkingPOPIDRender render(pop);
  const char *text = render.c_str();
  if (text && text[0] != '\0') {
    return QString::fromLatin1(text);
  }
  return QStringLiteral("0x%1")
      .arg(static_cast<uint32_t>(pop), 8, 16, QLatin1Char('0'))
      .toUpper();
}

#ifdef Q_OS_LINUX
// When the app is launched with sudo while Steam runs under a normal user,
// SteamAPI_Init will look in root's home (e.g. /root/.steam) and think Steam
// is not running. Prefer the invoking user's home/runtime if available.
void fixSteamEnvForSudo() {
  if (geteuid() != 0) {
    return;
  }
  const QByteArray sudoUser = qgetenv("SUDO_USER");
  const QByteArray sudoHomeEnv = qgetenv("SUDO_HOME");
  if (sudoUser.isEmpty() && sudoHomeEnv.isEmpty()) {
    return;
  }

  QByteArray targetHome = sudoHomeEnv;
  uid_t targetUid = 0;
  if (targetHome.isEmpty() && !sudoUser.isEmpty()) {
    struct passwd pwd{};
    struct passwd *result = nullptr;
    long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufsize < 0) {
      bufsize = 16384;
    }
    std::vector<char> buffer(static_cast<size_t>(bufsize));
    if (getpwnam_r(sudoUser.constData(), &pwd, buffer.data(), buffer.size(),
                   &result) == 0 &&
        result != nullptr && result->pw_dir != nullptr) {
      targetHome = QByteArray(result->pw_dir);
      targetUid = result->pw_uid;
    }
  } else if (!sudoUser.isEmpty()) {
    if (struct passwd *pwd = getpwnam(sudoUser.constData())) {
      targetUid = pwd->pw_uid;
    }
  }

  if (!targetHome.isEmpty() && qgetenv("HOME") != targetHome) {
    qputenv("HOME", targetHome);
  }

  if (targetUid != 0 && qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR")) {
    QByteArray runtime =
        QByteArray("/run/user/") +
        QByteArray::number(static_cast<unsigned long long>(targetUid));
    // Only set it if the path exists; otherwise leave as-is.
    if (access(runtime.constData(), R_OK | X_OK) == 0) {
      qputenv("XDG_RUNTIME_DIR", runtime);
    }
  }
}
#endif

QString stripGhProxyPrefix(const QString &url) {
  const QString prefix = QStringLiteral("https://gh-proxy.org/");
  if (url.startsWith(prefix)) {
    return url.mid(prefix.size());
  }
  return url;
}
} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent), steamReady_(false), localPort_(25565),
      localBindPort_(8888), lastTcpClients_(0), lastMemberLogCount_(-1),
      roomName_(QCoreApplication::translate("Backend", "ConnectTool 房间")),
      appVersion_(QStringLiteral(CONNECTTOOL_VERSION)) {
  // Set a default app id so Steam can bootstrap in development environments
  qputenv("SteamAppId", QByteArray("480"));
  qputenv("SteamGameId", QByteArray("480"));
  updateStatusText_.clear();

#ifdef Q_OS_LINUX
  fixSteamEnvForSudo();
#endif

  workGuard_ = std::make_unique<
      boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
      boost::asio::make_work_guard(ioContext_));
  ioThread_ = std::thread([this]() { ioContext_.run(); });

  lobbiesModel_.setFilter(lobbyFilter_);
  lobbiesModel_.setSortMode(lobbySortMode_);

  connect(&callbackTimer_, &QTimer::timeout, this, &Backend::tick);
  connect(&slowTimer_, &QTimer::timeout, this, &Backend::refreshFriends);
  friendsRefreshResetTimer_.setSingleShot(true);
  connect(&friendsRefreshResetTimer_, &QTimer::timeout, this,
          [this]() { setFriendsRefreshing(false); });
  statusOverrideTimer_.setSingleShot(true);
  connect(&statusOverrideTimer_, &QTimer::timeout, this,
          [this]() { clearStatusOverride(); });
  connect(&steamCheckTimer_, &QTimer::timeout, this, [this]() {
    if (steamReady_) {
      steamCheckTimer_.stop();
      return;
    }
    tryInitializeSteam();
  });

  connect(&cooldownTimer_, &QTimer::timeout, this, [this]() {
    bool anyChanged = false;
    int maxCooldown = 0;
    std::vector<std::pair<uint64_t, int>> updates;
    for (auto it = inviteCooldowns_.begin(); it != inviteCooldowns_.end();) {
      if (it->second > 0) {
        it->second -= 1;
        updates.emplace_back(it->first, it->second);
        if (it->second == 0) {
          it = inviteCooldowns_.erase(it);
        } else {
          maxCooldown = std::max(maxCooldown, it->second);
          ++it;
        }
        anyChanged = true;
      } else {
        it = inviteCooldowns_.erase(it);
      }
    }
    for (const auto &update : updates) {
      updateFriendCooldown(QString::number(update.first), update.second);
    }
    if (inviteCooldownSeconds_ != maxCooldown) {
      inviteCooldownSeconds_ = maxCooldown;
      emit inviteCooldownChanged();
    }
    if (!anyChanged && inviteCooldownSeconds_ != 0) {
      inviteCooldownSeconds_ = 0;
      emit inviteCooldownChanged();
    }
  });

  tryInitializeSteam();

  callbackTimer_.start(16);
  slowTimer_.start(15000);
  steamCheckTimer_.start(3000);
  cooldownTimer_.start(1000);

  updateStatus();
}

Backend::~Backend() {
  stopVpn();
  callbackTimer_.stop();
  slowTimer_.stop();
  steamCheckTimer_.stop();

  if (steamManager_) {
    steamManager_->stopMessageHandler();
  }

  if (server_) {
    server_->stop();
    server_.reset();
  }

  if (workGuard_) {
    workGuard_->reset();
  }

  ioContext_.stop();
  if (ioThread_.joinable()) {
    ioThread_.join();
  }

  // Let the Steam manager handle shutdown/SteamAPI cleanup in its destructor
  steamManager_.reset();
  roomManager_.reset();
}

void Backend::initializeSound(QWindow *window) {
  mainWindow_ = window;
  soundNotifier_.initialize(window);
}

bool Backend::isHost() const {
  if (!steamReady_) {
    return false;
  }
  if (inTunMode()) {
    return vpnHosting_;
  }
  return steamManager_ && steamManager_->isHost();
}

bool Backend::isConnected() const {
  if (!steamReady_) {
    return false;
  }
  if (inTunMode()) {
    return vpnConnected_;
  }
  return steamManager_ && steamManager_->isConnected();
}

QString Backend::lobbyId() const {
  if (!roomManager_) {
    return {};
  }
  CSteamID lobby = roomManager_->getCurrentLobby();
  return lobby.IsValid() ? QString::number(lobby.ConvertToUint64()) : QString();
}

QString Backend::selfSteamId() const {
  return selfSteamId_;
}

QString Backend::lobbyName() const {
  if (!steamReady_ || !roomManager_ || !SteamMatchmaking()) {
    return {};
  }

  const std::string name = roomManager_->getLobbyName();
  if (!name.empty()) {
    return QString::fromUtf8(name.c_str());
  }

  CSteamID lobby = roomManager_->getCurrentLobby();
  if (lobby.IsValid()) {
    CSteamID owner = SteamMatchmaking()->GetLobbyOwner(lobby);
    if (owner.IsValid() && SteamFriends()) {
      const char *ownerName = SteamFriends()->GetFriendPersonaName(owner);
      if (ownerName && ownerName[0] != '\0') {
        return QCoreApplication::translate("Backend", "%1 的房间")
            .arg(QString::fromUtf8(ownerName));
      }
    }
  }
  return {};
}

int Backend::tcpClients() const {
  return server_ ? server_->getClientCount() : 0;
}

void Backend::setJoinTarget(const QString &id) {
  if (joinTarget_ == id) {
    return;
  }
  joinTarget_ = id;
  if (joinTarget_ != lastAutoJoinTarget_) {
    lastAutoJoinTarget_.clear();
  }
  emit joinTargetChanged();
}

void Backend::setJoinTargetFromLobby(const QString &id) {
  lastAutoJoinTarget_ = id;
  setJoinTarget(id);
}

void Backend::setPublishLobby(bool publish) {
  if (publishLobby_ == publish) {
    return;
  }
  publishLobby_ = publish;
  emit publishLobbyChanged();
  if (roomManager_) {
    roomManager_->setPublishLobby(publishLobby_);
    roomManager_->refreshLobbyMetadata();
  }
}

void Backend::setLocalPort(int port) {
  port = std::max(0, port);
  if (localPort_ == port) {
    return;
  }
  localPort_ = port;
  emit localPortChanged();
}

void Backend::setLocalBindPort(int port) {
  port = std::clamp(port, 1, 65535);
  if (localBindPort_ == port) {
    return;
  }
  localBindPort_ = port;
  emit localBindPortChanged();
}

bool Backend::tryInitializeSteam() {
  if (steamReady_) {
    return true;
  }

  // Clean up any partial state before retrying.
  steamManager_.reset();
  roomManager_.reset();

  steamReady_ = SteamAPI_Init();
  if (!steamReady_) {
    status_ = tr("无法初始化 Steam API，请确认客户端已登录。");
    emit stateChanged();
    return false;
  }
  refreshSelfSteamId();

  if (SteamClient()) {
    SteamClient()->SetWarningMessageHook(&steamWarningHook);
  }
  if (SteamNetworkingUtils()) {
    SteamNetworkingUtils()->SetDebugOutputFunction(
        k_ESteamNetworkingSocketsDebugOutputType_Msg, &steamNetDebugHook);
  }

  roomName_ = defaultRoomName();
  emit roomNameChanged();

  steamManager_ = std::make_unique<SteamNetworkingManager>();
  if (!steamManager_->initialize()) {
    status_ = tr("Steam 网络初始化失败。");
    steamReady_ = false;
    steamManager_.reset();
    emit stateChanged();
    return false;
  }

  roomManager_ = std::make_unique<SteamRoomManager>(steamManager_.get());
  steamManager_->setRoomManager(roomManager_.get());
  roomManager_->setAdvertisedMode(inTunMode());
  roomManager_->setLobbyName(roomName_.toStdString());
  roomManager_->setPublishLobby(publishLobby_);
  roomManager_->setLobbyInviteCallback([this](const CSteamID &lobby) {
    const QString lobbyStr = QString::number(lobby.ConvertToUint64());
    QMetaObject::invokeMethod(
        this,
        [this, lobbyStr]() {
          setJoinTargetFromLobby(lobbyStr);
          joinLobby(lobbyStr);
        },
        Qt::QueuedConnection);
  });
  roomManager_->setLobbyModeChangedCallback([this](bool wantsTun,
                                                   const CSteamID &lobby) {
    QMetaObject::invokeMethod(
        this,
        [this, wantsTun, lobby]() { handleLobbyModeChanged(wantsTun, lobby); },
        Qt::QueuedConnection);
  });
  roomManager_->setHostLeftCallback([this]() {
    QMetaObject::invokeMethod(
        this, [this]() { disconnect(); }, Qt::QueuedConnection);
  });
  roomManager_->setChatMessageCallback([this](const CSteamID &sender,
                                              const std::string &payload) {
    const uint64_t senderId = sender.ConvertToUint64();
    const QString text =
        QString::fromUtf8(payload.data(), static_cast<int>(payload.size()));
    QMetaObject::invokeMethod(
        this, [this, senderId, text]() { handleChatMessage(senderId, text); },
        Qt::QueuedConnection);
  });
  roomManager_->setPinnedMessageChangedCallback(
      [this](const std::string &payload) {
        const QString text =
            QString::fromUtf8(payload.data(), static_cast<int>(payload.size()));
        QMetaObject::invokeMethod(
            this, [this, text]() { handlePinnedMessageMetadata(text); },
            Qt::QueuedConnection);
      });
  roomManager_->setLobbyListCallback(
      [this](const std::vector<SteamRoomManager::LobbyInfo> &lobbies) {
        QMetaObject::invokeMethod(
            this,
            [this, lobbies]() {
              updateLobbiesList(lobbies);
              setLobbyRefreshing(false);
            },
            Qt::QueuedConnection);
      });

  steamManager_->setMessageHandlerDependencies(ioContext_, server_, localPort_,
                                               localBindPort_);
  steamManager_->startMessageHandler();

  refreshSelfSteamId();
  refreshFriends();
  updateMembersList();
  refreshHostId();
  updateStatus();
  emit stateChanged();
  return true;
}

bool Backend::ensureSteamReady(const QString &actionLabel) {
  if (!steamReady_) {
    tryInitializeSteam();
  }
  if (steamReady_) {
    return true;
  }
  qWarning() << tr("无法%1：Steam 未初始化。").arg(actionLabel);
  return false;
}

bool Backend::hasAdminPrivileges() const { return currentUserIsAdmin(); }

bool Backend::ensureTunPrivileges() {
  if (hasAdminPrivileges()) {
    return true;
  }
  emit adminPrivilegesRequired();
  emit tunStartDenied();
  vpnWanted_ = false;
  vpnHosting_ = false;
  vpnStartAttempted_ = false;
  emit stateChanged();
  return false;
}

void Backend::startHosting() {
  if (!ensureSteamReady(tr("主持房间"))) {
    return;
  }
  if (isHost()) {
    qWarning() << tr("已经在主持房间中。");
    return;
  }
  if (isConnected()) {
    qWarning() << tr("当前已连接到房间，请先断开。");
    return;
  }

  if (roomManager_) {
    roomManager_->setAdvertisedMode(inTunMode());
  }

  if (inTunMode()) {
    if (!ensureTunPrivileges()) {
      return;
    }
    ensureVpnSetup();
    if (!vpnManager_ || !vpnBridge_) {
      return;
    }
    vpnWanted_ = true;
    vpnHosting_ = true;
    bool started = vpnBridge_->isRunning();
    if (!started) {
      started = vpnBridge_->start();
      updateVpnInfo();
    }
    if (!started) {
      qWarning() << tr("无法启动 TUN 设备，请检查权限或驱动。");
      vpnHosting_ = false;
      return;
    }
    if (roomManager_ && roomManager_->startHosting()) {
      vpnConnected_ = true;
      vpnBridge_->rebroadcastState();
      updateStatus();
      refreshLobbies();
    } else {
      qWarning() << tr("创建房间失败，请检查 Steam 状态。");
      vpnHosting_ = false;
    }
    return;
  }

  if (roomManager_ && roomManager_->startHosting()) {
    steamManager_->setHostSteamID(SteamUser()->GetSteamID());
    updateStatus();
    refreshLobbies();
  } else {
    qWarning() << tr("创建房间失败，请检查 Steam 状态。");
  }
}

void Backend::ensureServerRunning() {
  if (server_) {
    return;
  }
  server_ = std::make_unique<TCPServer>(localBindPort_, steamManager_.get());
  server_->setClientCountCallback([this](int count) {
    QMetaObject::invokeMethod(
        this,
        [this, count]() {
          lastTcpClients_ = count;
          emit serverChanged();
          updateStatus();
        },
        Qt::QueuedConnection);
  });
  if (!server_->start()) {
    qWarning() << tr("启动本地 TCP 服务器失败。");
    server_.reset();
    lastTcpClients_ = 0;
  }
  emit serverChanged();
}

void Backend::ensureVpnSetup() {
  if (!vpnManager_) {
    vpnManager_ = std::make_unique<SteamVpnNetworkingManager>();
    if (!vpnManager_->initialize()) {
      qWarning() << tr("Steam VPN 初始化失败。");
      vpnManager_.reset();
      return;
    }
  }
  if (!vpnBridge_) {
    vpnBridge_ = std::make_unique<SteamVpnBridge>(vpnManager_.get());
    vpnManager_->setVpnBridge(vpnBridge_.get());
  }
  if (roomManager_) {
    roomManager_->setVpnMode(inTunMode(), vpnManager_.get());
  }
  vpnManager_->startMessageHandler();
}

void Backend::stopVpn() {
  vpnConnected_ = false;
  vpnHosting_ = false;
  vpnStartAttempted_ = false;
  tunLocalIp_.clear();
  tunDeviceName_.clear();
  refreshSelfSteamId();
  if (vpnManager_) {
    vpnManager_->stopMessageHandler();
    vpnManager_->clearPeers();
  }
  if (vpnBridge_) {
    vpnBridge_->stop();
  }
  if (roomManager_) {
    roomManager_->setVpnMode(false, vpnManager_.get());
  }
}

void Backend::syncVpnPeers() {
  if (!inTunMode() || !vpnManager_ || !roomManager_) {
    return;
  }
  CSteamID currentLobby = roomManager_->getCurrentLobby();
  if (!currentLobby.IsValid()) {
    return;
  }
  std::set<CSteamID> desired;
  if (SteamUser()) {
    const CSteamID myId = SteamUser()->GetSteamID();
    for (const auto &member : roomManager_->getLobbyMembers()) {
      if (member != myId) {
        desired.insert(member);
      }
    }
  }
  vpnManager_->syncPeers(desired);
}

void Backend::updateVpnInfo() {
  if (!vpnBridge_) {
    return;
  }
  const QString nextIp =
      QString::fromStdString(vpnBridge_->getLocalIP()).trimmed();
  const QString nextDev =
      QString::fromStdString(vpnBridge_->getTunDeviceName()).trimmed();
  bool changed = false;
  if (nextIp != tunLocalIp_) {
    tunLocalIp_ = nextIp;
    changed = true;
  }
  if (nextDev != tunDeviceName_) {
    tunDeviceName_ = nextDev;
    changed = true;
  }
  if (changed) {
    emit stateChanged();
  }
}

void Backend::joinHost() {
  if (!ensureSteamReady(tr("加入房间"))) {
    return;
  }
  if (isConnected()) {
    qWarning() << tr("已经连接到房间，请先断开。");
    return;
  }

  clearStatusOverride();
  const auto markNotFound = [this]() { setStatusOverride(tr("房间不存在。")); };
  const QString trimmedTarget = joinTarget_.trimmed();
  if (trimmedTarget.isEmpty()) {
    startHosting();
    return;
  }

  bool ok = false;
  const uint64 hostID = trimmedTarget.toULongLong(&ok);
  if (!ok) {
    markNotFound();
    qWarning() << tr("房间不存在。");
    return;
  }

  CSteamID targetSteamID(hostID);
  if (!targetSteamID.IsValid()) {
    markNotFound();
    qWarning() << tr("房间不存在。");
    return;
  }

  const bool targetIsLobby = targetSteamID.IsLobby();
  const bool targetIsUser = targetSteamID.BIndividualAccount();
  if (!targetIsLobby && !targetIsUser) {
    markNotFound();
    qWarning() << tr("房间不存在。");
    return;
  }

  if (targetIsLobby) {
    applyLobbyModePreference(targetSteamID);
  }

  if (inTunMode()) {
    if (!ensureTunPrivileges()) {
      return;
    }
    ensureVpnSetup();
    if (!vpnManager_ || !vpnBridge_) {
      return;
    }
    vpnWanted_ = true;
    if (targetIsLobby) {
      if (roomManager_ && roomManager_->getCurrentLobby().IsValid()) {
        roomManager_->leaveLobby();
      }
      setJoinTargetFromLobby(trimmedTarget);
      if (roomManager_ && roomManager_->joinLobby(targetSteamID)) {
        if (!vpnBridge_->isRunning()) {
          if (!vpnBridge_->start()) {
            qWarning() << tr("无法启动 TUN 设备，请检查权限或驱动。");
            return;
          }
          updateVpnInfo();
        }
        vpnHosting_ = false;
        vpnConnected_ = true;
        vpnBridge_->rebroadcastState();
        updateStatus();
        refreshLobbies();
      } else {
        qWarning() << tr("无法加入房间。");
      }
    } else if (targetIsUser) {
      if (!vpnBridge_->isRunning()) {
        if (!vpnBridge_->start()) {
          qWarning() << tr("无法启动 TUN 设备，请检查权限或驱动。");
          return;
        }
        updateVpnInfo();
      }
      vpnManager_->addPeer(targetSteamID);
      hostSteamId_ = QString::number(targetSteamID.ConvertToUint64());
      emit hostSteamIdChanged();
      vpnHosting_ = false;
      vpnConnected_ = true;
      updateStatus();
      refreshLobbies();
    }
    return;
  }

  // TCP 模式
  if (targetIsLobby) {
    if (roomManager_ && roomManager_->getCurrentLobby().IsValid()) {
      roomManager_->leaveLobby();
    }
    setJoinTargetFromLobby(trimmedTarget);
    if (roomManager_ && roomManager_->joinLobby(targetSteamID)) {
      updateStatus();
      refreshLobbies();
    } else {
      qWarning() << tr("无法加入房间。");
    }
    return;
  }

  if (steamManager_->joinHost(hostID)) {
    ensureServerRunning();
    updateStatus();
    refreshLobbies();
  } else {
    qWarning() << tr("无法连接到房主。");
  }
}

void Backend::joinLobby(const QString &lobbyId) {
  if (!ensureSteamReady(tr("加入大厅"))) {
    return;
  }
  const QString trimmedId = lobbyId.trimmed();
  bool ok = false;
  const uint64 idValue = trimmedId.toULongLong(&ok);
  if (!ok) {
    qWarning() << tr("无效的房间 ID。");
    return;
  }

  CSteamID lobby(idValue);
  if (!lobby.IsValid() || !lobby.IsLobby()) {
    qWarning() << tr("请输入有效的房间 ID。");
    return;
  }

  if (isHost() || isConnected()) {
    disconnect();
  }

  applyLobbyModePreference(lobby);

  if (inTunMode()) {
    if (!ensureTunPrivileges()) {
      return;
    }
    ensureVpnSetup();
    if (!vpnManager_ || !vpnBridge_) {
      return;
    }
    vpnWanted_ = true;
    if (roomManager_ && roomManager_->getCurrentLobby().IsValid()) {
      roomManager_->leaveLobby();
    }
    setJoinTargetFromLobby(trimmedId);
    if (roomManager_ && roomManager_->joinLobby(lobby)) {
      if (!vpnBridge_->isRunning()) {
        if (!vpnBridge_->start()) {
          qWarning() << tr("无法启动 TUN 设备，请检查权限或驱动。");
          return;
        }
        updateVpnInfo();
      }
      vpnHosting_ = false;
      vpnConnected_ = true;
      vpnBridge_->rebroadcastState();
      updateStatus();
      refreshLobbies();
    } else {
      qWarning() << tr("无法加入房间。");
    }
    return;
  }

  if (isHost() || isConnected()) {
    disconnect();
  }
  if (roomManager_ && roomManager_->getCurrentLobby().IsValid()) {
    roomManager_->leaveLobby();
  }

  setJoinTargetFromLobby(trimmedId);
  if (roomManager_ && roomManager_->joinLobby(lobby)) {
    updateStatus();
    refreshLobbies();
  } else {
    qWarning() << tr("无法加入房间。");
  }
}

void Backend::disconnect() {
  const bool wasHost = isHost();
  const QString prevLobbyId = lobbyId();
  const int prevMemberCount = membersModel_.count();
  QString mySteamId;
  mySteamId = selfSteamId();

  if (roomManager_) {
    roomManager_->leaveLobby();
  }
  if (steamManager_) {
    steamManager_->disconnect();
  }
  if (server_) {
    server_->stop();
    server_.reset();
    lastTcpClients_ = 0;
    emit serverChanged();
  }
  updateMembersList();
  updateStatus();
  updateLobbyInfoSignals();
  setLobbyRefreshing(false);

  setJoinTarget(QString());
  lastAutoJoinTarget_.clear();
  chatModel_.clear();

  if (wasHost && !mySteamId.isEmpty()) {
    lobbiesModel_.removeByHostId(mySteamId);
  } else if (!prevLobbyId.isEmpty() && prevMemberCount > 0) {
    lobbiesModel_.adjustMemberCount(prevLobbyId, -1);
  }

  if (inTunMode()) {
    vpnWanted_ = false;
    stopVpn();
  }
}

void Backend::refreshFriends() {
  if (!steamReady_ || !steamManager_) {
    setFriendsRefreshing(false);
    return;
  }
  setFriendsRefreshing(true);
  QVariantList updated;
  std::vector<FriendsModel::Entry> modelData;
  int idx = 0;
  for (const auto &friendInfo : SteamUtils::getFriendsList()) {
    const QString steamId = QString::number(friendInfo.id.ConvertToUint64());
    const QString displayName = QString::fromStdString(friendInfo.name);
    const QString avatar = QString::fromStdString(friendInfo.avatarDataUrl);
    const PersonaDisplay persona = personaStateDisplay(friendInfo.personaState);
    const auto cooldownIt =
        inviteCooldowns_.find(friendInfo.id.ConvertToUint64());
    const int friendCooldown =
        cooldownIt != inviteCooldowns_.end() ? cooldownIt->second : 0;

    QVariantMap entry;
    entry.insert(QStringLiteral("id"), steamId);
    entry.insert(QStringLiteral("name"), displayName);
    entry.insert(QStringLiteral("status"), persona.label);
    entry.insert(QStringLiteral("online"), persona.online);
    entry.insert(QStringLiteral("cooldown"), friendCooldown);
    if (!avatar.isEmpty()) {
      entry.insert(QStringLiteral("avatar"), avatar);
    }
    updated.push_back(entry);
    modelData.push_back({steamId, displayName, avatar, persona.online,
                         persona.label, persona.priority, friendCooldown});
    ++idx;
  }
  friendsModel_.setFriends(std::move(modelData));
  if (updated != friends_) {
    friends_ = updated;
    emit friendsChanged();
  }
  friendsRefreshResetTimer_.start(1500);
}

void Backend::refreshLobbies() {
  if (!ensureSteamReady(tr("刷新大厅列表"))) {
    return;
  }
  if (roomManager_ && roomManager_->searchLobbies()) {
    setLobbyRefreshing(true);
  } else {
    qWarning() << tr("无法请求大厅列表。");
    setLobbyRefreshing(false);
  }
}

void Backend::setFriendFilter(const QString &text) {
  if (friendFilter_ == text) {
    return;
  }
  friendFilter_ = text;
  friendsModel_.setFilter(friendFilter_);
  emit friendFilterChanged();
}

void Backend::setChatReminderEnabled(bool enabled) {
  if (chatReminderEnabled_ == enabled) {
    return;
  }
  chatReminderEnabled_ = enabled;
  emit chatReminderEnabledChanged();
}

void Backend::refreshSelfSteamId() {
  QString nextId;
  if (steamReady_ && SteamUser()) {
    nextId = QString::number(SteamUser()->GetSteamID().ConvertToUint64());
  }
  if (nextId == selfSteamId_) {
    return;
  }
  selfSteamId_ = nextId;
  emit stateChanged();
  updateMembersList();
}

void Backend::requestUserAttention() {
  if (!mainWindow_) {
    return;
  }
  // Only nudge the user when the window is not already active.
  if (QGuiApplication::applicationState() == Qt::ApplicationActive &&
      mainWindow_->isActive()) {
    return;
  }
  mainWindow_->alert(0);
}

void Backend::setLobbyFilter(const QString &text) {
  if (lobbyFilter_ == text) {
    return;
  }
  lobbyFilter_ = text;
  lobbiesModel_.setFilter(lobbyFilter_);
  emit lobbyFilterChanged();
}

void Backend::setLobbySortMode(int mode) {
  if (lobbySortMode_ == mode) {
    return;
  }
  lobbySortMode_ = mode;
  lobbiesModel_.setSortMode(lobbySortMode_);
  emit lobbySortModeChanged();
}

void Backend::setConnectionMode(int mode) {
  ConnectionMode next = ConnectionMode::Tcp;
  if (mode == static_cast<int>(ConnectionMode::Tun)) {
    next = ConnectionMode::Tun;
  }
  if (connectionMode_ == next) {
    return;
  }
  if (isHost() || isConnected()) {
    qWarning() << tr("请先断开连接再切换模式。");
    return;
  }
  if (next == ConnectionMode::Tcp) {
    stopVpn();
    vpnWanted_ = false;
  }
  connectionMode_ = next;
  if (roomManager_) {
    roomManager_->setVpnMode(inTunMode(), vpnManager_.get());
    roomManager_->setAdvertisedMode(inTunMode());
  }
  if (next == ConnectionMode::Tun) {
    vpnStartAttempted_ = false;
  }
  updateStatus();
  emit stateChanged();
}

bool Backend::applyLobbyModePreference(const CSteamID &lobby) {
  if (!roomManager_ || !lobby.IsValid() || !lobby.IsLobby()) {
    return false;
  }
  const bool wantsTun = roomManager_->lobbyWantsTun(lobby);
  const ConnectionMode desired =
      wantsTun ? ConnectionMode::Tun : ConnectionMode::Tcp;
  if (connectionMode_ != desired) {
    setConnectionMode(static_cast<int>(desired));
  }
  return wantsTun;
}

void Backend::handleLobbyModeChanged(bool wantsTun, const CSteamID &lobby) {
  if (!roomManager_ || lobby != roomManager_->getCurrentLobby()) {
    return;
  }
  // Always track host-advertised mode so metadata stays correct if we later
  // refresh it (e.g. host re-publishes).
  roomManager_->setAdvertisedMode(wantsTun);
  if (wantsTun && !ensureTunPrivileges()) {
    return;
  }
  // Host advertises TCP, but UI was in TUN: fall back to TCP.
  if (!wantsTun && connectionMode_ == ConnectionMode::Tun) {
    if (isHost()) {
      return;
    }
    vpnWanted_ = false;
    stopVpn();
    connectionMode_ = ConnectionMode::Tcp;
    roomManager_->setVpnMode(false, nullptr);
    updateStatus();
    emit stateChanged();
    return;
  }
  if (!wantsTun) {
    return;
  }
  // Host advertises TUN, auto-switch guests into TUN mode.
  if (connectionMode_ == ConnectionMode::Tun) {
    return;
  }
  if (isHost()) {
    return;
  }
  vpnWanted_ = true;
  connectionMode_ = ConnectionMode::Tun;
  ensureVpnSetup();
  roomManager_->setVpnMode(true, vpnManager_.get());
  vpnStartAttempted_ = false;
  // Start TUN bridge immediately so the device appears for guests.
  if (vpnBridge_ && !vpnBridge_->isRunning()) {
    if (!vpnBridge_->start()) {
      qWarning() << tr("无法启动 TUN 设备，请检查权限或驱动。");
      return;
    }
    updateVpnInfo();
    vpnConnected_ = true;
    vpnHosting_ = false;
  }
  syncVpnPeers();
  updateStatus();
  emit stateChanged();
}

void Backend::ensureVpnRunning() {
  if (!inTunMode()) {
    return;
  }
  if (!vpnWanted_) {
    return;
  }
  ensureVpnSetup();
  if (!vpnBridge_ || vpnBridge_->isRunning()) {
    return;
  }
  if (vpnStartAttempted_) {
    return;
  }
  vpnStartAttempted_ = true;
  if (!vpnBridge_->start()) {
    qWarning() << tr("无法启动 TUN 设备，请检查权限或驱动。");
    vpnConnected_ = false;
    return;
  }
  updateVpnInfo();
  vpnConnected_ = true;
  vpnHosting_ = vpnHosting_ && vpnConnected_;
}

void Backend::setRoomName(const QString &name) {
  QString next = name;
  next = next.left(64).trimmed();
  if (roomName_ == next) {
    return;
  }
  roomName_ = next;
  emit roomNameChanged();
  emit stateChanged();
  if (roomManager_) {
    roomManager_->setLobbyName(roomName_.toStdString());
    roomManager_->refreshLobbyMetadata();
  }
}

void Backend::refreshMembers() { updateMembersList(); }

void Backend::inviteFriend(const QString &steamId) {
  if (!ensureSteamReady(tr("邀请好友"))) {
    return;
  }
  bool ok = false;
  uint64 friendId = steamId.toULongLong(&ok);
  if (!ok) {
    qWarning() << tr("无效的好友 ID。");
    return;
  }
  auto it = inviteCooldowns_.find(friendId);
  if (it != inviteCooldowns_.end() && it->second > 0) {
    qWarning() << tr("请 %1 秒后再发送邀请。").arg(it->second);
    return;
  }
  if (roomManager_ && roomManager_->getCurrentLobby().IsValid()) {
    SteamMatchmaking()->InviteUserToLobby(roomManager_->getCurrentLobby(),
                                          CSteamID(friendId));
    inviteCooldowns_[friendId] = 3;
    inviteCooldownSeconds_ =
        std::max(inviteCooldownSeconds_, inviteCooldowns_[friendId]);
    emit inviteCooldownChanged();
    updateFriendCooldown(steamId, inviteCooldowns_[friendId]);
  } else {
    qWarning() << tr("当前未在房间中，无法邀请。");
  }
}

void Backend::addFriend(const QString &steamId) {
  if (!ensureSteamReady(tr("添加好友"))) {
    return;
  }
  qDebug() << "[Friends] addFriend request" << steamId;

  bool ok = false;
  const uint64 targetId = steamId.toULongLong(&ok);
  if (!ok) {
    qDebug() << "[Friends] addFriend invalid id";
    qWarning() << tr("无效的好友 ID。");
    return;
  }

  CSteamID target(targetId);
  if (!target.IsValid() || !target.BIndividualAccount()) {
    qDebug() << "[Friends] addFriend invalid account";
    qWarning() << tr("无效的好友 ID。");
    return;
  }

  if (SteamUser() && target == SteamUser()->GetSteamID()) {
    qDebug() << "[Friends] addFriend self";
    qWarning() << tr("无法添加自己为好友。");
    return;
  }

  if (SteamFriends() &&
      SteamFriends()->HasFriend(target, k_EFriendFlagImmediate)) {
    qDebug() << "[Friends] addFriend already friend";
    qWarning() << tr("已经是好友了。");
    return;
  }

  if (!SteamFriends()) {
    qDebug() << "[Friends] addFriend missing SteamFriends";
    qWarning() << tr("无法发送好友请求，请检查 Steam 状态。");
    return;
  }

  const bool overlayEnabled = SteamUtils() && SteamUtils()->IsOverlayEnabled();
  bool overlayInvoked = false;
  if (overlayEnabled) {
    qDebug() << "[Friends] opening overlay friendadd"
             << target.ConvertToUint64();
    SteamFriends()->ActivateGameOverlayToUser("friendadd", target);
    overlayInvoked = true;
  } else {
    qDebug() << "[Friends] overlay disabled or unavailable";
  }

  bool openedProfile = false;
  if (!overlayInvoked) {
    const QUrl profileUrl(
        QStringLiteral("https://steamcommunity.com/profiles/%1/").arg(steamId));
    openedProfile = QDesktopServices::openUrl(profileUrl);
    qDebug() << "[Friends] fallback openUrl (profile)" << profileUrl
             << "opened:" << openedProfile;
  }

  if (overlayInvoked) {
    qWarning() << tr("已尝试打开 Steam 添加好友窗口。");
  } else {
    qWarning() << tr(
        "已在浏览器中打开对方 Steam 个人主页，请在网页中添加好友。");
    if (openedProfile) {
      setStatusOverride(tr("正在打开 Steam 个人主页…"), 2000);
    }
  }
}

void Backend::updateFriendCooldown(const QString &steamId, int seconds) {
  const bool modelChanged = friendsModel_.setInviteCooldown(steamId, seconds);
  bool listChanged = false;
  for (auto &entry : friends_) {
    QVariantMap map = entry.toMap();
    if (map.value(QStringLiteral("id")).toString() == steamId) {
      if (map.value(QStringLiteral("cooldown")).toInt() != seconds) {
        map.insert(QStringLiteral("cooldown"), seconds);
        entry = map;
        listChanged = true;
      }
      break;
    }
  }
  if (listChanged) {
    emit friendsChanged();
  }
}

void Backend::setFriendsRefreshing(bool refreshing) {
  if (friendsRefreshing_ == refreshing) {
    return;
  }
  friendsRefreshing_ = refreshing;
  emit friendsRefreshingChanged();
}

void Backend::setLobbyRefreshing(bool refreshing) {
  if (lobbyRefreshing_ == refreshing) {
    return;
  }
  lobbyRefreshing_ = refreshing;
  emit lobbyRefreshingChanged();
}

void Backend::updateRelayPing() {
  int next = -1;
  QVariantList popList;

  if (steamReady_ && SteamNetworkingUtils()) {
    const int popCount = SteamNetworkingUtils()->GetPOPCount();
    if (popCount > 0) {
      std::vector<SteamNetworkingPOPID> pops(popCount);
      const int filled =
          SteamNetworkingUtils()->GetPOPList(pops.data(), popCount);
      popList.reserve(std::max(0, filled));
      for (int i = 0; i < filled; ++i) {
        SteamNetworkingPOPID via = 0;
        const int ping = SteamNetworkingUtils()->GetPingToDataCenter(
            pops[static_cast<size_t>(i)], &via);
        const int roundTrip = ping >= 0 ? ping * 2 : -1;
        if (roundTrip >= 0 && (next < 0 || roundTrip < next)) {
          next = roundTrip;
        }

        QVariantMap entry;
        entry.insert(QStringLiteral("name"),
                     renderPopId(pops[static_cast<size_t>(i)]));
        entry.insert(QStringLiteral("ping"), roundTrip);
        if (via != 0) {
          entry.insert(QStringLiteral("via"), renderPopId(via));
        }
        popList.push_back(entry);
      }
      std::sort(popList.begin(), popList.end(),
                [](const QVariant &a, const QVariant &b) {
                  const QVariantMap am = a.toMap();
                  const QVariantMap bm = b.toMap();
                  int ap = am.value(QStringLiteral("ping"),
                                    QVariant(std::numeric_limits<int>::max()))
                               .toInt();
                  int bp = bm.value(QStringLiteral("ping"),
                                    QVariant(std::numeric_limits<int>::max()))
                               .toInt();
                  if (ap < 0) {
                    ap = std::numeric_limits<int>::max();
                  }
                  if (bp < 0) {
                    bp = std::numeric_limits<int>::max();
                  }
                  return ap < bp;
                });
    }
  }

  if (next != relayPingMs_) {
    relayPingMs_ = next;
    emit relayPingChanged();
  }

  if (relayPops_ != popList) {
    relayPops_ = popList;
    emit relayPopsChanged();
  }
}

void Backend::copyToClipboard(const QString &text) {
  if (text.isEmpty()) {
    return;
  }
  QClipboard *cb = QGuiApplication::clipboard();
  if (cb) {
    cb->setText(text);
  }
}

void Backend::sendChatMessage(const QString &text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  if (!ensureSteamReady(tr("发送消息"))) {
    return;
  }
  if (!roomManager_ || !roomManager_->getCurrentLobby().IsValid()) {
    qWarning() << tr("请先加入房间后再发送消息。");
    return;
  }
  const std::string payload = trimmed.toUtf8().toStdString();
  if (!roomManager_->sendChatMessage(payload)) {
    qWarning() << tr("消息发送失败。");
  }
}

void Backend::pinChatMessage(const QString &steamId, const QString &displayName,
                             const QString &avatar, const QString &message,
                             const QDateTime &timestamp) {
  if (!ensureSteamReady(tr("置顶消息"))) {
    return;
  }
  if (!roomManager_ || roomManager_->getCurrentLobby() == k_steamIDNil) {
    qWarning() << tr("请先加入房间后再置顶消息。");
    return;
  }
  if (!isHost()) {
    setStatusOverride(tr("只有房主可以置顶消息。"), 2400);
    return;
  }

  ChatModel::Entry entry;
  entry.steamId = steamId;
  entry.displayName = displayName.isEmpty() ? steamId : displayName;
  entry.message = message.trimmed();
  entry.avatar = avatar;
  entry.timestamp =
      timestamp.isValid() ? timestamp : QDateTime::currentDateTime();
  if (entry.message.isEmpty()) {
    return;
  }

  bool isSelfAuthor = false;
  if (steamReady_ && SteamUser()) {
    const QString myId =
        QString::number(SteamUser()->GetSteamID().ConvertToUint64());
    isSelfAuthor = (steamId == myId);
  }
  entry = populatePinnedEntryAvatar(std::move(entry), isSelfAuthor);
  chatModel_.setPinnedMessage(entry);

  if (roomManager_) {
    const QString payload = serializePinnedMessage(entry);
    roomManager_->setPinnedMessageData(payload.toStdString());
  }
}

void Backend::clearPinnedChatMessage() {
  if (!roomManager_ || roomManager_->getCurrentLobby() == k_steamIDNil) {
    chatModel_.clearPinnedMessage();
    return;
  }
  if (!isHost()) {
    setStatusOverride(tr("只有房主可以取消置顶。"), 2400);
    return;
  }
  chatModel_.clearPinnedMessage();
  roomManager_->clearPinnedMessageData();
}

void Backend::launchSteam(bool useSteamChina) {
#ifdef Q_OS_WIN
  QString steamPath;
  {
    QSettings steamReg("HKEY_CURRENT_USER\\Software\\Valve\\Steam",
                       QSettings::NativeFormat);
    const QString regPath =
        steamReg.value(QStringLiteral("SteamPath")).toString();
    if (!regPath.isEmpty()) {
      const QString candidate =
          QDir(regPath).filePath(QStringLiteral("steam.exe"));
      if (QFileInfo::exists(candidate)) {
        steamPath = candidate;
      }
    }
  }

  const auto tryProgramDir = [&](const QString &base) {
    if (!steamPath.isEmpty() || base.isEmpty()) {
      return;
    }
    const QString candidate =
        QDir(base).filePath(QStringLiteral("Steam/steam.exe"));
    if (QFileInfo::exists(candidate)) {
      steamPath = candidate;
    }
  };

  tryProgramDir(qEnvironmentVariable("ProgramFiles(x86)"));
  tryProgramDir(qEnvironmentVariable("ProgramFiles"));

  if (steamPath.isEmpty()) {
    setStatusOverride(tr("未找到 Steam 安装路径。"), 3000);
    return;
  }

  QStringList args;
  if (useSteamChina) {
    args << QStringLiteral("-steamchina");
  }
  const bool started = QProcess::startDetached(steamPath, args);
  if (!started) {
    setStatusOverride(tr("无法启动 Steam。"), 3000);
    return;
  }

  setStatusOverride(useSteamChina ? tr("尝试以蒸汽平台启动 Steam…")
                                  : tr("尝试以国际版启动 Steam…"),
                    3000);
#else
  Q_UNUSED(useSteamChina);
  qWarning() << "Steam launch switch is only supported on Windows.";
#endif
}

void Backend::handleChatMessage(uint64_t senderId, const QString &message) {
  const QString trimmed = message.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  ChatModel::Entry entry;
  entry.steamId = QString::number(senderId);
  entry.message = trimmed;
  entry.timestamp = QDateTime::currentDateTime();

  const CSteamID senderSteam(static_cast<uint64>(senderId));
  if (senderSteam.IsValid() && SteamFriends()) {
    const char *name = SteamFriends()->GetFriendPersonaName(senderSteam);
    if (name && name[0] != '\0') {
      entry.displayName = QString::fromUtf8(name);
    }
  }
  if (entry.displayName.isEmpty()) {
    entry.displayName = entry.steamId;
  }
  entry.avatar = avatarForSteamId(senderSteam);

  if (steamReady_ && SteamUser()) {
    const CSteamID myId = SteamUser()->GetSteamID();
    entry.isSelf = myId.IsValid() && senderSteam == myId;
  }

  if (!entry.isSelf && chatReminderEnabled_) {
    if (!soundNotifier_.isInitialized() && mainWindow_) {
      soundNotifier_.initialize(mainWindow_);
    }
    soundNotifier_.playMessageAlert();
    requestUserAttention();
  }

  chatModel_.appendMessage(std::move(entry));
}

void Backend::handlePinnedMessageMetadata(const QString &payload) {
  if (payload.isEmpty()) {
    chatModel_.clearPinnedMessage();
    return;
  }
  const auto parsed = parsePinnedMessagePayload(payload);
  if (!parsed.has_value()) {
    chatModel_.clearPinnedMessage();
    return;
  }
  bool isSelfAuthor = false;
  if (steamReady_ && SteamUser()) {
    const QString myId =
        QString::number(SteamUser()->GetSteamID().ConvertToUint64());
    isSelfAuthor = (parsed->steamId == myId);
  }
  ChatModel::Entry entry =
      populatePinnedEntryAvatar(std::move(*parsed), isSelfAuthor);
  chatModel_.setPinnedMessage(entry);
}

std::optional<ChatModel::Entry>
Backend::parsePinnedMessagePayload(const QString &payload) const {
  QJsonParseError error{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    return std::nullopt;
  }

  const QJsonObject obj = doc.object();
  ChatModel::Entry entry;
  entry.steamId = obj.value(QStringLiteral("steamId")).toString();
  entry.displayName = obj.value(QStringLiteral("displayName")).toString();
  entry.message = obj.value(QStringLiteral("message")).toString().trimmed();
  const QString ts = obj.value(QStringLiteral("timestamp")).toString();
  if (!ts.isEmpty()) {
    entry.timestamp = QDateTime::fromString(ts, Qt::ISODateWithMs);
    if (!entry.timestamp.isValid()) {
      entry.timestamp = QDateTime::fromString(ts, Qt::ISODate);
    }
    if (entry.timestamp.isValid()) {
      entry.timestamp = entry.timestamp.toLocalTime();
    }
  }
  if (!entry.timestamp.isValid()) {
    entry.timestamp = QDateTime::currentDateTime();
  }
  if (entry.displayName.isEmpty() && !entry.steamId.isEmpty()) {
    entry.displayName = entry.steamId;
  }
  if (entry.message.isEmpty()) {
    return std::nullopt;
  }
  return entry;
}

QString Backend::serializePinnedMessage(const ChatModel::Entry &entry) const {
  QJsonObject obj;
  obj.insert(QStringLiteral("steamId"), entry.steamId);
  obj.insert(QStringLiteral("displayName"), entry.displayName);
  obj.insert(QStringLiteral("message"), entry.message);
  const QDateTime ts = entry.timestamp.isValid()
                           ? entry.timestamp.toUTC()
                           : QDateTime::currentDateTimeUtc();
  obj.insert(QStringLiteral("timestamp"), ts.toString(Qt::ISODateWithMs));
  const QJsonDocument doc(obj);
  return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

ChatModel::Entry Backend::populatePinnedEntryAvatar(ChatModel::Entry entry,
                                                    bool isSelfAuthor) {
  entry.isSelf = isSelfAuthor;
  bool ok = false;
  const uint64_t id = entry.steamId.toULongLong(&ok);
  if (ok) {
    const CSteamID steamId(static_cast<uint64>(id));
    if (steamId.IsValid()) {
      if (entry.displayName.isEmpty() && SteamFriends()) {
        const char *name = SteamFriends()->GetFriendPersonaName(steamId);
        if (name && name[0] != '\0') {
          entry.displayName = QString::fromUtf8(name);
        }
      }
      if (entry.avatar.isEmpty()) {
        entry.avatar = avatarForSteamId(steamId);
      }
    }
  }
  return entry;
}

void Backend::tick() {
  if (!steamReady_) {
    if (relayPingMs_ != -1) {
      relayPingMs_ = -1;
      emit relayPingChanged();
    }
    if (!relayPops_.isEmpty()) {
      relayPops_.clear();
      emit relayPopsChanged();
    }
    return;
  }

  refreshSelfSteamId();

  SteamAPI_RunCallbacks();
  if (steamManager_) {
    steamManager_->update();
  }

  const auto now = std::chrono::steady_clock::now();
  if (lastRelayPingSample_.time_since_epoch().count() == 0 ||
      now - lastRelayPingSample_ > std::chrono::seconds(2)) {
    updateRelayPing();
    lastRelayPingSample_ = now;
  }

  if (inTunMode()) {
    ensureVpnRunning();
    syncVpnPeers();
    updateVpnInfo();
  }

  refreshHostId();
  updateMembersList();
  updateStatus();
  updateLobbyInfoSignals();
}

void Backend::updateStatus() {
  if (inTunMode()) {
    updateVpnInfo();
  }
  if (!statusOverride_.isEmpty()) {
    return;
  }
  QString next;
  if (!steamReady_) {
    next = tr("Steam 未就绪。");
  } else if (inTunMode()) {
    const bool active = vpnBridge_ && vpnBridge_->isRunning();
    if (active != vpnConnected_) {
      vpnConnected_ = active;
    }
    QString ipText = tunLocalIp_;
    if (ipText.isEmpty() && active) {
      ipText = tr("IP 协商中…");
    }
    if (vpnHosting_) {
      next = tr("TUN 模式主持中");
    } else if (active) {
      next = tr("TUN 模式已连接");
    } else {
      next = tr("TUN 模式空闲，等待创建或加入房间。");
    }
    if (!ipText.isEmpty()) {
      next += tr(" · 本地 %1").arg(ipText);
    }
  } else if (isHost()) {
    const QString id = lobbyId();
    next = id.isEmpty() ? tr("主持房间中…") : tr("作为房主正在主持房间");
  } else if (isConnected()) {
    const QString id = lobbyId();
    next = id.isEmpty() ? tr("已连接到房间") : tr("已连接到房主 %1").arg(id);
  } else {
    next = tr("空闲，等待创建或加入房间。");
  }

  const int clientCount = tcpClients();
  // if (server_) {
  //   next += tr(" · 本地 TCP 客户端 %1").arg(clientCount);
  // }

  if (clientCount != lastTcpClients_) {
    lastTcpClients_ = clientCount;
    emit serverChanged();
  }

  if (next != status_) {
    status_ = next;
    emit stateChanged();
  }
}

void Backend::setStatusOverride(const QString &text, int durationMs) {
  statusOverride_ = text;
  status_ = text;
  emit stateChanged();
  if (durationMs > 0) {
    statusOverrideTimer_.start(durationMs);
  } else {
    statusOverrideTimer_.stop();
  }
}

void Backend::clearStatusOverride() {
  if (statusOverride_.isEmpty()) {
    return;
  }
  statusOverride_.clear();
  updateStatus();
}

void Backend::updateLobbiesList(
    const std::vector<SteamRoomManager::LobbyInfo> &lobbies) {
  std::vector<LobbiesModel::Entry> entries;
  entries.reserve(lobbies.size());

  const QString currentLobbyId = lobbyId();
  const bool iAmHost = isHost();
  const QString myIdString =
      (steamReady_ && SteamUser())
          ? QString::number(SteamUser()->GetSteamID().ConvertToUint64())
          : QString();
  const int currentMemberCount =
      roomManager_ ? static_cast<int>(roomManager_->getLobbyMembers().size())
                   : 0;

  for (const auto &lobby : lobbies) {
    LobbiesModel::Entry entry;
    entry.lobbyId = QString::number(lobby.id.ConvertToUint64());
    entry.name = QString::fromStdString(lobby.name);
    if (entry.name.trimmed().isEmpty()) {
      QString hostDisplay = QString::fromStdString(lobby.ownerName);
      if (hostDisplay.isEmpty() && lobby.ownerId.IsValid() && SteamFriends()) {
        const char *ownerName =
            SteamFriends()->GetFriendPersonaName(lobby.ownerId);
        if (ownerName && ownerName[0] != '\0') {
          hostDisplay = QString::fromUtf8(ownerName);
        }
      }
      if (!hostDisplay.isEmpty()) {
        entry.name = tr("%1 的房间").arg(hostDisplay);
      } else {
        entry.name = tr("未命名房间");
      }
    }
    if (lobby.ownerId.IsValid()) {
      entry.hostId = QString::number(lobby.ownerId.ConvertToUint64());
    }
    entry.hostName = QString::fromStdString(lobby.ownerName);
    if (entry.hostName.isEmpty() && lobby.ownerId.IsValid() && SteamFriends()) {
      const char *ownerName =
          SteamFriends()->GetFriendPersonaName(lobby.ownerId);
      if (ownerName) {
        entry.hostName = QString::fromUtf8(ownerName);
      }
    }
    entry.memberCount = lobby.memberCount;
    entry.ping = lobby.pingMs >= 0 ? lobby.pingMs : -1;

    if (!currentLobbyId.isEmpty() && entry.lobbyId == currentLobbyId &&
        currentMemberCount > 0) {
      entry.memberCount = std::max(entry.memberCount, currentMemberCount);
    }

    if (!iAmHost && !myIdString.isEmpty() && entry.hostId == myIdString) {
      continue; // when acting as client, hide previously hosted lobby
    }

    entries.push_back(std::move(entry));
  }

  lobbiesModel_.setLobbies(std::move(entries));
}

QString Backend::avatarForSteamId(const CSteamID &memberId) {
  if (!memberId.IsValid()) {
    return {};
  }
  const uint64_t key = memberId.ConvertToUint64();
  const auto cached = memberAvatars_.find(key);
  if (cached != memberAvatars_.end()) {
    return cached->second;
  }

  const std::string avatarData = SteamUtils::getAvatarDataUrl(memberId);
  if (avatarData.empty()) {
    return {};
  }
  QString avatar = QString::fromStdString(avatarData);
  memberAvatars_.emplace(key, avatar);
  return avatar;
}

void Backend::updateMembersList() {
  if (!steamReady_) {
    membersModel_.setMembers({});
    memberAvatars_.clear();
    lobbiesModel_.setMemberCount({}, 0);
    return;
  }

  if (inTunMode()) {
    std::vector<CSteamID> lobbyMembers;
    if (roomManager_) {
      CSteamID currentLobby = roomManager_->getCurrentLobby();
      if (currentLobby.IsValid()) {
        lobbyMembers = roomManager_->getLobbyMembers();
      }
    }

    std::unordered_map<uint64_t, uint32_t> ipBySteam;
    if (vpnBridge_) {
      const auto routes = vpnBridge_->getRoutingTable();
      for (const auto &kv : routes) {
        const uint64_t sid = kv.second.steamID.ConvertToUint64();
        ipBySteam[sid] = kv.second.ipAddress;
      }
    }

    std::vector<MembersModel::Entry> entries;
    entries.reserve(lobbyMembers.size());
    CSteamID myId = SteamUser()->GetSteamID();
    std::unordered_set<uint64_t> seen;
    seen.reserve(lobbyMembers.size());

    for (const auto &memberId : lobbyMembers) {
      const uint64 memberValue = memberId.ConvertToUint64();
      seen.insert(memberValue);

      MembersModel::Entry entry;
      entry.isFriend =
          (SteamFriends() &&
           SteamFriends()->HasFriend(memberId, k_EFriendFlagImmediate)) ||
          (SteamUser() && memberId == myId);
      entry.steamId = QString::number(memberValue);
      entry.displayName =
          QString::fromUtf8(SteamFriends()->GetFriendPersonaName(memberId));
      entry.avatar = avatarForSteamId(memberId);
      entry.ping = -1;
      entry.relay = QStringLiteral("-");

      if (memberId == myId) {
        entry.ping = 0;
        entry.relay = vpnHosting_ ? tr("房主") : tr("本机");
      } else if (vpnManager_) {
        entry.ping = vpnManager_->getPeerPing(memberId);
        entry.relay = QString::fromStdString(
            vpnManager_->getPeerConnectionType(memberId));
      }
      auto itIp = ipBySteam.find(memberValue);
      if (itIp != ipBySteam.end()) {
        entry.ip =
            QString::fromStdString(SteamVpnBridge::ipToString(itIp->second));
      }
      entries.push_back(std::move(entry));
    }

    const int newCount = static_cast<int>(entries.size());
    if (newCount != lastMemberLogCount_) {
      lastMemberLogCount_ = newCount;
      qDebug() << "[Members] updated count:" << newCount;
      for (const auto &entry : entries) {
        qDebug() << "   member" << entry.displayName << "(" << entry.steamId
                 << ")";
      }
    }
    membersModel_.setMembers(std::move(entries));
    const QString currentLobbyId = lobbyId();
    if (!currentLobbyId.isEmpty() && newCount > 0) {
      lobbiesModel_.setMemberCount(currentLobbyId, newCount);
    }
    return;
  }

  if (!steamManager_) {
    membersModel_.setMembers({});
    return;
  }

  std::vector<CSteamID> lobbyMembers;
  if (roomManager_) {
    CSteamID currentLobby = roomManager_->getCurrentLobby();
    if (currentLobby.IsValid()) {
      lobbyMembers = roomManager_->getLobbyMembers();
    }
  }

  std::vector<MembersModel::Entry> entries;
  entries.reserve(lobbyMembers.size());
  std::vector<std::tuple<uint64_t, int, std::string>> pingBroadcast;

  CSteamID myId = SteamUser()->GetSteamID();
  CSteamID hostId = steamManager_->getHostSteamID();

  std::unordered_set<uint64_t> seen;
  seen.reserve(lobbyMembers.size());

  const auto isMemberHost = [&](const CSteamID &member) -> bool {
    return hostId.IsValid() && member == hostId;
  };

  for (const auto &memberId : lobbyMembers) {
    const uint64 memberValue = memberId.ConvertToUint64();
    seen.insert(memberValue);

    MembersModel::Entry entry;
    entry.isFriend =
        (SteamFriends() &&
         SteamFriends()->HasFriend(memberId, k_EFriendFlagImmediate)) ||
        (SteamUser() && memberId == myId);
    entry.isSelf = (SteamUser() && memberId == myId);
    entry.steamId = QString::number(memberValue);
    entry.displayName =
        QString::fromUtf8(SteamFriends()->GetFriendPersonaName(memberId));
    entry.avatar = avatarForSteamId(memberId);
    entry.relay = QStringLiteral("-");
    entry.ping = -1;
    const bool memberIsHost = isMemberHost(memberId);

    if (memberId == myId) {
      entry.ping = 0;
      entry.relay = isHost() ? tr("房主") : tr("本机");
    } else {
      if (memberIsHost) {
        entry.relay = tr("房主");
        int rp = -1;
        std::string relayInfo;
        const bool hasBroadcast =
            roomManager_ && roomManager_->getRemotePing(myId, rp, relayInfo) &&
            rp >= 0;
        if (hasBroadcast && rp > 1) {
          entry.ping = rp;
          if (!relayInfo.empty()) {
            entry.relay = QString::fromStdString(relayInfo);
          }
        } else {
          const int fallbackPing =
              steamManager_ ? steamManager_->getHostPing() : -1;
          entry.ping = fallbackPing > 1 ? fallbackPing : -1;
        }
        if (entry.ping >= 0 && entry.ping < 2) {
          entry.ping = -1;
        }
      } else if (!isHost()) {
        int rp = -1;
        std::string relayInfo;
        if (roomManager_ &&
            roomManager_->getRemotePing(memberId, rp, relayInfo) && rp >= 0) {
          entry.ping = rp;
          entry.relay = QString::fromStdString(relayInfo);
        }
      } else if (isHost()) {
        std::lock_guard<std::mutex> lock(steamManager_->getConnectionsMutex());
        for (const auto &conn : steamManager_->getConnections()) {
          SteamNetConnectionRealTimeStatus_t status{};
          if (steamManager_->getInterface()->GetConnectionRealTimeStatus(
                  conn, &status, 0, nullptr) != k_EResultOK ||
              status.m_eState != k_ESteamNetworkingConnectionState_Connected) {
            continue; // Transport not ready; skip to avoid ICE asserts.
          }

          SteamNetConnectionInfo_t info;
          if (steamManager_->getInterface()->GetConnectionInfo(conn, &info) &&
              info.m_identityRemote.GetSteamID() == memberId) {
            entry.ping = steamManager_->getConnectionPing(conn);
            entry.relay = QString::fromStdString(
                steamManager_->getConnectionRelayInfo(conn));
            if (entry.ping >= 0) {
              pingBroadcast.emplace_back(memberValue, entry.ping,
                                         entry.relay.toStdString());
            }
            break;
          }
        }
      }
    }

    entries.push_back(std::move(entry));
  }

  if (isHost()) {
    std::lock_guard<std::mutex> lock(steamManager_->getConnectionsMutex());
    for (const auto &conn : steamManager_->getConnections()) {
      SteamNetConnectionRealTimeStatus_t status{};
      if (steamManager_->getInterface()->GetConnectionRealTimeStatus(
              conn, &status, 0, nullptr) != k_EResultOK ||
          status.m_eState != k_ESteamNetworkingConnectionState_Connected) {
        continue; // Still negotiating; skip to avoid noisy asserts.
      }

      SteamNetConnectionInfo_t info;
      if (!steamManager_->getInterface()->GetConnectionInfo(conn, &info)) {
        continue;
      }
      CSteamID remoteId = info.m_identityRemote.GetSteamID();
      if (!remoteId.IsValid()) {
        continue;
      }
      const uint64_t remoteValue = remoteId.ConvertToUint64();
      if (!seen.insert(remoteValue).second) {
        continue;
      }

      MembersModel::Entry entry;
      entry.isFriend =
          (SteamFriends() &&
           SteamFriends()->HasFriend(remoteId, k_EFriendFlagImmediate)) ||
          (SteamUser() && remoteId == myId);
      entry.isSelf = (SteamUser() && remoteId == myId);
      entry.steamId = QString::number(remoteValue);
      entry.displayName =
          QString::fromUtf8(SteamFriends()->GetFriendPersonaName(remoteId));
      entry.avatar = avatarForSteamId(remoteId);
      entry.ping = steamManager_->getConnectionPing(conn);
      const std::string relayInfo = steamManager_->getConnectionRelayInfo(conn);
      entry.relay =
          relayInfo.empty() ? tr("P2P") : QString::fromStdString(relayInfo);
      if (entry.ping >= 0) {
        pingBroadcast.emplace_back(remoteValue, entry.ping, relayInfo);
      }

      entries.push_back(std::move(entry));
    }
  }

  if (isHost() && roomManager_) {
    const auto now = std::chrono::steady_clock::now();
    if (lastPingBroadcast_.time_since_epoch().count() == 0 ||
        now - lastPingBroadcast_ > std::chrono::seconds(2)) {
      roomManager_->broadcastPings(pingBroadcast);
      lastPingBroadcast_ = now;
    }
  }

  const int newCount = static_cast<int>(entries.size());
  if (newCount != lastMemberLogCount_) {
    lastMemberLogCount_ = newCount;
    qDebug() << "[Members] updated count:" << newCount;
    for (const auto &entry : entries) {
      qDebug() << "   member" << entry.displayName << "(" << entry.steamId
               << ")";
    }
  }

  membersModel_.setMembers(std::move(entries));
  const QString currentLobbyId = lobbyId();
  if (!currentLobbyId.isEmpty() && newCount > 0) {
    lobbiesModel_.setMemberCount(currentLobbyId, newCount);
  }
}

void Backend::refreshHostId() {
  QString next;
  if (inTunMode()) {
    if (roomManager_) {
      CSteamID lobby = roomManager_->getCurrentLobby();
      if (lobby.IsValid()) {
        CSteamID owner = SteamMatchmaking()->GetLobbyOwner(lobby);
        if (owner.IsValid()) {
          next = QString::number(owner.ConvertToUint64());
        }
      }
    }
    if (next.isEmpty() && !hostSteamId_.isEmpty()) {
      next = hostSteamId_;
    }
  } else if (steamManager_) {
    CSteamID host = steamManager_->getHostSteamID();
    if (host.IsValid()) {
      next = QString::number(host.ConvertToUint64());
    }
  }
  if (next != hostSteamId_) {
    hostSteamId_ = next;
    emit hostSteamIdChanged();
  }
}

void Backend::updateLobbyInfoSignals() {
  const QString id = lobbyId();
  const QString name = lobbyName();
  const bool lobbyChanged = id != lastLobbyId_;
  if (lobbyChanged) {
    chatModel_.clear();
  }
  if (lobbyChanged || name != lastLobbyName_) {
    lastLobbyId_ = id;
    lastLobbyName_ = name;
    emit stateChanged();
  }
}

void Backend::checkForUpdates(bool useProxy) {
  if (checkingUpdate_) {
    return;
  }
  resetUpdateCheck();
  updateStatusText_ = tr("正在检查更新…");
  checkingUpdate_ = true;
  emit updateInfoChanged();

  const QString apiUrl = QStringLiteral(
      "https://api.github.com/repos/moeleak/connecttool-qt/releases/latest");
  const QString reqUrl =
      useProxy ? QStringLiteral("https://gh-proxy.org/") + apiUrl : apiUrl;

  const QUrl url(reqUrl);
  QNetworkRequest req{url};
  req.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("connecttool-qt"));
  currentUpdateReply_ = networkManager_.get(req);

  connect(currentUpdateReply_, &QNetworkReply::finished, this,
          &Backend::handleUpdateReply);
}

void Backend::downloadUpdate(bool useProxy, const QString &targetPath) {
  if (downloadingUpdate_) {
    return;
  }
  if (latestDownloadUrl_.isEmpty()) {
    updateStatusText_ = tr("没有可用的下载链接，请先检查更新。");
    emit updateInfoChanged();
    return;
  }
  QString pathInput = targetPath.trimmed();

  // Accept both raw file paths and file:// URLs (e.g. file:///C:/path/on/windows)
  const QUrl parsed = QUrl::fromUserInput(pathInput);
  if (parsed.isLocalFile()) {
    const QString local = parsed.toLocalFile();
    if (!local.isEmpty()) {
      pathInput = local;
    }
  }

#ifdef Q_OS_WIN
  // File URLs sliced in QML may come through as "/C:/path"; drop the leading
  // slash so QDir treats it as a proper drive path.
  if (pathInput.startsWith('/') && pathInput.size() > 2 &&
      pathInput[1].isLetter() && pathInput[2] == QLatin1Char(':')) {
    pathInput.remove(0, 1);
  }
#endif

  pathInput = QDir::fromNativeSeparators(pathInput);
  if (pathInput.isEmpty()) {
    updateStatusText_ = tr("请选择下载目录。");
    emit updateInfoChanged();
    return;
  }
  QString chosenDir;
  QString chosenFileName;
  const QFileInfo fi(pathInput);
  const bool endsWithSlash =
      pathInput.endsWith('/') || pathInput.endsWith('\\');
  if (!endsWithSlash && (!fi.exists() || fi.isFile())) {
    chosenDir = fi.dir().absolutePath();
    chosenFileName = fi.fileName();
  }
  if (chosenDir.isEmpty()) {
    chosenDir = fi.absoluteFilePath();
  }
  if (chosenFileName.isEmpty()) {
    chosenFileName = fi.fileName();
  }

  QDir dir(chosenDir);
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    updateStatusText_ = tr("无法创建下载目录：%1").arg(chosenDir);
    emit updateInfoChanged();
    return;
  }

  const QString urlStr = preferredDownloadUrl(useProxy);
  if (urlStr.isEmpty()) {
    updateStatusText_ = tr("下载链接无效。");
    emit updateInfoChanged();
    return;
  }

  resetDownloadState();
  downloadTargetDir_ = dir.absolutePath();
  downloadTargetRequested_ = pathInput;
  downloadingUpdate_ = true;
  downloadProgress_ = 0.0;
  downloadSavedPath_.clear();
  updateStatusText_ = tr("正在下载更新…");
  emit updateInfoChanged();
  emit updateDownloadChanged();

  QNetworkRequest req{QUrl(urlStr)};
  req.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("connecttool-qt"));
  currentDownloadReply_ = networkManager_.get(req);

  connect(currentDownloadReply_, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64 total) {
            if (total > 0) {
              downloadProgress_ =
                  static_cast<double>(received) / static_cast<double>(total);
            } else {
              downloadProgress_ = 0.0;
            }
            emit updateDownloadChanged();
          });
  connect(currentDownloadReply_, &QNetworkReply::finished, this,
          &Backend::handleDownloadFinished);
}

void Backend::handleUpdateReply() {
  checkingUpdate_ = false;
  QPointer<QNetworkReply> reply = currentUpdateReply_;
  currentUpdateReply_.clear();

  if (!reply) {
    updateStatusText_ = tr("检查失败。");
    updateAvailable_ = false;
    latestDownloadUrl_.clear();
    latestReleasePage_.clear();
    emit updateInfoChanged();
    return;
  }

  const QByteArray payload = reply->readAll();
  const QNetworkReply::NetworkError error = reply->error();
  reply->deleteLater();
  if (error != QNetworkReply::NoError) {
    updateStatusText_ = tr("检查失败：%1").arg(reply->errorString());
    updateAvailable_ = false;
    latestDownloadUrl_.clear();
    latestReleasePage_.clear();
    emit updateInfoChanged();
    return;
  }

  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    updateStatusText_ = tr("更新信息解析失败。");
    updateAvailable_ = false;
    latestDownloadUrl_.clear();
    latestReleasePage_.clear();
    emit updateInfoChanged();
    return;
  }

  const QJsonObject obj = doc.object();
  const QString tag = obj.value(QStringLiteral("tag_name")).toString();
  latestReleasePage_ =
      stripGhProxyPrefix(obj.value(QStringLiteral("html_url")).toString());
  const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
  QString assetUrl;
  for (const auto &assetVal : assets) {
    if (!assetVal.isObject()) {
      continue;
    }
    const QJsonObject assetObj = assetVal.toObject();
    const QString browserUrl = stripGhProxyPrefix(
        assetObj.value(QStringLiteral("browser_download_url")).toString());
    if (!browserUrl.isEmpty()) {
      assetUrl = browserUrl;
      break;
    }
  }

  latestVersion_ =
      normalizeVersion(tag.isEmpty() ? QStringLiteral("0.0.0") : tag);
  latestDownloadUrl_ = assetUrl;
  updateAvailable_ =
      isVersionNewer(latestVersion_, normalizeVersion(appVersion_));

  if (updateAvailable_) {
    if (!latestDownloadUrl_.isEmpty()) {
      updateStatusText_ = tr("发现新版本 %1，可下载更新。").arg(latestVersion_);
    } else {
      updateStatusText_ =
          tr("发现新版本 %1，暂未找到下载链接。").arg(latestVersion_);
    }
  } else {
    updateStatusText_ = tr("当前已是最新版本（%1）。").arg(appVersion_);
  }
  emit updateInfoChanged();
}

void Backend::handleDownloadFinished() {
  QPointer<QNetworkReply> reply = currentDownloadReply_;
  currentDownloadReply_.clear();
  downloadingUpdate_ = false;

  if (!reply) {
    updateStatusText_ = tr("下载已取消。");
    emit updateInfoChanged();
    emit updateDownloadChanged();
    return;
  }

  const QNetworkReply::NetworkError err = reply->error();
  const QByteArray data = reply->readAll();
  const QUrl finalUrl = reply->url();
  reply->deleteLater();

  if (err != QNetworkReply::NoError) {
    updateStatusText_ = tr("下载失败：%1").arg(reply->errorString());
    emit updateInfoChanged();
    emit updateDownloadChanged();
    return;
  }

  QFileInfo requested(downloadTargetRequested_.isEmpty()
                          ? downloadTargetDir_
                          : downloadTargetRequested_);
  QString fileName = QFileInfo(finalUrl.path()).fileName();
  if (fileName.isEmpty()) {
    fileName =
        latestVersion_.isEmpty()
            ? QStringLiteral("connecttool-qt-release.bin")
            : QStringLiteral("connecttool-qt-%1.zip").arg(latestVersion_);
  }

  QString targetDir = downloadTargetDir_;
  if (targetDir.isEmpty()) {
    targetDir =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  }

  // If user specified a filename in the requested path, prefer it.
  if (requested.exists() && requested.isFile()) {
    targetDir = requested.dir().absolutePath();
    fileName = requested.fileName();
  } else if (!requested.exists() && !requested.fileName().isEmpty() &&
             !downloadTargetDir_.endsWith('/') &&
             !downloadTargetDir_.endsWith('\\')) {
    targetDir = requested.dir().absolutePath();
    fileName = requested.fileName();
  }
  if (!fileName.contains('.')) {
    fileName.append(QStringLiteral(".zip"));
  }

  const QString targetPath = QDir(targetDir).filePath(fileName);
  QSaveFile file(targetPath);
  if (!file.open(QIODevice::WriteOnly)) {
    updateStatusText_ = tr("保存文件失败：%1").arg(file.errorString());
    emit updateInfoChanged();
    emit updateDownloadChanged();
    return;
  }
  if (file.write(data) != data.size() || !file.commit()) {
    updateStatusText_ = tr("保存文件失败：%1").arg(file.errorString());
    emit updateInfoChanged();
    emit updateDownloadChanged();
    return;
  }

  downloadProgress_ = 1.0;
  downloadSavedPath_ = targetPath;
  updateStatusText_ = tr("已下载到 %1").arg(downloadSavedPath_);
  emit updateInfoChanged();
  emit updateDownloadChanged();
}

void Backend::resetUpdateCheck() {
  if (currentUpdateReply_) {
    currentUpdateReply_->disconnect(this);
    currentUpdateReply_->deleteLater();
    currentUpdateReply_.clear();
  }
  checkingUpdate_ = false;
}

void Backend::resetDownloadState() {
  if (currentDownloadReply_) {
    currentDownloadReply_->disconnect(this);
    currentDownloadReply_->deleteLater();
    currentDownloadReply_.clear();
  }
  downloadingUpdate_ = false;
  downloadProgress_ = 0.0;
  downloadSavedPath_.clear();
  downloadTargetDir_.clear();
  downloadTargetRequested_.clear();
}

bool Backend::isVersionNewer(const QString &candidate,
                             const QString &current) const {
  const auto parse = [](const QString &v) {
    QStringList parts = v.split('.');
    while (parts.size() < 3) {
      parts.append(QStringLiteral("0"));
    }
    std::array<int, 3> vals{0, 0, 0};
    for (int i = 0; i < 3 && i < parts.size(); ++i) {
      bool ok = false;
      vals[static_cast<size_t>(i)] = parts[i].toInt(&ok);
      if (!ok) {
        vals[static_cast<size_t>(i)] = 0;
      }
    }
    return vals;
  };

  const auto a = parse(candidate);
  const auto b = parse(current);
  if (a[0] != b[0]) {
    return a[0] > b[0];
  }
  if (a[1] != b[1]) {
    return a[1] > b[1];
  }
  return a[2] > b[2];
}

QString Backend::normalizeVersion(const QString &input) const {
  QString v = input.trimmed();
  if (v.startsWith('v') || v.startsWith('V')) {
    v = v.mid(1);
  }
  return v.isEmpty() ? QStringLiteral("0.0.0") : v;
}

QString Backend::preferredDownloadUrl(bool useProxy) const {
  if (latestDownloadUrl_.isEmpty()) {
    return {};
  }
  const QString baseUrl = stripGhProxyPrefix(latestDownloadUrl_);
  if (!useProxy) {
    return baseUrl;
  }
  return QStringLiteral("https://gh-proxy.org/%1").arg(baseUrl);
}
