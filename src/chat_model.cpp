#include "chat_model.h"

ChatModel::ChatModel(QObject *parent) : QAbstractListModel(parent) {}

int ChatModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(entries_.size());
}

QVariant ChatModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return {};
  }
  const auto row = static_cast<size_t>(index.row());
  if (row >= entries_.size()) {
    return {};
  }
  const auto &entry = entries_[row];
  switch (role) {
  case SteamIdRole:
    return entry.steamId;
  case DisplayNameRole:
    return entry.displayName;
  case AvatarRole:
    return entry.avatar;
  case MessageRole:
    return entry.message;
  case IsSelfRole:
    return entry.isSelf;
  case TimestampRole:
    return entry.timestamp;
  case IsPinnedRole:
    return entry.pinned;
  default:
    return {};
  }
}

QHash<int, QByteArray> ChatModel::roleNames() const {
  return {{SteamIdRole, "steamId"},       {DisplayNameRole, "displayName"},
          {AvatarRole, "avatar"},         {MessageRole, "message"},
          {IsSelfRole, "isSelf"},         {TimestampRole, "timestamp"},
          {IsPinnedRole, "isPinned"}};
}

void ChatModel::appendMessage(Entry entry) {
  if (pinnedEntry_ && sameMessage(entry, *pinnedEntry_)) {
    entry.pinned = true;
  }
  const int insertRow = static_cast<int>(entries_.size());
  beginInsertRows(QModelIndex(), insertRow, insertRow);
  entries_.push_back(std::move(entry));
  endInsertRows();
  emit countChanged();

  const int overflow = static_cast<int>(entries_.size()) - maxMessages_;
  if (overflow > 0) {
    beginRemoveRows(QModelIndex(), 0, overflow - 1);
    entries_.erase(entries_.begin(), entries_.begin() + overflow);
    endRemoveRows();
    emit countChanged();
  }
  updatePinnedFlags();
}

void ChatModel::clear() {
  const bool hadPinned = pinnedEntry_.has_value();
  pinnedEntry_.reset();
  if (entries_.empty()) {
    if (hadPinned) {
      emit pinnedChanged();
    }
    return;
  }
  beginResetModel();
  entries_.clear();
  endResetModel();
  emit countChanged();
  if (hadPinned) {
    emit pinnedChanged();
  }
}

void ChatModel::setPinnedMessage(const Entry &entry) {
  Entry pinned = entry;
  pinned.pinned = true;
  const bool changed =
      !pinnedEntry_.has_value() || !sameMessage(*pinnedEntry_, pinned);
  pinnedEntry_ = std::move(pinned);
  updatePinnedFlags();
  if (changed) {
    emit pinnedChanged();
  }
}

void ChatModel::clearPinnedMessage() {
  if (!pinnedEntry_) {
    return;
  }
  pinnedEntry_.reset();
  updatePinnedFlags();
  emit pinnedChanged();
}

QVariantMap ChatModel::pinnedMessage() const {
  QVariantMap map;
  if (!pinnedEntry_) {
    return map;
  }
  map.insert(QStringLiteral("steamId"), pinnedEntry_->steamId);
  map.insert(QStringLiteral("displayName"), pinnedEntry_->displayName);
  map.insert(QStringLiteral("avatar"), pinnedEntry_->avatar);
  map.insert(QStringLiteral("message"), pinnedEntry_->message);
  map.insert(QStringLiteral("isSelf"), pinnedEntry_->isSelf);
  map.insert(QStringLiteral("timestamp"), pinnedEntry_->timestamp);
  map.insert(QStringLiteral("isPinned"), true);
  return map;
}

void ChatModel::updatePinnedFlags() {
  if (entries_.empty()) {
    return;
  }
  std::vector<int> changedRows;
  changedRows.reserve(entries_.size());
  for (size_t i = 0; i < entries_.size(); ++i) {
    bool shouldPin =
        pinnedEntry_.has_value() && sameMessage(entries_[i], *pinnedEntry_);
    if (entries_[i].pinned != shouldPin) {
      entries_[i].pinned = shouldPin;
      changedRows.push_back(static_cast<int>(i));
    }
  }
  if (changedRows.empty()) {
    return;
  }

  // Coalesce contiguous ranges for efficiency
  int start = changedRows.front();
  int end = changedRows.front();
  for (size_t idx = 1; idx < changedRows.size(); ++idx) {
    const int row = changedRows[idx];
    if (row == end + 1) {
      end = row;
      continue;
    }
    const QModelIndex top = index(start, 0);
    const QModelIndex bottom = index(end, 0);
    emit dataChanged(top, bottom, {IsPinnedRole});
    start = end = row;
  }
  const QModelIndex top = index(start, 0);
  const QModelIndex bottom = index(end, 0);
  emit dataChanged(top, bottom, {IsPinnedRole});
}

bool ChatModel::sameMessage(const Entry &a, const Entry &b) {
  if (a.steamId != b.steamId || a.message != b.message) {
    return false;
  }
  if (a.timestamp.isValid() && b.timestamp.isValid()) {
    return a.timestamp == b.timestamp;
  }
  return true;
}
