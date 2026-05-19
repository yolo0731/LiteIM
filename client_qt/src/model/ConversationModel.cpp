#include "liteim_client/model/ConversationModel.hpp"

#include <QModelIndex>
#include <QVariant>

#include <algorithm>

namespace liteim::client {

ConversationModel::ConversationModel(QObject* parent) : QAbstractListModel(parent) {}

int ConversationModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return conversations_.size();
}

QVariant ConversationModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= conversations_.size()) {
        return {};
    }

    const auto& item = conversations_.at(index.row());
    switch (role) {
    case Qt::DisplayRole: {
        auto text = item.title;
        if (!item.last_message.isEmpty()) {
            text += QStringLiteral("\n") + item.last_message;
        }
        if (!item.timestamp.isEmpty()) {
            text += QStringLiteral("  ") + item.timestamp;
        }
        if (item.unread_count > 0) {
            text += QStringLiteral("  (") + QString::number(item.unread_count) +
                    QStringLiteral(")");
        }
        return text;
    }
    case ConversationIdRole:
        return item.conversation_id;
    case KindRole:
        return static_cast<int>(item.kind);
    case TitleRole:
        return item.title;
    case LastMessageRole:
        return item.last_message;
    case TimestampRole:
        return item.timestamp;
    case AvatarTextRole:
        return item.avatar_text;
    case UnreadCountRole:
        return item.unread_count;
    default:
        return {};
    }
}

QHash<int, QByteArray> ConversationModel::roleNames() const {
    return {
        {ConversationIdRole, QByteArrayLiteral("conversationId")},
        {KindRole, QByteArrayLiteral("kind")},
        {TitleRole, QByteArrayLiteral("title")},
        {LastMessageRole, QByteArrayLiteral("lastMessage")},
        {TimestampRole, QByteArrayLiteral("timestamp")},
        {AvatarTextRole, QByteArrayLiteral("avatarText")},
        {UnreadCountRole, QByteArrayLiteral("unreadCount")},
    };
}

void ConversationModel::setConversations(const QVector<ConversationItem>& conversations) {
    beginResetModel();
    conversations_ = conversations;
    for (auto& item : conversations_) {
        item.unread_count = std::max(0, item.unread_count);
    }
    endResetModel();
}

void ConversationModel::applyIncomingMessage(const ConversationUpdate& update,
                                             const QString& active_conversation_id) {
    beginResetModel();
    const auto row = findConversationRow(update.conversation_id);
    ConversationItem item;
    if (row >= 0) {
        item = conversations_.takeAt(row);
    } else {
        item.conversation_id = update.conversation_id;
        item.kind = update.kind;
    }

    item.kind = update.kind;
    item.title = update.title;
    item.last_message = update.last_message;
    item.timestamp = update.timestamp;
    item.avatar_text = update.avatar_text;
    if (update.conversation_id != active_conversation_id) {
        ++item.unread_count;
    }

    conversations_.prepend(item);
    endResetModel();
}

void ConversationModel::markConversationRead(const QString& conversation_id) {
    const auto row = findConversationRow(conversation_id);
    if (row < 0 || conversations_[row].unread_count == 0) {
        return;
    }

    conversations_[row].unread_count = 0;
    const auto changed = index(row, 0);
    emit dataChanged(changed, changed, {UnreadCountRole, Qt::DisplayRole});
}

int ConversationModel::totalUnreadCount() const {
    int total = 0;
    for (const auto& item : conversations_) {
        total += std::max(0, item.unread_count);
    }
    return total;
}

const QVector<ConversationItem>& ConversationModel::conversations() const noexcept {
    return conversations_;
}

int ConversationModel::findConversationRow(const QString& conversation_id) const {
    for (int i = 0; i < conversations_.size(); ++i) {
        if (conversations_.at(i).conversation_id == conversation_id) {
            return i;
        }
    }
    return -1;
}

}  // namespace liteim::client
