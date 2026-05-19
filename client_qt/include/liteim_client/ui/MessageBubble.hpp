#pragma once

#include "liteim_client/model/ConversationModel.hpp"

#include <QDateTime>
#include <QString>
#include <QWidget>

class QLabel;
class QWidget;

namespace liteim::client {

enum class MessageDirection {
    Incoming,
    Outgoing,
};

enum class MessageSendStatus {
    Sending,
    Succeeded,
    Failed,
};

struct ChatMessage {
    QString conversation_id;
    quint64 message_id{0};
    quint64 sender_id{0};
    QString sender_name;
    QString text;
    QDateTime sent_at;
    MessageDirection direction{MessageDirection::Incoming};
    MessageSendStatus status{MessageSendStatus::Succeeded};
};

class MessageBubble final : public QWidget {
    Q_OBJECT

public:
    explicit MessageBubble(const ChatMessage& message,
                           ConversationKind conversation_kind,
                           QWidget* parent = nullptr);

    const ChatMessage& message() const noexcept;
    void setStatus(MessageSendStatus status);

private:
    void applyMessage();

    ChatMessage message_;
    ConversationKind conversation_kind_{ConversationKind::Private};
    QWidget* bubble_body_{nullptr};
    QLabel* sender_label_{nullptr};
    QLabel* text_label_{nullptr};
    QLabel* time_label_{nullptr};
    QLabel* status_label_{nullptr};
};

}  // namespace liteim::client
