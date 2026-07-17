#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>
#include <vector>

class MembersModel : public QAbstractListModel {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
  enum Roles {
    SteamIdRole = Qt::UserRole + 1,
    DisplayNameRole,
    AvatarRole,
    PingRole,
    RelayRole,
    IsFriendRole,
    IsSelfRole,
    IpRole
  };

  struct Entry {
    QString steamId;
    QString displayName;
    QString avatar;
    int ping = -1;
    QString relay;
    bool isFriend = false;
    bool isSelf = false;
    QString ip;
  };

  explicit MembersModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setMembers(std::vector<Entry> entries);
  int count() const { return static_cast<int>(entries_.size()); }

signals:
  void countChanged();

private:
  std::vector<Entry> entries_;
};
