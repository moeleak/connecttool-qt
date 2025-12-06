#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QVariantMap>
#include <optional>
#include <vector>

class ChatModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(bool hasPinned READ hasPinned NOTIFY pinnedChanged)
  Q_PROPERTY(QVariantMap pinnedMessage READ pinnedMessage NOTIFY pinnedChanged)

public:
  enum Roles {
    SteamIdRole = Qt::UserRole + 1,
    DisplayNameRole,
    AvatarRole,
    MessageRole,
    IsSelfRole,
    TimestampRole,
    IsPinnedRole
  };

  struct Entry {
    QString steamId;
    QString displayName;
    QString avatar;
    QString message;
    bool isSelf = false;
    bool pinned = false;
    QDateTime timestamp;
  };

  explicit ChatModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void appendMessage(Entry entry);
  void clear();
  void setPinnedMessage(const Entry &entry);
  void clearPinnedMessage();

  int count() const { return static_cast<int>(entries_.size()); }
  bool hasPinned() const { return pinnedEntry_.has_value(); }
  QVariantMap pinnedMessage() const;

signals:
  void countChanged();
  void pinnedChanged();

private:
  void updatePinnedFlags();

  static bool sameMessage(const Entry &a, const Entry &b);

  std::vector<Entry> entries_;
  std::optional<Entry> pinnedEntry_;
  int maxMessages_ = 200;
};
