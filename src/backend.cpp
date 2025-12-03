#include "backend.h"

#include "../net/tcp_server.h"
#include "../steam/steam_networking_manager.h"
#include "../steam/steam_room_manager.h"
#include "../steam/steam_utils.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlEngine>
#include <QVariantMap>
#include <QtDebug>
#include <algorithm>
#include <mutex>
#include <unordered_set>

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
} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent), steamReady_(false), localPort_(25565),
      localBindPort_(8888), lastTcpClients_(0), lastMemberLogCount_(-1) {
  // Set a default app id so Steam can bootstrap in development environments
  qputenv("SteamAppId", QByteArray("480"));
  qputenv("SteamGameId", QByteArray("480"));

  steamReady_ = SteamAPI_Init();
  if (!steamReady_) {
    status_ = tr("无法初始化 Steam API，请确认客户端已登录。");
    return;
  }

  steamManager_ = std::make_unique<SteamNetworkingManager>();
  if (!steamManager_->initialize()) {
    status_ = tr("Steam 网络初始化失败。");
    steamReady_ = false;
    return;
  }

  roomManager_ = std::make_unique<SteamRoomManager>(steamManager_.get());

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
  slowTimer_.start(5000);

  refreshFriends();
  updateMembersList();
  updateStatus();
}

Backend::~Backend() {
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
  return steamReady_ && steamManager_ && steamManager_->isHost();
}

bool Backend::isConnected() const {
  return steamReady_ && steamManager_ && steamManager_->isConnected();
}

QString Backend::lobbyId() const {
  if (!roomManager_) {
    return {};
  }
  CSteamID lobby = roomManager_->getCurrentLobby();
  return lobby.IsValid() ? QString::number(lobby.ConvertToUint64()) : QString();
}

int Backend::tcpClients() const {
  return server_ ? server_->getClientCount() : 0;
}

void Backend::setJoinTarget(const QString &id) {
  if (joinTarget_ == id) {
    return;
  }
  joinTarget_ = id;
  emit joinTargetChanged();
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
  emit errorMessage(tr("无法%1：Steam 未初始化。").arg(actionLabel));
  return false;
}

void Backend::startHosting() {
  if (!ensureSteamReady(tr("主持房间"))) {
    return;
  }
  if (isHost()) {
    emit errorMessage(tr("已经在主持房间中。"));
    return;
  }
  if (isConnected()) {
    emit errorMessage(tr("当前已连接到房间，请先断开。"));
    return;
  }

  if (roomManager_ && roomManager_->startHosting()) {
    steamManager_->setHostSteamID(SteamUser()->GetSteamID());
    updateStatus();
  } else {
    emit errorMessage(tr("创建房间失败，请检查 Steam 状态。"));
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
    emit errorMessage(tr("启动本地 TCP 服务器失败。"));
    server_.reset();
    lastTcpClients_ = 0;
  }
  emit serverChanged();
}

void Backend::joinHost() {
  if (!ensureSteamReady(tr("加入房间"))) {
    return;
  }
  if (isConnected()) {
    emit errorMessage(tr("已经连接到房间，请先断开。"));
    return;
  }
  const QString trimmedTarget = joinTarget_.trimmed();
  if (trimmedTarget.isEmpty()) {
    startHosting();
    return;
  }
  bool ok = false;
  uint64 hostID = trimmedTarget.toULongLong(&ok);
  if (!ok) {
    emit errorMessage(tr("请输入有效的房间/Steam ID。"));
    return;
  }

  CSteamID targetSteamID(hostID);
  if (!targetSteamID.IsValid()) {
    emit errorMessage(tr("无效的房间/Steam ID。"));
    return;
  }

  // 如果输入的是房间 ID，先加入房间再由回调发起 P2P 连接
  if (targetSteamID.IsLobby()) {
    if (roomManager_ && roomManager_->joinLobby(targetSteamID)) {
      updateStatus();
    } else {
      emit errorMessage(tr("无法加入房间。"));
    }
    return;
  }

  if (steamManager_->joinHost(hostID)) {
    ensureServerRunning();
    updateStatus();
  } else {
    emit errorMessage(tr("无法连接到房主。"));
  }
}

void Backend::disconnect() {
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
}

