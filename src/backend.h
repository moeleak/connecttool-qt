#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <boost/asio.hpp>
#include <memory>
#include <thread>
#include <unordered_map>

#include "friends_model.h"
#include "members_model.h"

class SteamNetworkingManager;
class SteamRoomManager;
class TCPServer;

class Backend : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool steamReady READ steamReady NOTIFY stateChanged)
  Q_PROPERTY(bool isHost READ isHost NOTIFY stateChanged)
  Q_PROPERTY(bool isConnected READ isConnected NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  Q_PROPERTY(QString lobbyId READ lobbyId NOTIFY stateChanged)
  Q_PROPERTY(QString hostSteamId READ hostSteamId NOTIFY hostSteamIdChanged)
  Q_PROPERTY(QString joinTarget READ joinTarget WRITE setJoinTarget NOTIFY
                 joinTargetChanged)
  Q_PROPERTY(int tcpClients READ tcpClients NOTIFY serverChanged)
  Q_PROPERTY(
      int localPort READ localPort WRITE setLocalPort NOTIFY localPortChanged)
  Q_PROPERTY(int localBindPort READ localBindPort WRITE setLocalBindPort NOTIFY
                 localBindPortChanged)
  Q_PROPERTY(QVariantList friends READ friends NOTIFY friendsChanged)
  Q_PROPERTY(FriendsModel *friendsModel READ friendsModel NOTIFY friendsChanged)
  Q_PROPERTY(QString friendFilter READ friendFilter WRITE setFriendFilter NOTIFY
                 friendFilterChanged)
  Q_PROPERTY(MembersModel *membersModel READ membersModel CONSTANT)
  Q_PROPERTY(int inviteCooldown READ inviteCooldown NOTIFY inviteCooldownChanged)

public:
  explicit Backend(QObject *parent = nullptr);
  ~Backend();

  bool steamReady() const { return steamReady_; }
  bool isHost() const;
  bool isConnected() const;
  QString status() const { return status_; }
  QString lobbyId() const;
  QString hostSteamId() const { return hostSteamId_; }
  QString joinTarget() const { return joinTarget_; }
  int tcpClients() const;
  int localPort() const { return localPort_; }
  int localBindPort() const { return localBindPort_; }
  QVariantList friends() const { return friends_; }
  FriendsModel *friendsModel() { return &friendsModel_; }
  MembersModel *membersModel() { return &membersModel_; }
  QString friendFilter() const { return friendFilter_; }
  int inviteCooldown() const { return inviteCooldownSeconds_; }

  void setJoinTarget(const QString &id);
  void setLocalPort(int port);
  void setLocalBindPort(int port);
  void setFriendFilter(const QString &text);

  Q_INVOKABLE void startHosting();
  Q_INVOKABLE void joinHost();
  Q_INVOKABLE void disconnect();
  Q_INVOKABLE void refreshFriends();
  Q_INVOKABLE void refreshMembers();
  Q_INVOKABLE void inviteFriend(const QString &steamId);
  Q_INVOKABLE void copyToClipboard(const QString &text);

signals:
  void stateChanged();
  void joinTargetChanged();
  void localPortChanged();
  void localBindPortChanged();
  void friendsChanged();
  void serverChanged();
  void errorMessage(const QString &message);
  void friendFilterChanged();
  void inviteCooldownChanged();
  void hostSteamIdChanged();

private:
  void tick();
  void updateStatus();
  void updateMembersList();
  void updateFriendsList();
  void ensureServerRunning();
  bool ensureSteamReady(const QString &actionLabel);
  void refreshHostId();

  std::unique_ptr<SteamNetworkingManager> steamManager_;
  std::unique_ptr<SteamRoomManager> roomManager_;
  std::unique_ptr<TCPServer> server_;
  boost::asio::io_context ioContext_;
  std::unique_ptr<
      boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
      workGuard_;
  std::thread ioThread_;
  QTimer callbackTimer_;
  QTimer slowTimer_;
  QTimer cooldownTimer_;

  bool steamReady_;
  QString status_;
  QString joinTarget_;
  QString hostSteamId_;
  int localPort_;
  int localBindPort_;
  int lastTcpClients_;
  int lastMemberLogCount_;
  QVariantList friends_;
  FriendsModel friendsModel_;
  MembersModel membersModel_;
  QString friendFilter_;
  std::unordered_map<uint64_t, QString> memberAvatars_;
  int inviteCooldownSeconds_ = 0;
};
