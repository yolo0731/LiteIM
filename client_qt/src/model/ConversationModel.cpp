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
            text +=
                QStringLiteral("  (") + QString::number(item.unread_count) + QStringLiteral(")");
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
    case TargetIdRole:
        return QVariant::fromValue<qulonglong>(item.target_id);
    default:
        return {};
    }
}
// 把 role 编号映射成名字,返回QHash
QHash<int, QByteArray> ConversationModel::roleNames() const {
    return {
        {ConversationIdRole, QByteArrayLiteral("conversationId")},
        {KindRole, QByteArrayLiteral("kind")},
        {TitleRole, QByteArrayLiteral("title")},
        {LastMessageRole, QByteArrayLiteral("lastMessage")},
        {TimestampRole, QByteArrayLiteral("timestamp")},
        {AvatarTextRole, QByteArrayLiteral("avatarText")},
        {UnreadCountRole, QByteArrayLiteral("unreadCount")},
        {TargetIdRole, QByteArrayLiteral("targetId")},
    };
}

void ConversationModel::setConversations(const QVector<ConversationItem>& conversations) {
    beginResetModel();               // 模型要整体重置了
    conversations_ = conversations;  // 替换整个会话列表
    // 防止未读数为负数
    for (auto& item : conversations_) {
        item.unread_count = std::max(0, item.unread_count);
    }
    endResetModel();  // 模型重置完成了，视图要重新获取数据了
}

void ConversationModel::applyIncomingMessage(const ConversationUpdate& update,
                                             const QString& active_conversation_id) {
    beginResetModel();
    const auto row =
        findConversationRow(update.conversation_id);  // 找这个 conversation_id 是否已存在
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

    conversations_.prepend(item);  // 把这个会话项放到最前面
    endResetModel();
}

void ConversationModel::markConversationRead(const QString& conversation_id) {
    const auto row = findConversationRow(conversation_id);
    if (row < 0 || conversations_[row].unread_count == 0) {
        return;
    }

    conversations_[row].unread_count = 0;
    const auto changed = index(row, 0);
    // 通知QT这一行数据变了
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

// 遍历 conversations_，找到指定会话所在行
int ConversationModel::findConversationRow(const QString& conversation_id) const {
    for (int i = 0; i < conversations_.size(); ++i) {
        if (conversations_.at(i).conversation_id == conversation_id) {
            return i;
        }
    }
    return -1;
}

}  // namespace liteim::client
