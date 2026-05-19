#include "liteim_client/ui/ChatPage.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
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

    // 聊天内容占位区
    auto* message_area =
        new QLabel(QStringLiteral("Select a conversation to start chatting."), this);
    message_area->setObjectName(QStringLiteral("chatMessageAreaPlaceholder"));
    message_area->setAlignment(Qt::AlignCenter);
    message_area->setMinimumHeight(320);

    auto* input = new QTextEdit(this);
    input->setObjectName(QStringLiteral("chatInputEdit"));
    input->setPlaceholderText(QStringLiteral("Message input starts in a later step."));
    input->setEnabled(false);
    input->setFixedHeight(96);

    // 把控件加入布局
    root->addWidget(top_bar);
    root->addWidget(message_area, 1);
    root->addWidget(input);

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

}  // namespace liteim::client
