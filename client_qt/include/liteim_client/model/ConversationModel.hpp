#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include <cstdint>

// Qt 客户端的会话列表数据模型
namespace liteim::client {

enum class ConversationKind {
    Private,
    Group,
};

// 表示一个会话项的数据结构，包含了会话 ID、类型、标题、最后一条消息、时间戳、头像文本和未读数等信息
struct ConversationItem {
    QString conversation_id;
    ConversationKind kind{ConversationKind::Private};
    QString title;
    QString last_message;
    QString timestamp;
    QString avatar_text;
    int unread_count{0};
    std::uint64_t target_id{0};
};

// 表示会话更新的结构体，包含了会话 ID、类型、标题、最后一条消息、时间戳、头像文本等信息
struct ConversationUpdate {
    QString conversation_id;
    ConversationKind kind{ConversationKind::Private};
    QString title;
    QString last_message;
    QString timestamp;
    QString avatar_text;
};

class ConversationModel final : public QAbstractListModel {
    Q_OBJECT

public:
    // 一行数据的不同字段
    enum Role {
        ConversationIdRole = Qt::UserRole + 1,
        KindRole,
        TitleRole,
        LastMessageRole,
        TimestampRole,
        AvatarTextRole,
        UnreadCountRole,
        TargetIdRole,
    };

    explicit ConversationModel(QObject* parent = nullptr);

    // 返回模型有多少行
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    // 返回指定行和role的数据
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    // 把 role 编号映射成名字，在 QML 中可以通过名字访问
    QHash<int, QByteArray> roleNames() const override;
    // 一次性替换整个会话列表
    void setConversations(const QVector<ConversationItem>& conversations);
    // 收到新消息后更新会话列表
    void applyIncomingMessage(const ConversationUpdate& update,
                              const QString& active_conversation_id = QString());
    // 把某个会话未读数清零
    void markConversationRead(const QString& conversation_id);
    // 统计所有会话未读数总和
    int totalUnreadCount() const;

    const QVector<ConversationItem>& conversations() const noexcept;

private:
    int findConversationRow(const QString& conversation_id) const;

    QVector<ConversationItem> conversations_;
};

}  // namespace liteim::client
