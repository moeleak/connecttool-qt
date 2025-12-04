#include "members_model.h"

MembersModel::MembersModel(QObject *parent) : QAbstractListModel(parent) {}

int MembersModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(entries_.size());
}

QVariant MembersModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }

  const Entry &entry = entries_[static_cast<std::size_t>(index.row())];

  switch (role) {
  case Qt::DisplayRole:
  case DisplayNameRole:
    return entry.displayName;
  case SteamIdRole:
    return entry.steamId;
  case IpRole:
    return entry.ip;
  case AvatarRole:
    return entry.avatar;
  case PingRole:
    return entry.ping >= 0 ? QVariant(entry.ping) : QVariant();
  case RelayRole:
    return entry.relay;
  case IsFriendRole:
    return entry.isFriend;
  default:
    return {};
  }
}

QHash<int, QByteArray> MembersModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[SteamIdRole] = "steamId";
  roles[DisplayNameRole] = "displayName";
  roles[IpRole] = "ip";
  roles[AvatarRole] = "avatar";
  roles[PingRole] = "ping";
  roles[RelayRole] = "relay";
  roles[IsFriendRole] = "isFriend";
  return roles;
}

void MembersModel::setMembers(std::vector<Entry> entries) {
  if (entries.size() != entries_.size()) {
    beginResetModel();
    entries_ = std::move(entries);
    endResetModel();
    emit countChanged();
    return;
  }

  bool changed = false;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].steamId != entries_[i].steamId ||
        entries[i].displayName != entries_[i].displayName ||
        entries[i].avatar != entries_[i].avatar ||
        entries[i].ping != entries_[i].ping ||
        entries[i].relay != entries_[i].relay ||
        entries[i].isFriend != entries_[i].isFriend ||
        entries[i].ip != entries_[i].ip) {
      changed = true;
      break;
    }
  }

  if (!changed) {
    return;
  }

  entries_ = std::move(entries);
  if (!entries_.empty()) {
    emit dataChanged(index(0, 0),
                     index(static_cast<int>(entries_.size()) - 1, 0));
  }
}
