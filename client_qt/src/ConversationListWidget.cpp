#include "liteim_client/ConversationListWidget.hpp"

#include <QAbstractItemView>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace liteim::client {

ConversationListWidget::ConversationListWidget(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("conversationListWidget"));
    setMinimumWidth(280);
    setMaximumWidth(360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    title_label_ = new QLabel(this);
    title_label_->setObjectName(QStringLiteral("conversationSectionTitle"));

    auto* search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("conversationSearchEdit"));
    search->setPlaceholderText(QStringLiteral("Search"));
    search->setEnabled(false);

    list_widget_ = new QListWidget(this);
    list_widget_->setObjectName(QStringLiteral("conversationListItems"));
    list_widget_->setFrameShape(QFrame::NoFrame);
    list_widget_->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(title_label_);
    layout->addWidget(search);
    layout->addWidget(list_widget_, 1);

    setSection(QStringLiteral("messages"));
}

void ConversationListWidget::setSection(const QString& section_id) {
    list_widget_->clear();
    if (section_id == QStringLiteral("contacts")) {
        populateContacts();
    } else if (section_id == QStringLiteral("groups")) {
        populateGroups();
    } else if (section_id == QStringLiteral("agent")) {
        populateAgent();
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

void ConversationListWidget::populateAgent() {
    title_label_->setText(QStringLiteral("Agent"));
    list_widget_->addItem(QStringLiteral("Agent contact placeholder"));
}

void ConversationListWidget::populateSettings() {
    title_label_->setText(QStringLiteral("Settings"));
    list_widget_->addItem(QStringLiteral("Settings placeholder"));
}

}  // namespace liteim::client
