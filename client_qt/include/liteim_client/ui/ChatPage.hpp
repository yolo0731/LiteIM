#pragma once

#include "liteim_client/model/ConversationModel.hpp"
#include "liteim_client/ui/MessageBubble.hpp"

#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace liteim::client {

class ChatInputBar;

class ChatPage final : public QWidget {
    Q_OBJECT

public:
    explicit ChatPage(QWidget* parent = nullptr);

    void setCurrentUser(const QString& nickname, bool online);
    void setActiveSection(const QString& section_title);
    void openConversation(const QString& conversation_id,
                          const QString& title,
                          ConversationKind kind);
    void setMessages(const QVector<ChatMessage>& messages);
    void appendMessage(const ChatMessage& message);
    void updateMessageStatus(quint64 message_id, MessageSendStatus status);

signals:
    void historyRequested(QString conversation_id, quint64 before_message_id);
    void sendMessageRequested(QString conversation_id, QString text);

private:
    void handleSendRequested(const QString& text);
    void requestEarlierHistory();
    void rebuildMessageList();
    void scrollToBottom();
    quint64 earliestMessageId() const noexcept;

    QLabel* current_user_label_{nullptr};
    QLabel* online_status_label_{nullptr};
    QLabel* chat_title_label_{nullptr};
    QScrollArea* message_scroll_area_{nullptr};
    QWidget* messages_container_{nullptr};
    QVBoxLayout* messages_layout_{nullptr};
    QPushButton* load_earlier_button_{nullptr};
    ChatInputBar* input_bar_{nullptr};
    QVector<MessageBubble*> bubble_widgets_;

    QString active_conversation_id_;
    ConversationKind active_conversation_kind_{ConversationKind::Private};
    QVector<ChatMessage> messages_;
};

}  // namespace liteim::client
