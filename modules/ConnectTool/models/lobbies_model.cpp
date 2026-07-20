#include "lobbies_model.h"

LobbiesModel::LobbiesModel(QObject *parent) : QAbstractListModel(parent) {}

int LobbiesModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(filtered_.size());
}

QVariant LobbiesModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount(index.parent())) {
    return {};
  }

  const auto &entry = filtered_[static_cast<std::size_t>(index.row())];
  switch (role) {
  case LobbyIdRole:
    return entry.lobbyId;
  case NameRole:
    return entry.name;
  case HostNameRole:
    return entry.hostName;
  case HostIdRole:
    return entry.hostId;
  case MemberCountRole:
    return entry.memberCount;
  case PingRole:
    return entry.ping;
  default:
    return {};
  }
}

QHash<int, QByteArray> LobbiesModel::roleNames() const {
  return {{LobbyIdRole, "lobbyId"}, {NameRole, "name"},           {HostNameRole, "hostName"},
          {HostIdRole, "hostId"},   {MemberCountRole, "members"}, {PingRole, "ping"}};
}

void LobbiesModel::setLobbies(std::vector<Entry> list) { applyReset(std::move(list)); }

bool LobbiesModel::removeByHostId(const QString &hostId) {
  if (hostId.isEmpty()) {
    return false;
  }
  std::vector<Entry> next = entries_;
  next.erase(std::remove_if(next.begin(), next.end(),
                            [&hostId](const Entry &entry) { return entry.hostId == hostId; }),
             next.end());
  if (next.size() == entries_.size()) {
    return false;
  }
  applyReset(std::move(next));
  return true;
}

bool LobbiesModel::setMemberCount(const QString &lobbyId, int count) {
  if (count < 0) {
    return false;
  }
  const auto entry = std::ranges::find(entries_, lobbyId, &Entry::lobbyId);
  if (entry == entries_.end() || entry->memberCount == count) {
    return false;
  }

  beginResetModel();
  entry->memberCount = count;
  filtered_ = filterEntries(entries_);
  endResetModel();
  return true;
}

bool LobbiesModel::adjustMemberCount(const QString &lobbyId, int delta) {
  if (delta == 0) {
    return false;
  }
  const auto entry = std::ranges::find(entries_, lobbyId, &Entry::lobbyId);
  if (entry == entries_.end()) {
    return false;
  }
  const int next = std::max(0, entry->memberCount + delta);
  return setMemberCount(lobbyId, next);
}

void LobbiesModel::setFilter(const QString &text) {
  if (filter_ == text) {
    return;
  }
  filter_ = text;
  filterLower_ = filter_.toLower();
  auto next = filterEntries(entries_);
  const bool sizeChanged = next.size() != filtered_.size();

  beginResetModel();
  filtered_ = std::move(next);
  endResetModel();
  emit filterChanged();
  if (sizeChanged) {
    emit countChanged();
  }
}

void LobbiesModel::setSortMode(int mode) {
  if (mode != SortByMembers && mode != SortByName && mode != SortByPing) {
    mode = SortByMembers;
  }
  if (mode == sortMode_) {
    return;
  }

  beginResetModel();
  sortMode_ = mode;
  filtered_ = filterEntries(entries_);
  endResetModel();
  emit sortModeChanged();
}

std::vector<LobbiesModel::Entry>
LobbiesModel::filterEntries(const std::vector<Entry> &source) const {
  std::vector<Entry> result;
  result.reserve(source.size());
  for (const auto &entry : source) {
    if (matchesFilter(entry)) {
      result.push_back(entry);
    }
  }
  std::stable_sort(result.begin(), result.end(), [this](const Entry &a, const Entry &b) {
    if (sortMode_ == SortByName) {
      const int cmp = a.name.toLower().compare(b.name.toLower());
      if (cmp != 0)
        return cmp < 0;
      return a.memberCount > b.memberCount;
    }
    if (sortMode_ == SortByPing) {
      if ((a.ping < 0) != (b.ping < 0)) {
        return a.ping >= 0;
      }
      if (a.ping != b.ping) {
        return a.ping < b.ping;
      }
    }
    if (a.memberCount != b.memberCount) {
      return a.memberCount > b.memberCount;
    }
    return a.name.toLower() < b.name.toLower();
  });
  return result;
}

bool LobbiesModel::matchesFilter(const Entry &entry) const {
  if (filterLower_.isEmpty()) {
    return true;
  }
  const auto contains = [&](const QString &value) {
    return value.toLower().contains(filterLower_);
  };
  return contains(entry.name) || contains(entry.hostName) || contains(entry.lobbyId);
}

void LobbiesModel::applyReset(std::vector<Entry> list) {
  const auto previousCount = filtered_.size();
  beginResetModel();
  entries_ = std::move(list);
  filtered_ = filterEntries(entries_);
  endResetModel();
  if (previousCount != filtered_.size()) {
    emit countChanged();
  }
}
