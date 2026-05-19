#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

namespace liteim::client {

enum class ConversationKind {
    Private,
    Group,
};

struct ConversationItem {
    QString conversation_id;
    ConversationKind kind{ConversationKind::Private};
    QString title;
    QString last_message;
    QString timestamp;
    QString avatar_text;
    int unread_count{0};
};

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
    enum Role {
        ConversationIdRole = Qt::UserRole + 1,
        KindRole,
        TitleRole,
        LastMessageRole,
        TimestampRole,
        AvatarTextRole,
        UnreadCountRole,
    };

    explicit ConversationModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setConversations(const QVector<ConversationItem>& conversations);
    void applyIncomingMessage(const ConversationUpdate& update,
                              const QString& active_conversation_id = QString());
    void markConversationRead(const QString& conversation_id);
    int totalUnreadCount() const;
    const QVector<ConversationItem>& conversations() const noexcept;

private:
    int findConversationRow(const QString& conversation_id) const;

    QVector<ConversationItem> conversations_;
};

}  // namespace liteim::client