void Backend::refreshFriends() {
  if (!steamReady_ || !steamManager_) {
    return;
  }
  QVariantList updated;
  std::vector<FriendsModel::Entry> modelData;
  int idx = 0;
  for (const auto &friendInfo : SteamUtils::getFriendsList()) {
    const QString steamId = QString::number(friendInfo.id.ConvertToUint64());
    const QString displayName = QString::fromStdString(friendInfo.name);
    const QString avatar = QString::fromStdString(friendInfo.avatarDataUrl);
    const PersonaDisplay persona = personaStateDisplay(friendInfo.personaState);

    QVariantMap entry;
    entry.insert(QStringLiteral("id"), steamId);
    entry.insert(QStringLiteral("name"), displayName);
    entry.insert(QStringLiteral("status"), persona.label);
    entry.insert(QStringLiteral("online"), persona.online);
    if (!avatar.isEmpty()) {
      entry.insert(QStringLiteral("avatar"), avatar);
    }
    updated.push_back(entry);
    modelData.push_back({steamId, displayName, avatar, persona.online,
                         persona.label, persona.priority});
    ++idx;
  }
  friendsModel_.setFriends(std::move(modelData));
  if (updated != friends_) {
    friends_ = updated;
    emit friendsChanged();
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

void Backend::refreshMembers() { updateMembersList(); }

void Backend::inviteFriend(const QString &steamId) {
  if (!ensureSteamReady(tr("邀请好友"))) {
    return;
  }
  bool ok = false;
  uint64 friendId = steamId.toULongLong(&ok);
  if (!ok) {
    emit errorMessage(tr("无效的好友 ID。"));
    return;
  }
  if (roomManager_ && roomManager_->getCurrentLobby().IsValid()) {
    SteamMatchmaking()->InviteUserToLobby(roomManager_->getCurrentLobby(),
                                          CSteamID(friendId));
  } else {
    emit errorMessage(tr("当前未在房间中，无法邀请。"));
  }
}

void Backend::tick() {
  if (!steamReady_ || !steamManager_) {
    return;
  }

  SteamAPI_RunCallbacks();
  steamManager_->update();

  updateMembersList();
  updateStatus();
}

void Backend::updateStatus() {
  QString next;
  if (!steamReady_) {
    next = tr("Steam 未就绪。");
  } else if (isHost()) {
    const QString id = lobbyId();
    next = id.isEmpty() ? tr("主持房间中…") : tr("主持房间 %1").arg(id);
  } else if (isConnected()) {
    const QString id = lobbyId();
    next = id.isEmpty() ? tr("已连接到房间") : tr("已连接到房主 %1").arg(id);
  } else {
    next = tr("空闲，等待创建或加入房间。");
  }

  const int clientCount = tcpClients();
  if (server_) {
    next += tr(" · 本地 TCP 客户端 %1").arg(clientCount);
  }

  if (clientCount != lastTcpClients_) {
    lastTcpClients_ = clientCount;
    emit serverChanged();
  }

  if (next != status_) {
    status_ = next;
    emit stateChanged();
  }
}

void Backend::updateMembersList() {
  if (!steamReady_ || !steamManager_) {
    membersModel_.setMembers({});
    memberAvatars_.clear();
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
      } else if (isHost()) {
        std::lock_guard<std::mutex> lock(steamManager_->getConnectionsMutex());
        for (const auto &conn : steamManager_->getConnections()) {
          SteamNetConnectionInfo_t info;
          if (steamManager_->getInterface()->GetConnectionInfo(conn, &info) &&
              info.m_identityRemote.GetSteamID() == memberId) {
            entry.ping = steamManager_->getConnectionPing(conn);
            entry.relay = QString::fromStdString(
                steamManager_->getConnectionRelayInfo(conn));
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
      entry.steamId = QString::number(remoteValue);
      entry.displayName =
          QString::fromUtf8(SteamFriends()->GetFriendPersonaName(remoteId));
      entry.avatar = avatarForMember(remoteId);
      entry.ping = steamManager_->getConnectionPing(conn);
      const std::string relayInfo = steamManager_->getConnectionRelayInfo(conn);
      entry.relay =
          relayInfo.empty() ? tr("直连") : QString::fromStdString(relayInfo);

      entries.push_back(std::move(entry));
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
}
