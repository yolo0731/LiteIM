#include "liteim_client/ui/ConversationListWidget.hpp"

#include <QAbstractItemView>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace liteim::client {

ConversationListWidget::ConversationListWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("conversationListWidget"));
    // 限制中间栏宽度
    setMinimumWidth(280);
    setMaximumWidth(360);
    // 创建布局
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    // 标题 Label
    title_label_ = new QLabel(this);
    title_label_->setObjectName(QStringLiteral("conversationSectionTitle"));
    // 搜索框
    auto* search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("conversationSearchEdit"));
    search->setPlaceholderText(QStringLiteral("Search"));
    search->setEnabled(false);

    // 列表控件
    list_widget_ = new QListWidget(this);
    list_widget_->setObjectName(QStringLiteral("conversationListItems"));
    list_widget_->setFrameShape(QFrame::NoFrame);
    list_widget_->setSelectionMode(QAbstractItemView::SingleSelection);

    // 添加控件到布局
    layout->addWidget(title_label_);
    layout->addWidget(search);
    layout->addWidget(list_widget_, 1);

    // 默认显示消息列表
    setSection(QStringLiteral("messages"));
}

// 每次切换区域，先清空旧列表
void ConversationListWidget::setSection(const QString& section_id) {
    list_widget_->clear();
    if (section_id == QStringLiteral("contacts")) {
        populateContacts();
    } else if (section_id == QStringLiteral("groups")) {
        populateGroups();
    } else if (section_id == QStringLiteral("settings")) {
        populateSettings();
    } else {
        populateMessages();
    }
}

void ConversationListWidget::populateMessages() {
    title_label_->setText(QStringLiteral("Messages"));
    list_widget_->addItem(QStringLiteral("Messages placeholder: recent private chat"));
    list_widget_->addItem(QStringLiteral("Messages placeholder: recent group chat"));
}

void ConversationListWidget::populateContacts() {
    title_label_->setText(QStringLiteral("Contacts"));
    list_widget_->addItem(QStringLiteral("Contacts placeholder: online friends"));
    list_widget_->addItem(QStringLiteral("Contacts placeholder: offline friends"));
}

void ConversationListWidget::populateGroups() {
    title_label_->setText(QStringLiteral("Groups"));
    list_widget_->addItem(QStringLiteral("Groups placeholder: joined groups"));
    list_widget_->addItem(QStringLiteral("Groups placeholder: owned groups"));
}

void ConversationListWidget::populateSettings() {
    title_label_->setText(QStringLiteral("Settings"));
    list_widget_->addItem(QStringLiteral("Settings placeholder"));
}

}  // namespace liteim::client
