#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <boost/asio.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <QPointer>
#include <thread>
#include <unordered_map>
#include <QSaveFile>

#include "friends_model.h"
#include "chat_model.h"
#include "lobbies_model.h"
#include "members_model.h"
#include "steam_room_manager.h"
#include "sound_notifier.h"

class SteamNetworkingManager;
class TCPServer;
class SteamVpnNetworkingManager;
class SteamVpnBridge;
class QWindow;

class Backend : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool steamReady READ steamReady NOTIFY stateChanged)
  Q_PROPERTY(bool isHost READ isHost NOTIFY stateChanged)
  Q_PROPERTY(bool isConnected READ isConnected NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  Q_PROPERTY(int connectionMode READ connectionMode WRITE setConnectionMode
                 NOTIFY stateChanged)
  Q_PROPERTY(QString lobbyId READ lobbyId NOTIFY stateChanged)
  Q_PROPERTY(QString lobbyName READ lobbyName NOTIFY stateChanged)
  Q_PROPERTY(bool publishLobby READ publishLobby WRITE setPublishLobby NOTIFY
                 publishLobbyChanged)
  Q_PROPERTY(QString hostSteamId READ hostSteamId NOTIFY hostSteamIdChanged)
  Q_PROPERTY(QString joinTarget READ joinTarget WRITE setJoinTarget NOTIFY
                 joinTargetChanged)
  Q_PROPERTY(int relayPing READ relayPing NOTIFY relayPingChanged)
  Q_PROPERTY(QVariantList relayPops READ relayPops NOTIFY relayPopsChanged)
  Q_PROPERTY(int tcpClients READ tcpClients NOTIFY serverChanged)
  Q_PROPERTY(
      int localPort READ localPort WRITE setLocalPort NOTIFY localPortChanged)
  Q_PROPERTY(int localBindPort READ localBindPort WRITE setLocalBindPort NOTIFY
                 localBindPortChanged)
  Q_PROPERTY(QVariantList friends READ friends NOTIFY friendsChanged)
  Q_PROPERTY(FriendsModel *friendsModel READ friendsModel NOTIFY friendsChanged)
  Q_PROPERTY(QString friendFilter READ friendFilter WRITE setFriendFilter NOTIFY
                 friendFilterChanged)
  Q_PROPERTY(ChatModel *chatModel READ chatModel CONSTANT)
  Q_PROPERTY(bool chatReminderEnabled READ chatReminderEnabled WRITE
                 setChatReminderEnabled NOTIFY chatReminderEnabledChanged)
  Q_PROPERTY(bool friendsRefreshing READ friendsRefreshing NOTIFY
                 friendsRefreshingChanged)
  Q_PROPERTY(QString selfSteamId READ selfSteamId NOTIFY stateChanged)
  Q_PROPERTY(MembersModel *membersModel READ membersModel CONSTANT)
  Q_PROPERTY(LobbiesModel *lobbiesModel READ lobbiesModel CONSTANT)
  Q_PROPERTY(
      QString roomName READ roomName WRITE setRoomName NOTIFY roomNameChanged)
  Q_PROPERTY(
      bool lobbyRefreshing READ lobbyRefreshing NOTIFY lobbyRefreshingChanged)
  Q_PROPERTY(QString lobbyFilter READ lobbyFilter WRITE setLobbyFilter NOTIFY
                 lobbyFilterChanged)
  Q_PROPERTY(int lobbySortMode READ lobbySortMode WRITE setLobbySortMode NOTIFY
                 lobbySortModeChanged)
  Q_PROPERTY(QString tunLocalIp READ tunLocalIp NOTIFY stateChanged)
  Q_PROPERTY(QString tunDeviceName READ tunDeviceName NOTIFY stateChanged)
  Q_PROPERTY(
      int inviteCooldown READ inviteCooldown NOTIFY inviteCooldownChanged)
  Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
  Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateInfoChanged)
  Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateInfoChanged)
  Q_PROPERTY(bool checkingUpdate READ checkingUpdate NOTIFY updateInfoChanged)
  Q_PROPERTY(QString updateStatusText READ updateStatusText NOTIFY
                 updateInfoChanged)
  Q_PROPERTY(QString latestReleasePage READ latestReleasePage NOTIFY
                 updateInfoChanged)
  Q_PROPERTY(bool downloadingUpdate READ downloadingUpdate NOTIFY
                 updateDownloadChanged)
  Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY
                 updateDownloadChanged)
  Q_PROPERTY(QString downloadSavedPath READ downloadSavedPath NOTIFY
                 updateDownloadChanged)

