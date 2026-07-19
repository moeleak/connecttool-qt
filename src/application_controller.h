#pragma once

#include "backend.h"

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
  explicit SessionController(Backend &backend);

  [[nodiscard]] bool steamReady() const { return backend_.steamReady(); }
  [[nodiscard]] bool host() const { return backend_.isHost(); }
  [[nodiscard]] bool connected() const { return backend_.isConnected(); }
  [[nodiscard]] QString status() const { return backend_.status(); }
  [[nodiscard]] int mode() const { return backend_.connectionMode(); }
  [[nodiscard]] QString lobbyId() const { return backend_.lobbyId(); }
  [[nodiscard]] QString lobbyName() const { return backend_.lobbyName(); }
  [[nodiscard]] QString hostSteamId() const { return backend_.hostSteamId(); }
  [[nodiscard]] QString selfSteamId() const { return backend_.selfSteamId(); }
  [[nodiscard]] QString joinTarget() const { return backend_.joinTarget(); }
  [[nodiscard]] QString roomName() const { return backend_.roomName(); }
  [[nodiscard]] bool published() const { return backend_.publishLobby(); }
  [[nodiscard]] int localPort() const { return backend_.localPort(); }
  [[nodiscard]] int bindPort() const { return backend_.localBindPort(); }
  [[nodiscard]] int tcpClients() const { return backend_.tcpClients(); }
  [[nodiscard]] QString tunIp() const { return backend_.tunLocalIp(); }
  [[nodiscard]] QString tunDevice() const { return backend_.tunDeviceName(); }

  void setMode(int mode) { backend_.setConnectionMode(mode); }
  void setJoinTarget(const QString &target) { backend_.setJoinTarget(target); }
  void setRoomName(const QString &name) { backend_.setRoomName(name); }
  void setPublished(bool published) { backend_.setPublishLobby(published); }
  void setLocalPort(int port) { backend_.setLocalPort(port); }
  void setBindPort(int port) { backend_.setLocalBindPort(port); }

  Q_INVOKABLE void start() { backend_.startHosting(); }
  Q_INVOKABLE void join() { backend_.joinHost(); }
  Q_INVOKABLE void disconnect() { backend_.disconnect(); }

signals:
  void changed();
  void adminPrivilegesRequired();
  void tunStartDenied();

private:
  Backend &backend_;
};

class LobbyController final : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(LobbiesModel *model READ model CONSTANT)
  Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)
  Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY changed)

public:
  explicit LobbyController(Backend &backend);
  [[nodiscard]] LobbiesModel *model() { return backend_.lobbiesModel(); }
  [[nodiscard]] bool refreshing() const { return backend_.lobbyRefreshing(); }
  [[nodiscard]] QString filter() const { return backend_.lobbyFilter(); }
  [[nodiscard]] int sortMode() const { return backend_.lobbySortMode(); }
  void setFilter(const QString &filter) { backend_.setLobbyFilter(filter); }
  void setSortMode(int mode) { backend_.setLobbySortMode(mode); }
  Q_INVOKABLE void refresh() { backend_.refreshLobbies(); }
  Q_INVOKABLE void join(const QString &lobbyId) { backend_.joinLobby(lobbyId); }

signals:
  void changed();

private:
  Backend &backend_;
};

class SocialController final : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(FriendsModel *friends READ friends CONSTANT)
  Q_PROPERTY(MembersModel *members READ members CONSTANT)
  Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)

public:
  explicit SocialController(Backend &backend);
  [[nodiscard]] FriendsModel *friends() { return backend_.friendsModel(); }
  [[nodiscard]] MembersModel *members() { return backend_.membersModel(); }
  [[nodiscard]] bool refreshing() const { return backend_.friendsRefreshing(); }
  [[nodiscard]] QString filter() const { return backend_.friendFilter(); }
  void setFilter(const QString &filter) { backend_.setFriendFilter(filter); }
  Q_INVOKABLE void refreshFriends() { backend_.refreshFriends(); }
  Q_INVOKABLE void refreshMembers() { backend_.refreshMembers(); }
  Q_INVOKABLE void invite(const QString &steamId) { backend_.inviteFriend(steamId); }
  Q_INVOKABLE void addFriend(const QString &steamId) { backend_.addFriend(steamId); }

signals:
  void changed();

private:
  Backend &backend_;
};

class ChatController final : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(ChatModel *model READ model CONSTANT)
  Q_PROPERTY(bool reminderEnabled READ reminderEnabled WRITE setReminderEnabled NOTIFY changed)

public:
  explicit ChatController(Backend &backend);
  [[nodiscard]] ChatModel *model() { return backend_.chatModel(); }
  [[nodiscard]] bool reminderEnabled() const { return backend_.chatReminderEnabled(); }
  void setReminderEnabled(bool enabled) { backend_.setChatReminderEnabled(enabled); }
  Q_INVOKABLE void send(const QString &message) { backend_.sendChatMessage(message); }
  Q_INVOKABLE void pin(int row) { backend_.pinChatMessage(row); }
  Q_INVOKABLE void clearPin() { backend_.clearPinnedChatMessage(); }

signals:
  void changed();

private:
  Backend &backend_;
};

class NetworkController final : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(int relayPing READ relayPing NOTIFY changed)
  Q_PROPERTY(QVariantList relayPops READ relayPops NOTIFY changed)

public:
  explicit NetworkController(Backend &backend);
  [[nodiscard]] int relayPing() const { return backend_.relayPing(); }
  [[nodiscard]] QVariantList relayPops() const { return backend_.relayPops(); }

signals:
  void changed();

private:
  Backend &backend_;
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
  explicit ApplicationController(Backend &backend);

  [[nodiscard]] SessionController *session() { return &session_; }
  [[nodiscard]] LobbyController *lobbies() { return &lobbies_; }
  [[nodiscard]] SocialController *social() { return &social_; }
  [[nodiscard]] ChatController *chat() { return &chat_; }
  [[nodiscard]] NetworkController *network() { return &network_; }
  [[nodiscard]] UpdaterController *updater() { return &updater_; }
  [[nodiscard]] QString version() const { return backend_.appVersion(); }

  Q_INVOKABLE void copy(const QString &text) {
    backend_.copyToClipboard(text);
    emit copied();
  }
  Q_INVOKABLE void launchSteam(bool china = false) { backend_.launchSteam(china); }

signals:
  void copied();

private:
  Backend &backend_;
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
