#include "backend.h"

#include "../net/tcp_server.h"
#include "../steam/steam_networking_manager.h"
#include "../steam/steam_room_manager.h"
#include "../steam/steam_vpn_bridge.h"
#include "../steam/steam_vpn_networking_manager.h"
#include "../steam/steam_utils.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlEngine>
#include <QVariantMap>
#include <QUrl>
#include <QtDebug>
#include <algorithm>
#include <chrono>
#include <isteamnetworkingutils.h>
#ifdef Q_OS_LINUX
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include <mutex>
#include <sstream>
#include <steam_api.h>
#include <steamnetworkingtypes.h>
#include <unordered_set>
#include <set>
#include <vector>
#include <utility>

namespace {
struct PersonaDisplay {
  QString label;
  bool online;
  int priority;
};

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
  qDebug() << "[SteamNet]" << msg;
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
    struct passwd pwd {};
    struct passwd *result = nullptr;
    long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufsize < 0) {
      bufsize = 16384;
    }
    std::vector<char> buffer(static_cast<size_t>(bufsize));
    if (getpwnam_r(sudoUser.constData(), &pwd, buffer.data(),
                   buffer.size(), &result) == 0 &&
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
} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent), steamReady_(false), localPort_(25565),
      localBindPort_(8888), lastTcpClients_(0), lastMemberLogCount_(-1),
      roomName_(QCoreApplication::translate("Backend", "ConnectTool 房间")) {
  // Set a default app id so Steam can bootstrap in development environments
  qputenv("SteamAppId", QByteArray("480"));
  qputenv("SteamGameId", QByteArray("480"));

#ifdef Q_OS_LINUX
  fixSteamEnvForSudo();
#endif

  steamReady_ = SteamAPI_Init();
  if (!steamReady_) {
    status_ = tr("无法初始化 Steam API，请确认客户端已登录。");
    return;
  }
  if (SteamClient()) {
    SteamClient()->SetWarningMessageHook(&steamWarningHook);
  }
  if (SteamNetworkingUtils()) {
    SteamNetworkingUtils()->SetDebugOutputFunction(
        k_ESteamNetworkingSocketsDebugOutputType_Everything,
        &steamNetDebugHook);
  }
  roomName_ = defaultRoomName();
  emit roomNameChanged();

  steamManager_ = std::make_unique<SteamNetworkingManager>();
  if (!steamManager_->initialize()) {
    status_ = tr("Steam 网络初始化失败。");
    steamReady_ = false;
    return;
  }

  roomManager_ = std::make_unique<SteamRoomManager>(steamManager_.get());
  steamManager_->setRoomManager(roomManager_.get());
  roomManager_->setLobbyName(roomName_.toStdString());
  roomManager_->setPublishLobby(publishLobby_);
  roomManager_->setLobbyModeChangedCallback(
      [this](bool wantsTun, const CSteamID &lobby) {
        QMetaObject::invokeMethod(
            this,
            [this, wantsTun, lobby]() {
              handleLobbyModeChanged(wantsTun, lobby);
            },
            Qt::QueuedConnection);
      });
  roomManager_->setHostLeftCallback([this]() {
    QMetaObject::invokeMethod(
        this, [this]() { disconnect(); }, Qt::QueuedConnection);
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
  lobbiesModel_.setFilter(lobbyFilter_);
  lobbiesModel_.setSortMode(lobbySortMode_);

  workGuard_ = std::make_unique<
      boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
      boost::asio::make_work_guard(ioContext_));
  ioThread_ = std::thread([this]() { ioContext_.run(); });

  steamManager_->setMessageHandlerDependencies(ioContext_, server_, localPort_,
                                               localBindPort_);
  steamManager_->startMessageHandler();

  connect(&callbackTimer_, &QTimer::timeout, this, &Backend::tick);
  callbackTimer_.start(16);

  connect(&slowTimer_, &QTimer::timeout, this, &Backend::refreshFriends);
  slowTimer_.start(15000);

  friendsRefreshResetTimer_.setSingleShot(true);
  connect(&friendsRefreshResetTimer_, &QTimer::timeout, this,
          [this]() { setFriendsRefreshing(false); });

  statusOverrideTimer_.setSingleShot(true);
  connect(&statusOverrideTimer_, &QTimer::timeout, this,
          [this]() { clearStatusOverride(); });

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
  cooldownTimer_.start(1000);

  refreshFriends();
  updateMembersList();
  refreshHostId();
  updateStatus();
}

Backend::~Backend() {
  stopVpn();
  callbackTimer_.stop();
  slowTimer_.stop();

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

bool Backend::ensureSteamReady(const QString &actionLabel) {
  if (steamReady_) {
    return true;
  }
  qWarning() << tr("无法%1：Steam 未初始化。").arg(actionLabel);
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

  if (inTunMode()) {
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
    } else {
      qWarning() << tr("创建房间失败，请检查 Steam 状态。");
      vpnHosting_ = false;
    }
    return;
  }

  if (roomManager_ && roomManager_->startHosting()) {
    steamManager_->setHostSteamID(SteamUser()->GetSteamID());
    updateStatus();
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
  if (inTunMode()) {
    ensureVpnSetup();
    if (!vpnManager_ || !vpnBridge_) {
      return;
    }
    vpnWanted_ = true;
    clearStatusOverride();
    const auto markNotFound = [this]() {
      setStatusOverride(tr("房间不存在。"));
    };
    const QString trimmedTarget = joinTarget_.trimmed();
    if (trimmedTarget.isEmpty()) {
      startHosting();
      return;
    }
    bool ok = false;
    uint64 hostID = trimmedTarget.toULongLong(&ok);
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
    if (targetSteamID.IsLobby()) {
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
    } else {
      qWarning() << tr("无法加入房间。");
    }
    return;
    }
    if (targetSteamID.BIndividualAccount()) {
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
    } else {
      markNotFound();
      qWarning() << tr("房间不存在。");
    }
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
  uint64 hostID = trimmedTarget.toULongLong(&ok);
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

  if (!targetSteamID.IsLobby() && !targetSteamID.BIndividualAccount()) {
    markNotFound();
    qWarning() << tr("房间不存在。");
    return;
  }

  // 如果输入的是房间 ID，先加入房间再由回调发起 P2P 连接
  if (targetSteamID.IsLobby()) {
    if (roomManager_ && roomManager_->getCurrentLobby().IsValid()) {
      roomManager_->leaveLobby();
    }
    if (roomManager_ && roomManager_->joinLobby(targetSteamID)) {
      updateStatus();
    } else {
      qWarning() << tr("无法加入房间。");
    }
    return;
  }

  if (steamManager_->joinHost(hostID)) {
    ensureServerRunning();
    updateStatus();
  } else {
    qWarning() << tr("无法连接到房主。");
  }
}

void Backend::joinLobby(const QString &lobbyId) {
  if (!ensureSteamReady(tr("加入大厅"))) {
    return;
  }
  if (inTunMode()) {
    ensureVpnSetup();
    if (!vpnManager_ || !vpnBridge_) {
      return;
    }
    vpnWanted_ = true;
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
    } else {
      qWarning() << tr("无法加入房间。");
    }
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
  if (roomManager_ && roomManager_->getCurrentLobby().IsValid()) {
    roomManager_->leaveLobby();
  }

  setJoinTargetFromLobby(trimmedId);
  if (roomManager_ && roomManager_->joinLobby(lobby)) {
    updateStatus();
  } else {
    qWarning() << tr("无法加入房间。");
  }
}

void Backend::disconnect() {
  const bool wasHost = isHost();
  const QString prevLobbyId = lobbyId();
  const int prevMemberCount = membersModel_.count();
  QString mySteamId;
  if (steamReady_ && SteamUser()) {
    mySteamId = QString::number(SteamUser()->GetSteamID().ConvertToUint64());
  }

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

  if (!lastAutoJoinTarget_.isEmpty() && joinTarget_ == lastAutoJoinTarget_) {
    setJoinTarget(QString());
  }
  lastAutoJoinTarget_.clear();

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
  }
  if (next == ConnectionMode::Tun) {
    vpnStartAttempted_ = false;
  }
  updateStatus();
  emit stateChanged();
}

void Backend::handleLobbyModeChanged(bool wantsTun, const CSteamID &lobby) {
  if (!roomManager_ || lobby != roomManager_->getCurrentLobby()) {
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

  const bool overlayEnabled =
      SteamUtils() && SteamUtils()->IsOverlayEnabled();
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
    qWarning()
        << tr("已在浏览器中打开对方 Steam 个人主页，请在网页中添加好友。");
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

void Backend::copyToClipboard(const QString &text) {
  if (text.isEmpty()) {
    return;
  }
  QClipboard *cb = QGuiApplication::clipboard();
  if (cb) {
    cb->setText(text);
  }
}

void Backend::tick() {
  if (!steamReady_) {
    return;
  }

  SteamAPI_RunCallbacks();
  if (steamManager_) {
    steamManager_->update();
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

    const auto avatarForMember = [this](const CSteamID &memberId) -> QString {
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
    };

    for (const auto &memberId : lobbyMembers) {
      const uint64 memberValue = memberId.ConvertToUint64();
      seen.insert(memberValue);

      MembersModel::Entry entry;
      entry.isFriend = (SteamFriends() &&
                        SteamFriends()->HasFriend(memberId,
                                                  k_EFriendFlagImmediate)) ||
                       (SteamUser() && memberId == myId);
      entry.steamId = QString::number(memberValue);
      entry.displayName =
          QString::fromUtf8(SteamFriends()->GetFriendPersonaName(memberId));
      entry.avatar = avatarForMember(memberId);
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
        entry.ip = QString::fromStdString(SteamVpnBridge::ipToString(itIp->second));
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

  const auto avatarForMember = [this](const CSteamID &memberId) -> QString {
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
  };

  const auto isMemberHost = [&](const CSteamID &member) -> bool {
    return hostId.IsValid() && member == hostId;
  };

  for (const auto &memberId : lobbyMembers) {
    const uint64 memberValue = memberId.ConvertToUint64();
    seen.insert(memberValue);

    MembersModel::Entry entry;
    entry.isFriend = (SteamFriends() &&
                      SteamFriends()->HasFriend(memberId, k_EFriendFlagImmediate)) ||
                     (SteamUser() && memberId == myId);
    entry.steamId = QString::number(memberValue);
    entry.displayName =
        QString::fromUtf8(SteamFriends()->GetFriendPersonaName(memberId));
    entry.avatar = avatarForMember(memberId);
    entry.relay = QStringLiteral("-");
    entry.ping = -1;
    const bool memberIsHost = isMemberHost(memberId);

    if (memberId == myId) {
      entry.ping = 0;
      entry.relay = isHost() ? tr("房主") : tr("本机");
    } else {
      if (memberIsHost) {
        entry.relay = tr("房主");
        entry.ping = steamManager_->getHostPing();
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
      entry.steamId = QString::number(remoteValue);
      entry.displayName =
          QString::fromUtf8(SteamFriends()->GetFriendPersonaName(remoteId));
      entry.avatar = avatarForMember(remoteId);
      entry.ping = steamManager_->getConnectionPing(conn);
      const std::string relayInfo = steamManager_->getConnectionRelayInfo(conn);
      entry.relay =
          relayInfo.empty() ? tr("直连") : QString::fromStdString(relayInfo);
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
  if (id != lastLobbyId_ || name != lastLobbyName_) {
    lastLobbyId_ = id;
    lastLobbyName_ = name;
    emit stateChanged();
  }
}
