#pragma once

#include "application/runtime/application_runtime.h"

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QQmlEngine;
class QJSEngine;

class SessionController final : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(bool steamReady READ steamReady NOTIFY changed)
  Q_PROPERTY(bool host READ host NOTIFY changed)
  Q_PROPERTY(bool connected READ connected NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)
  Q_PROPERTY(QString lobbyId READ lobbyId NOTIFY changed)
  Q_PROPERTY(QString lobbyName READ lobbyName NOTIFY changed)
  Q_PROPERTY(QString hostSteamId READ hostSteamId NOTIFY changed)
  Q_PROPERTY(QString selfSteamId READ selfSteamId NOTIFY changed)
  Q_PROPERTY(QString joinTarget READ joinTarget WRITE setJoinTarget NOTIFY changed)
  Q_PROPERTY(QString roomName READ roomName WRITE setRoomName NOTIFY changed)
  Q_PROPERTY(bool published READ published WRITE setPublished NOTIFY changed)
  Q_PROPERTY(int localPort READ localPort WRITE setLocalPort NOTIFY changed)
  Q_PROPERTY(int bindPort READ bindPort WRITE setBindPort NOTIFY changed)
  Q_PROPERTY(int tcpClients READ tcpClients NOTIFY changed)
  Q_PROPERTY(QString tunIp READ tunIp NOTIFY changed)
  Q_PROPERTY(QString tunDevice READ tunDevice NOTIFY changed)

public:
  explicit SessionController(ApplicationRuntime &runtime);

  [[nodiscard]] bool steamReady() const { return runtime_.steamReady(); }
  [[nodiscard]] bool host() const { return runtime_.isHost(); }
  [[nodiscard]] bool connected() const { return runtime_.isConnected(); }
  [[nodiscard]] QString status() const { return runtime_.status(); }
  [[nodiscard]] int mode() const { return runtime_.connectionMode(); }
  [[nodiscard]] QString lobbyId() const { return runtime_.lobbyId(); }
  [[nodiscard]] QString lobbyName() const { return runtime_.lobbyName(); }
  [[nodiscard]] QString hostSteamId() const { return runtime_.hostSteamId(); }
  [[nodiscard]] QString selfSteamId() const { return runtime_.selfSteamId(); }
  [[nodiscard]] QString joinTarget() const { return runtime_.joinTarget(); }
  [[nodiscard]] QString roomName() const { return runtime_.roomName(); }
  [[nodiscard]] bool published() const { return runtime_.publishLobby(); }
  [[nodiscard]] int localPort() const { return runtime_.localPort(); }
  [[nodiscard]] int bindPort() const { return runtime_.localBindPort(); }
  [[nodiscard]] int tcpClients() const { return runtime_.tcpClients(); }
  [[nodiscard]] QString tunIp() const { return runtime_.tunLocalIp(); }
  [[nodiscard]] QString tunDevice() const { return runtime_.tunDeviceName(); }

  void setMode(int mode) { runtime_.setConnectionMode(mode); }
  void setJoinTarget(const QString &target) { runtime_.setJoinTarget(target); }
  void setRoomName(const QString &name) { runtime_.setRoomName(name); }
  void setPublished(bool published) { runtime_.setPublishLobby(published); }
  void setLocalPort(int port) { runtime_.setLocalPort(port); }
  void setBindPort(int port) { runtime_.setLocalBindPort(port); }

  Q_INVOKABLE void start() { runtime_.startHosting(); }
  Q_INVOKABLE void join() { runtime_.joinHost(); }
  Q_INVOKABLE void disconnect() { runtime_.disconnect(); }

signals:
  void changed();
  void adminPrivilegesRequired();
  void tunStartDenied();

private:
  ApplicationRuntime &runtime_;
};

class LobbyController final : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(LobbiesModel *model READ model CONSTANT)
  Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)
  Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY changed)

public:
  explicit LobbyController(ApplicationRuntime &runtime);
  [[nodiscard]] LobbiesModel *model() { return runtime_.lobbiesModel(); }
  [[nodiscard]] bool refreshing() const { return runtime_.lobbyRefreshing(); }
  [[nodiscard]] QString filter() const { return runtime_.lobbyFilter(); }
  [[nodiscard]] int sortMode() const { return runtime_.lobbySortMode(); }
  void setFilter(const QString &filter) { runtime_.setLobbyFilter(filter); }
  void setSortMode(int mode) { runtime_.setLobbySortMode(mode); }
  Q_INVOKABLE void refresh() { runtime_.refreshLobbies(); }
  Q_INVOKABLE void join(const QString &lobbyId) { runtime_.joinLobby(lobbyId); }

signals:
  void changed();

private:
  ApplicationRuntime &runtime_;
};

class SocialController final : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(FriendsModel *friends READ friends CONSTANT)
  Q_PROPERTY(MembersModel *members READ members CONSTANT)
  Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)

public:
  explicit SocialController(ApplicationRuntime &runtime);
  [[nodiscard]] FriendsModel *friends() { return runtime_.friendsModel(); }
  [[nodiscard]] MembersModel *members() { return runtime_.membersModel(); }
  [[nodiscard]] bool refreshing() const { return runtime_.friendsRefreshing(); }
  [[nodiscard]] QString filter() const { return runtime_.friendFilter(); }
  void setFilter(const QString &filter) { runtime_.setFriendFilter(filter); }
  Q_INVOKABLE void refreshFriends() { runtime_.refreshFriends(); }
  Q_INVOKABLE void refreshMembers() { runtime_.refreshMembers(); }
  Q_INVOKABLE void invite(const QString &steamId) { runtime_.inviteFriend(steamId); }
  Q_INVOKABLE void addFriend(const QString &steamId) { runtime_.addFriend(steamId); }

