#include "members_model.h"

MembersModel::MembersModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MembersModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
    {
        return 0;
    }
    return static_cast<int>(entries_.size());
}

QVariant MembersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return {};
    }

    const Entry &entry = entries_[static_cast<std::size_t>(index.row())];

    switch (role)
    {
    case Qt::DisplayRole:
    case DisplayNameRole:
        return entry.displayName;
    case SteamIdRole:
        return entry.steamId;
    case PingRole:
        return entry.ping >= 0 ? QVariant(entry.ping) : QVariant();
    case RelayRole:
        return entry.relay;
    default:
        return {};
    }
}

QHash<int, QByteArray> MembersModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[SteamIdRole] = "steamId";
    roles[DisplayNameRole] = "displayName";
    roles[PingRole] = "ping";
    roles[RelayRole] = "relay";
    return roles;
}

void MembersModel::setMembers(std::vector<Entry> entries)
{
    beginResetModel();
    entries_ = std::move(entries);
    endResetModel();
    emit countChanged();
}
