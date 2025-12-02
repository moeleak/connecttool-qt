#pragma once

#include <QAbstractListModel>
#include <QString>
#include <vector>
#include <algorithm>

class FriendsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
public:
    enum Roles
    {
        SteamIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        AvatarRole,
        OnlineRole,
        StatusRole,
    };

    struct Entry
    {
        QString steamId;
        QString displayName;
        QString avatarData;
        bool online = false;
        QString status;
        int presenceRank = 0;
    };

    explicit FriendsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setFriends(std::vector<Entry> list);
    int count() const { return static_cast<int>(entries_.size()); }
    QString filter() const { return filter_; }
    void setFilter(const QString &text);

signals:
    void countChanged();
    void filterChanged();

private:
    std::vector<Entry> filterEntries(const std::vector<Entry> &source) const;
    bool matchesFilter(const QString &name) const;
    int scoreFor(const QString &name) const;

    std::vector<Entry> entries_;
    std::vector<Entry> filtered_;
    QString filter_;
    QString filterLower_;
};