signals:
  void changed();

private:
  ApplicationRuntime &runtime_;
};

class ChatController final : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(ChatModel *model READ model CONSTANT)
  Q_PROPERTY(bool reminderEnabled READ reminderEnabled WRITE setReminderEnabled NOTIFY changed)

public:
  explicit ChatController(ApplicationRuntime &runtime);
  [[nodiscard]] ChatModel *model() { return runtime_.chatModel(); }
  [[nodiscard]] bool reminderEnabled() const { return runtime_.chatReminderEnabled(); }
  void setReminderEnabled(bool enabled) { runtime_.setChatReminderEnabled(enabled); }
  Q_INVOKABLE void send(const QString &message) { runtime_.sendChatMessage(message); }
  Q_INVOKABLE void pin(int row) { runtime_.pinChatMessage(row); }
  Q_INVOKABLE void clearPin() { runtime_.clearPinnedChatMessage(); }

signals:
  void changed();

private:
  ApplicationRuntime &runtime_;
};

class NetworkController final : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(int relayPing READ relayPing NOTIFY changed)
  Q_PROPERTY(QVariantList relayPops READ relayPops NOTIFY changed)

public:
  explicit NetworkController(ApplicationRuntime &runtime);
  [[nodiscard]] int relayPing() const { return runtime_.relayPing(); }
  [[nodiscard]] QVariantList relayPops() const { return runtime_.relayPops(); }

signals:
  void changed();

private:
  ApplicationRuntime &runtime_;
};

class UpdaterController final : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
  Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
  Q_PROPERTY(QString releasePage READ releasePage NOTIFY changed)
  Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
  Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
  Q_PROPERTY(bool checking READ checking NOTIFY changed)
  Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
  Q_PROPERTY(double progress READ progress NOTIFY changed)
  Q_PROPERTY(QString savedPath READ savedPath NOTIFY changed)

public:
  explicit UpdaterController(UpdateController &updater);

  [[nodiscard]] QString currentVersion() const { return updater_.currentVersion(); }
  [[nodiscard]] QString latestVersion() const { return updater_.latestVersion(); }
  [[nodiscard]] QString releasePage() const { return updater_.releasePage(); }
  [[nodiscard]] QString statusText() const { return updater_.statusText(); }
  [[nodiscard]] bool updateAvailable() const { return updater_.updateAvailable(); }
  [[nodiscard]] bool checking() const { return updater_.checking(); }
  [[nodiscard]] bool downloading() const { return updater_.downloading(); }
  [[nodiscard]] double progress() const { return updater_.progress(); }
  [[nodiscard]] QString savedPath() const { return updater_.savedPath(); }

  Q_INVOKABLE void check(bool useProxy = false) { updater_.check(useProxy); }
  Q_INVOKABLE void download(bool useProxy, const QString &targetPath) {
    updater_.download(useProxy, targetPath);
  }

signals:
  void changed();

private:
  UpdateController &updater_;
};

class ApplicationController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(SessionController *session READ session CONSTANT)
  Q_PROPERTY(LobbyController *lobbies READ lobbies CONSTANT)
  Q_PROPERTY(SocialController *social READ social CONSTANT)
  Q_PROPERTY(ChatController *chat READ chat CONSTANT)
  Q_PROPERTY(NetworkController *network READ network CONSTANT)
  Q_PROPERTY(UpdaterController *updater READ updater CONSTANT)
  Q_PROPERTY(QString version READ version CONSTANT)

public:
  explicit ApplicationController(ApplicationRuntime &runtime);

  [[nodiscard]] SessionController *session() { return &session_; }
  [[nodiscard]] LobbyController *lobbies() { return &lobbies_; }
  [[nodiscard]] SocialController *social() { return &social_; }
  [[nodiscard]] ChatController *chat() { return &chat_; }
  [[nodiscard]] NetworkController *network() { return &network_; }
  [[nodiscard]] UpdaterController *updater() { return &updater_; }
  [[nodiscard]] QString version() const { return runtime_.appVersion(); }

  Q_INVOKABLE void copy(const QString &text) {
    runtime_.copyToClipboard(text);
    emit copied();
  }
  Q_INVOKABLE void launchSteam(bool china = false) { runtime_.launchSteam(china); }

signals:
  void copied();

private:
  ApplicationRuntime &runtime_;
  SessionController session_;
  LobbyController lobbies_;
  SocialController social_;
  ChatController chat_;
  NetworkController network_;
  UpdaterController updater_;
};

// Expose the single application façade as a typed QML singleton.  The
// controller itself deliberately has no default constructor: its dependencies
// are assembled once in main and bound before the QML engine is created.
struct ApplicationControllerRegistration {
  Q_GADGET
  QML_FOREIGN(ApplicationController)
  QML_NAMED_ELEMENT(App)
  QML_SINGLETON

public:
  static void bind(ApplicationController &application);
  static ApplicationController *create(QQmlEngine *engine, QJSEngine *scriptEngine);

private:
  inline static ApplicationController *instance_ = nullptr;
};