public:
  enum class ConnectionMode { Tcp = 0, Tun = 1 };

  explicit Backend(QObject *parent = nullptr);
  ~Backend();

  bool steamReady() const { return steamReady_; }
  bool isHost() const;
  bool isConnected() const;
  QString status() const { return status_; }
  int connectionMode() const {
    return static_cast<int>(connectionMode_);
  }
  QString lobbyId() const;
  QString lobbyName() const;
  QString hostSteamId() const { return hostSteamId_; }
  QString joinTarget() const { return joinTarget_; }
  bool publishLobby() const { return publishLobby_; }
  int tcpClients() const;
  int localPort() const { return localPort_; }
  int localBindPort() const { return localBindPort_; }
  QVariantList friends() const { return friends_; }
  FriendsModel *friendsModel() { return &friendsModel_; }
  LobbiesModel *lobbiesModel() { return &lobbiesModel_; }
  MembersModel *membersModel() { return &membersModel_; }
  ChatModel *chatModel() { return &chatModel_; }
  bool chatReminderEnabled() const { return chatReminderEnabled_; }
  int relayPing() const { return relayPingMs_; }
  QVariantList relayPops() const { return relayPops_; }
  QString friendFilter() const { return friendFilter_; }
  bool friendsRefreshing() const { return friendsRefreshing_; }
  QString selfSteamId() const;
  int inviteCooldown() const { return inviteCooldownSeconds_; }
  QString roomName() const { return roomName_; }
  bool lobbyRefreshing() const { return lobbyRefreshing_; }
  QString lobbyFilter() const { return lobbyFilter_; }
  int lobbySortMode() const { return lobbySortMode_; }
  QString tunLocalIp() const { return tunLocalIp_; }
  QString tunDeviceName() const { return tunDeviceName_; }
  QString appVersion() const { return appVersion_; }
  QString latestVersion() const { return latestVersion_; }
  QString latestReleasePage() const { return latestReleasePage_; }
  bool updateAvailable() const { return updateAvailable_; }
  bool checkingUpdate() const { return checkingUpdate_; }
  QString updateStatusText() const { return updateStatusText_; }
  bool downloadingUpdate() const { return downloadingUpdate_; }
  double downloadProgress() const { return downloadProgress_; }
  QString downloadSavedPath() const { return downloadSavedPath_; }

  void setJoinTarget(const QString &id);
  void setPublishLobby(bool publish);
  void setLocalPort(int port);
  void setLocalBindPort(int port);
  void setFriendFilter(const QString &text);
  void setRoomName(const QString &name);
  void setLobbyFilter(const QString &text);
  void setLobbySortMode(int mode);
  void setConnectionMode(int mode);
  void handleLobbyModeChanged(bool wantsTun, const CSteamID &lobby);

  Q_INVOKABLE void startHosting();
  Q_INVOKABLE void joinHost();
  Q_INVOKABLE void joinLobby(const QString &lobbyId);
  Q_INVOKABLE void disconnect();
  Q_INVOKABLE void refreshFriends();
  Q_INVOKABLE void refreshLobbies();
  Q_INVOKABLE void refreshMembers();
  Q_INVOKABLE void inviteFriend(const QString &steamId);
  Q_INVOKABLE void addFriend(const QString &steamId);
  Q_INVOKABLE void copyToClipboard(const QString &text);
  Q_INVOKABLE void sendChatMessage(const QString &text);
  Q_INVOKABLE void pinChatMessage(const QString &steamId,
                                  const QString &displayName,
                                  const QString &avatar,
                                  const QString &message,
                                  const QDateTime &timestamp);
  Q_INVOKABLE void clearPinnedChatMessage();
  Q_INVOKABLE void launchSteam(bool useSteamChina);
  Q_INVOKABLE void checkForUpdates(bool useProxy = false);
  Q_INVOKABLE void downloadUpdate(bool useProxy, const QString &targetPath);
  void initializeSound(QWindow *window);
  void setChatReminderEnabled(bool enabled);

signals:
  void stateChanged();
  void joinTargetChanged();
  void localPortChanged();
  void localBindPortChanged();
  void friendsChanged();
  void serverChanged();
  void friendFilterChanged();
  void inviteCooldownChanged();
  void hostSteamIdChanged();
  void roomNameChanged();
  void publishLobbyChanged();
  void friendsRefreshingChanged();
  void lobbyRefreshingChanged();
  void lobbyFilterChanged();
  void lobbySortModeChanged();
  void adminPrivilegesRequired();
  void tunStartDenied();
  void relayPingChanged();
  void relayPopsChanged();
  void updateInfoChanged();
  void updateDownloadChanged();
  void chatReminderEnabledChanged();

