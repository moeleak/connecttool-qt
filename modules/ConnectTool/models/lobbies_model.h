#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>
#include <algorithm>
#include <vector>

class LobbiesModel : public QAbstractListModel {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
  Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)

public:
  // Keep the 1.5.x values stable; the ping mode is an additive UI feature.
  enum SortMode { SortByMembers = 0, SortByName = 1, SortByPing = 2 };

  enum Roles {
    LobbyIdRole = Qt::UserRole + 1,
    NameRole,
    HostNameRole,
    HostIdRole,
    MemberCountRole,
    PingRole,
  };

  struct Entry {
    QString lobbyId;
    QString name;
    QString hostName;
    QString hostId;
    int memberCount = 0;
    int ping = -1;
  };

  explicit LobbiesModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setLobbies(std::vector<Entry> list);
  bool setMemberCount(const QString &lobbyId, int count);
  bool adjustMemberCount(const QString &lobbyId, int delta);
  bool removeByHostId(const QString &hostId);
  int count() const { return static_cast<int>(filtered_.size()); }
  QString filter() const { return filter_; }
  int sortMode() const { return sortMode_; }
  void setFilter(const QString &text);
  void setSortMode(int mode);

signals:
  void countChanged();
  void filterChanged();
  void sortModeChanged();

private:
  std::vector<Entry> filterEntries(const std::vector<Entry> &source) const;
  bool matchesFilter(const Entry &entry) const;
  void applyReset(std::vector<Entry> list);

  std::vector<Entry> entries_;
  std::vector<Entry> filtered_;
  QString filter_;
  QString filterLower_;
  int sortMode_ = SortByMembers;
};
