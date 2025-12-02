#include "friends_model.h"

FriendsModel::FriendsModel(QObject *parent) : QAbstractListModel(parent) {}

int FriendsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
    {
        return 0;
    }
    return static_cast<int>(filtered_.size());
}

QVariant FriendsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
    {
        return {};
    }
    const auto row = static_cast<size_t>(index.row());
    if (row >= filtered_.size())
    {
        return {};
    }
    const auto &entry = filtered_[row];
    switch (role)
    {
    case SteamIdRole:
        return entry.steamId;
    case DisplayNameRole:
        return entry.displayName;
    case AvatarRole:
        return entry.avatarData;
    case OnlineRole:
        return entry.online;
    case StatusRole:
        return entry.status;
    default:
        return {};
    }
}

QHash<int, QByteArray> FriendsModel::roleNames() const
{
    return {
        {SteamIdRole, "steamId"},
        {DisplayNameRole, "displayName"},
        {AvatarRole, "avatar"},
        {OnlineRole, "online"},
        {StatusRole, "status"},
    };
}

void FriendsModel::setFriends(std::vector<Entry> list)
{
    auto filtered = filterEntries(list);
    const bool entryCountChanged = list.size() != entries_.size();
    const bool viewSizeChanged = filtered.size() != filtered_.size();

    if (entryCountChanged || viewSizeChanged)
    {
        beginResetModel();
        entries_ = std::move(list);
        filtered_ = std::move(filtered);
        endResetModel();
        if (entryCountChanged)
        {
            emit countChanged();
        }
    }
    else
    {
        entries_ = std::move(list);
        filtered_ = std::move(filtered);
        if (!filtered_.empty())
        {
            emit dataChanged(index(0, 0), index(static_cast<int>(filtered_.size()) - 1, 0));
        }
    }
}

void FriendsModel::setFilter(const QString &text)
{
    if (filter_ == text)
    {
        return;
    }
    filter_ = text;
    filterLower_ = filter_.toLower();
    emit filterChanged();
    auto filtered = filterEntries(entries_);
    const bool viewSizeChanged = filtered.size() != filtered_.size();
    if (viewSizeChanged)
    {
        beginResetModel();
        filtered_ = std::move(filtered);
        endResetModel();
    }
    else
    {
        filtered_ = std::move(filtered);
        if (!filtered_.empty())
        {
            emit dataChanged(index(0, 0), index(static_cast<int>(filtered_.size()) - 1, 0));
        }
    }
}

std::vector<FriendsModel::Entry> FriendsModel::filterEntries(const std::vector<Entry> &source) const
{
    std::vector<Entry> result;
    result.reserve(source.size());
    for (const auto &entry : source)
    {
        if (matchesFilter(entry.displayName))
        {
            result.push_back(entry);
        }
    }
    std::stable_sort(result.begin(), result.end(), [this](const Entry &a, const Entry &b) {
        if (a.presenceRank != b.presenceRank)
        {
            return a.presenceRank < b.presenceRank;
        }
        const int sa = scoreFor(a.displayName);
        const int sb = scoreFor(b.displayName);
        if (sa != sb)
            return sa < sb;
        return a.displayName.toLower() < b.displayName.toLower();
    });
    return result;
}

bool FriendsModel::matchesFilter(const QString &name) const
{
    if (filterLower_.isEmpty())
    {
        return true;
    }
    const QString lower = name.toLower();
    return lower.contains(filterLower_);
}

int FriendsModel::scoreFor(const QString &name) const
{
    if (filterLower_.isEmpty())
    {
        return 0;
    }
    const QString key = filterLower_;
    const QString lower = name.toLower();
    if (lower.startsWith(key))
        return 0;
    const int idx = lower.indexOf(key);
    if (idx >= 0)
        return 1;
    return 2;
}