private:
  void tick();
  void updateStatus();
  void updateMembersList();
  void updateFriendsList();
  void
  updateLobbiesList(const std::vector<SteamRoomManager::LobbyInfo> &lobbies);
  QString avatarForSteamId(const CSteamID &memberId);
  void handleChatMessage(uint64_t senderId, const QString &message);
  void ensureServerRunning();
  bool ensureSteamReady(const QString &actionLabel);
  void refreshHostId();
  void updateFriendCooldown(const QString &steamId, int seconds);
  void updateLobbyInfoSignals();
  void requestUserAttention();
  void setFriendsRefreshing(bool refreshing);
  void updateRelayPing();
  void handlePinnedMessageMetadata(const QString &payload);
  std::optional<ChatModel::Entry>
  parsePinnedMessagePayload(const QString &payload) const;
  QString serializePinnedMessage(const ChatModel::Entry &entry) const;
  ChatModel::Entry populatePinnedEntryAvatar(ChatModel::Entry entry,
                                             bool isSelfAuthor);
  void resetUpdateCheck();
  void resetDownloadState();
  void handleUpdateReply();
  void handleDownloadFinished();
  bool isVersionNewer(const QString &candidate, const QString &current) const;
  QString normalizeVersion(const QString &input) const;
  QString preferredDownloadUrl(bool useProxy) const;
  void setLobbyRefreshing(bool refreshing);
  void setStatusOverride(const QString &text, int durationMs = 3000);
  void clearStatusOverride();
  void setJoinTargetFromLobby(const QString &id);
  void ensureVpnSetup();
  void stopVpn();
  void syncVpnPeers();
  void updateVpnInfo();
  bool applyLobbyModePreference(const CSteamID &lobby);
  bool inTunMode() const { return connectionMode_ == ConnectionMode::Tun; }
  bool inTcpMode() const { return connectionMode_ == ConnectionMode::Tcp; }
  void ensureVpnRunning();
  bool hasAdminPrivileges() const;
  bool ensureTunPrivileges();
  bool tryInitializeSteam();
  void refreshSelfSteamId();

  std::unique_ptr<SteamNetworkingManager> steamManager_;
  std::unique_ptr<SteamVpnNetworkingManager> vpnManager_;
  std::unique_ptr<SteamVpnBridge> vpnBridge_;
  std::unique_ptr<SteamRoomManager> roomManager_;
  std::unique_ptr<TCPServer> server_;
  boost::asio::io_context ioContext_;
  std::unique_ptr<
      boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
      workGuard_;
  std::thread ioThread_;
  QTimer callbackTimer_;
  QTimer steamCheckTimer_;
  QTimer slowTimer_;
  QTimer cooldownTimer_;
  QTimer friendsRefreshResetTimer_;
  QTimer statusOverrideTimer_;

  bool steamReady_;
  QString status_;
  QString statusOverride_;
  QString joinTarget_;
  QString lastAutoJoinTarget_;
  QString hostSteamId_;
  int localPort_;
  int localBindPort_;
  int lastTcpClients_;
  int lastMemberLogCount_;
  QVariantList friends_;
  FriendsModel friendsModel_;
  LobbiesModel lobbiesModel_;
  MembersModel membersModel_;
  ChatModel chatModel_;
  SoundNotifier soundNotifier_;
  QPointer<QWindow> mainWindow_;
  QString friendFilter_;
  QString selfSteamId_;
  std::unordered_map<uint64_t, QString> memberAvatars_;
  std::unordered_map<uint64_t, int> inviteCooldowns_;
  int inviteCooldownSeconds_ = 0;
  QString roomName_;
  bool publishLobby_ = false;
  QString lobbyFilter_;
  int lobbySortMode_ = 0;
  QString lastLobbyId_;
  QString lastLobbyName_;
  bool friendsRefreshing_ = false;
  bool lobbyRefreshing_ = false;
  std::chrono::steady_clock::time_point lastPingBroadcast_;
  std::chrono::steady_clock::time_point lastRelayPingSample_;
  ConnectionMode connectionMode_ = ConnectionMode::Tun;
  bool vpnHosting_ = false;
  bool vpnConnected_ = false;
  bool vpnWanted_ = false;
  bool vpnStartAttempted_ = false;
  QString tunLocalIp_;
  QString tunDeviceName_;
  int relayPingMs_ = -1;
  QVariantList relayPops_;
  // Update info
  QString appVersion_;
  QString latestVersion_;
  QString latestDownloadUrl_;
  QString latestReleasePage_;
  QString updateStatusText_;
  QString downloadSavedPath_;
  QString downloadTargetDir_;
  QString downloadTargetRequested_;
  bool updateAvailable_ = false;
  bool checkingUpdate_ = false;
  bool downloadingUpdate_ = false;
  double downloadProgress_ = 0.0;
  bool chatReminderEnabled_ = true;
  QNetworkAccessManager networkManager_;
  QPointer<QNetworkReply> currentUpdateReply_;
  QPointer<QNetworkReply> currentDownloadReply_;
};
