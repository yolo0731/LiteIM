#include "liteim_client/ui/ChatPage.hpp"

#include "liteim_client/ui/ChatInputBar.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace liteim::client {

ChatPage::ChatPage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("chatPage"));

    auto* root = new QVBoxLayout(this);  //整体垂直布局
    root->setContentsMargins(24, 18, 24, 18);
    root->setSpacing(16);

    // 顶部栏是横向布局
    auto* top_bar = new QWidget(this);
    top_bar->setObjectName(QStringLiteral("chatTopBar"));
    auto* top_layout = new QHBoxLayout(top_bar);
    top_layout->setContentsMargins(0, 0, 0, 0);

    // 左侧标题
    chat_title_label_ = new QLabel(QStringLiteral("Messages"), top_bar);
    chat_title_label_->setObjectName(QStringLiteral("chatTitleLabel"));
    // 右侧当前用户昵称
    current_user_label_ = new QLabel(top_bar);
    current_user_label_->setObjectName(QStringLiteral("currentUserNicknameLabel"));
    // 在线状态
    online_status_label_ = new QLabel(top_bar);
    online_status_label_->setObjectName(QStringLiteral("onlineStatusLabel"));

    top_layout->addWidget(chat_title_label_);
    top_layout->addStretch();
    top_layout->addWidget(current_user_label_);
    top_layout->addWidget(online_status_label_);

    message_scroll_area_ = new QScrollArea(this);
    message_scroll_area_->setObjectName(QStringLiteral("chatMessageArea"));
    message_scroll_area_->setWidgetResizable(true);
    message_scroll_area_->setFrameShape(QFrame::NoFrame);
    message_scroll_area_->setMinimumHeight(320);

    messages_container_ = new QWidget(message_scroll_area_);
    messages_container_->setObjectName(QStringLiteral("chatMessagesContainer"));
    messages_layout_ = new QVBoxLayout(messages_container_);
    messages_layout_->setContentsMargins(0, 0, 0, 0);
    messages_layout_->setSpacing(8);

    load_earlier_button_ =
        new QPushButton(QStringLiteral("Load earlier messages"), messages_container_);
    load_earlier_button_->setObjectName(QStringLiteral("loadEarlierMessagesButton"));
    load_earlier_button_->setEnabled(false);
    messages_layout_->addWidget(load_earlier_button_, 0, Qt::AlignHCenter);
    messages_layout_->addStretch();

    message_scroll_area_->setWidget(messages_container_);

    input_bar_ = new ChatInputBar(this);
    input_bar_->setEnabled(false);

    // 把控件加入布局
    root->addWidget(top_bar);
    root->addWidget(message_scroll_area_, 1);
    root->addWidget(input_bar_);

    connect(input_bar_, &ChatInputBar::sendRequested, this, &ChatPage::handleSendRequested);
    connect(load_earlier_button_, &QPushButton::clicked, this, &ChatPage::requestEarlierHistory);
    connect(message_scroll_area_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        auto* scroll_bar = message_scroll_area_->verticalScrollBar();
        if (scroll_bar->maximum() > scroll_bar->minimum() &&
            value == scroll_bar->minimum() &&
            load_earlier_button_->isEnabled()) {
            requestEarlierHistory();
        }
    });

    setCurrentUser(QStringLiteral("LiteIM User"), true);
}

// 设置当前用户显示信息
void ChatPage::setCurrentUser(const QString& nickname, bool online) {
    current_user_label_->setText(nickname.isEmpty() ? QStringLiteral("LiteIM User") : nickname);
    online_status_label_->setText(online ? QStringLiteral("Online") : QStringLiteral("Offline"));
}

// 修改右侧顶部标题
void ChatPage::setActiveSection(const QString& section_title) {
    chat_title_label_->setText(section_title);
}

void ChatPage::openConversation(const QString& conversation_id,
                                const QString& title,
                                ConversationKind kind) {
    active_conversation_id_ = conversation_id;
    active_conversation_kind_ = kind;
    chat_title_label_->setText(title);
    messages_.clear();
    rebuildMessageList();
    input_bar_->setEnabled(!active_conversation_id_.isEmpty());
    load_earlier_button_->setEnabled(!active_conversation_id_.isEmpty());

    if (!active_conversation_id_.isEmpty()) {
        emit historyRequested(active_conversation_id_, 0);
    }
}

void ChatPage::setMessages(const QVector<ChatMessage>& messages) {
    messages_.clear();
    for (const auto& message : messages) {
        if (active_conversation_id_.isEmpty() ||
            message.conversation_id == active_conversation_id_) {
            messages_.push_back(message);
        }
    }
    rebuildMessageList();
    scrollToBottom();
}

void ChatPage::appendMessage(const ChatMessage& message) {
    if (!active_conversation_id_.isEmpty() && message.conversation_id != active_conversation_id_) {
        return;
    }
    messages_.push_back(message);
    auto* bubble = new MessageBubble(message, active_conversation_kind_, messages_container_);
    bubble_widgets_.push_back(bubble);
    messages_layout_->insertWidget(messages_layout_->count() - 1, bubble);
    load_earlier_button_->setEnabled(!active_conversation_id_.isEmpty() && !messages_.empty());
    scrollToBottom();
}

void ChatPage::updateMessageStatus(quint64 message_id, MessageSendStatus status) {
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].message_id == message_id) {
            messages_[i].status = status;
            if (i < bubble_widgets_.size()) {
                bubble_widgets_[i]->setStatus(status);
            }
            return;
        }
    }
}

void ChatPage::handleSendRequested(const QString& text) {
    if (active_conversation_id_.isEmpty()) {
        return;
    }

    ChatMessage message;
    message.conversation_id = active_conversation_id_;
    message.message_id = 0;
    message.sender_name = current_user_label_->text();
    message.text = text;
    message.sent_at = QDateTime::currentDateTime();
    message.direction = MessageDirection::Outgoing;
    message.status = MessageSendStatus::Sending;

    appendMessage(message);
    emit sendMessageRequested(active_conversation_id_, text);
}

void ChatPage::requestEarlierHistory() {
    if (active_conversation_id_.isEmpty() || messages_.empty()) {
        return;
    }
    const auto before_message_id = earliestMessageId();
    if (before_message_id == 0) {
        return;
    }
    emit historyRequested(active_conversation_id_, before_message_id);
}

void ChatPage::rebuildMessageList() {
    for (auto* bubble : bubble_widgets_) {
        messages_layout_->removeWidget(bubble);
        delete bubble;
    }
    bubble_widgets_.clear();

    for (const auto& message : messages_) {
        auto* bubble = new MessageBubble(message, active_conversation_kind_, messages_container_);
        bubble_widgets_.push_back(bubble);
        messages_layout_->insertWidget(messages_layout_->count() - 1, bubble);
    }

    load_earlier_button_->setEnabled(!active_conversation_id_.isEmpty() && !messages_.empty());
}

void ChatPage::scrollToBottom() {
    QTimer::singleShot(0, this, [this] {
        auto* scroll_bar = message_scroll_area_->verticalScrollBar();
        scroll_bar->setValue(scroll_bar->maximum());
    });
}

quint64 ChatPage::earliestMessageId() const noexcept {
    quint64 earliest = 0;
    for (const auto& message : messages_) {
        if (message.message_id == 0) {
            continue;
        }
        if (earliest == 0 || message.message_id < earliest) {
            earliest = message.message_id;
        }
    }
    return earliest;
}

}  // namespace liteim::client
