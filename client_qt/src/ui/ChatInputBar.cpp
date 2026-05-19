#include "liteim_client/ui/ChatInputBar.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QTextEdit>

namespace liteim::client {

ChatInputBar::ChatInputBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("chatInputBar"));

    input_edit_ = new QTextEdit(this);
    input_edit_->setObjectName(QStringLiteral("chatInputEdit"));
    input_edit_->setPlaceholderText(QStringLiteral("Type a message"));
    input_edit_->setAcceptRichText(false);
    input_edit_->setFixedHeight(92);
    input_edit_->installEventFilter(this);

    send_button_ = new QPushButton(QStringLiteral("Send"), this);
    send_button_->setObjectName(QStringLiteral("chatSendButton"));
    send_button_->setFixedWidth(84);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(input_edit_, 1);
    layout->addWidget(send_button_);

    connect(input_edit_, &QTextEdit::textChanged, this, &ChatInputBar::updateSendButton);
    connect(send_button_, &QPushButton::clicked, this, &ChatInputBar::submit);

    updateSendButton();
}

QString ChatInputBar::text() const {
    return input_edit_->toPlainText().trimmed();
}

void ChatInputBar::clear() {
    input_edit_->clear();
    updateSendButton();
}

bool ChatInputBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == input_edit_ && event->type() == QEvent::KeyPress) {
        auto* key_event = static_cast<QKeyEvent*>(event);
        const bool is_enter =
            key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter;
        if (is_enter && !(key_event->modifiers() & Qt::ShiftModifier)) {
            submit();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatInputBar::submit() {
    const auto message_text = text();
    if (message_text.isEmpty()) {
        updateSendButton();
        return;
    }

    emit sendRequested(message_text);
    clear();
}

void ChatInputBar::updateSendButton() {
    send_button_->setEnabled(!text().isEmpty());
}

}  // namespace liteim::client
