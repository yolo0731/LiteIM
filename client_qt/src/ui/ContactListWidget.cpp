#include "liteim_client/ui/ContactListWidget.hpp"

#include <QAbstractItemView>
#include <QFrame>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace liteim::client {

namespace {

QString itemText(const ContactListItem& item) {
    auto text = item.avatar_text.isEmpty()
                    ? item.title
                    : item.avatar_text + QStringLiteral("  ") + item.title;
    if (!item.subtitle.isEmpty()) {
        text += QStringLiteral("\n") + item.subtitle;
    }
    if (item.unread_count > 0) {
        text += QStringLiteral("  (") + QString::number(item.unread_count) + QStringLiteral(")");
    }
    return text;
}

}  // namespace

ContactListWidget::ContactListWidget(QWidget* parent)
    : ContactListWidget(QStringLiteral("contactListItems"), parent) {}

ContactListWidget::ContactListWidget(const QString& list_object_name, QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    list_widget_ = new QListWidget(this);
    list_widget_->setObjectName(list_object_name);
    list_widget_->setFrameShape(QFrame::NoFrame);
    list_widget_->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(list_widget_);
}

void ContactListWidget::setItems(const QVector<ContactListItem>& items) {
    list_widget_->clear();
    for (const auto& item : items) {
        ContactListItem normalized = item;
        normalized.unread_count = std::max(0, normalized.unread_count);
        list_widget_->addItem(itemText(normalized));
    }
}

QListWidget* ContactListWidget::listWidget() const noexcept {
    return list_widget_;
}

}  // namespace liteim::client
