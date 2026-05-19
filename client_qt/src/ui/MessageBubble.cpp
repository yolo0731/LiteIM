#include "liteim_client/ui/MessageBubble.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

namespace liteim::client {

namespace {

QString directionName(MessageDirection direction) {
    return direction == MessageDirection::Outgoing ? QStringLiteral("outgoing")
                                                   : QStringLiteral("incoming");
}

QString statusName(MessageSendStatus status) {
    switch (status) {
        case MessageSendStatus::Sending:
            return QStringLiteral("sending");
        case MessageSendStatus::Succeeded:
            return QStringLiteral("succeeded");
        case MessageSendStatus::Failed:
            return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

QString statusText(MessageSendStatus status) {
    switch (status) {
        case MessageSendStatus::Sending:
            return QStringLiteral("Sending");
        case MessageSendStatus::Succeeded:
            return QStringLiteral("Sent");
        case MessageSendStatus::Failed:
            return QStringLiteral("Failed");
    }
    return QStringLiteral("Failed");
}

}  // namespace

MessageBubble::MessageBubble(const ChatMessage& message,
                             ConversationKind conversation_kind,
                             QWidget* parent)
    : QWidget(parent), message_(message), conversation_kind_(conversation_kind) {
    setObjectName(QStringLiteral("messageBubble"));

    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 4, 0, 4);
    outer->setSpacing(0);

    bubble_body_ = new QWidget(this);
    bubble_body_->setObjectName(QStringLiteral("messageBubbleBody"));
    bubble_body_->setMaximumWidth(520);

    auto* body_layout = new QVBoxLayout(bubble_body_);
    body_layout->setContentsMargins(12, 8, 12, 8);
    body_layout->setSpacing(5);

    sender_label_ = new QLabel(bubble_body_);
    sender_label_->setObjectName(QStringLiteral("messageBubbleSender"));

    text_label_ = new QLabel(bubble_body_);
    text_label_->setObjectName(QStringLiteral("messageBubbleText"));
    text_label_->setWordWrap(true);
    text_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* meta_row = new QWidget(bubble_body_);
    auto* meta_layout = new QHBoxLayout(meta_row);
    meta_layout->setContentsMargins(0, 0, 0, 0);
    meta_layout->setSpacing(8);

    time_label_ = new QLabel(meta_row);
    time_label_->setObjectName(QStringLiteral("messageBubbleTime"));

    status_label_ = new QLabel(meta_row);
    status_label_->setObjectName(QStringLiteral("messageBubbleStatus"));

    meta_layout->addWidget(time_label_);
    meta_layout->addStretch();
    meta_layout->addWidget(status_label_);

    body_layout->addWidget(sender_label_);
    body_layout->addWidget(text_label_);
    body_layout->addWidget(meta_row);

    if (message_.direction == MessageDirection::Outgoing) {
        outer->addStretch();
        outer->addWidget(bubble_body_);
    } else {
        outer->addWidget(bubble_body_);
        outer->addStretch();
    }

    applyMessage();
}

const ChatMessage& MessageBubble::message() const noexcept {
    return message_;
}

void MessageBubble::setStatus(MessageSendStatus status) {
    message_.status = status;
    applyMessage();
}

void MessageBubble::applyMessage() {
    const auto direction = directionName(message_.direction);
    const auto status = statusName(message_.status);
    setProperty("messageDirection", direction);
    setProperty("messageStatus", status);
    bubble_body_->setProperty("messageDirection", direction);
    bubble_body_->setProperty("messageStatus", status);

    const bool show_sender = conversation_kind_ == ConversationKind::Group &&
                             message_.direction == MessageDirection::Incoming &&
                             !message_.sender_name.isEmpty();
    sender_label_->setVisible(show_sender);
    sender_label_->setText(show_sender ? message_.sender_name : QString());
    text_label_->setText(message_.text);
    time_label_->setText(message_.sent_at.isValid()
                             ? message_.sent_at.toString(QStringLiteral("HH:mm"))
                             : QString());

    const bool show_status = message_.direction == MessageDirection::Outgoing;
    status_label_->setVisible(show_status);
    status_label_->setText(show_status ? statusText(message_.status) : QString());

    style()->unpolish(bubble_body_);
    style()->polish(bubble_body_);
}

}  // namespace liteim::client
