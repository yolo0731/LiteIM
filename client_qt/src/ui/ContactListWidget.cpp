#include "liteim_client/ui/ContactListWidget.hpp"

#include <QAbstractItemView>
#include <QFrame>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>

namespace liteim::client {

namespace {
constexpr int kConversationIdRole = Qt::UserRole + 1;
constexpr int kTitleRole = Qt::UserRole + 2;
constexpr int kKindRole = Qt::UserRole + 3;
constexpr int kTargetIdRole = Qt::UserRole + 4;

// 把一条 ContactListItem 转成 QListWidget 能显示的文本
QString itemText(const ContactListItem& item) {
    auto text = item.avatar_text.isEmpty() ? item.title
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
    // 创建垂直布局
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    list_widget_ = new QListWidget(this);
    list_widget_->setObjectName(list_object_name);
    list_widget_->setFrameShape(QFrame::NoFrame);
    list_widget_->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(list_widget_);

    connect(list_widget_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        handleItemClicked(list_widget_->row(item));
    });
}

// 刷新整个列表
void ContactListWidget::setItems(const QVector<ContactListItem>& items) {
    list_widget_->clear();
    for (const auto& item : items) {
        ContactListItem normalized = item;
        normalized.unread_count = std::max(0, normalized.unread_count);
        auto* widget_item = new QListWidgetItem(itemText(normalized), list_widget_);
        widget_item->setData(kConversationIdRole, normalized.conversation_id);
        widget_item->setData(kTitleRole, normalized.title);
        widget_item->setData(kKindRole, static_cast<int>(normalized.kind));
        widget_item->setData(kTargetIdRole, QVariant::fromValue<qulonglong>(normalized.target_id));
    }
}

QListWidget* ContactListWidget::listWidget() const noexcept {
    return list_widget_;
}

void ContactListWidget::handleItemClicked(int row) {
    if (row < 0 || row >= list_widget_->count()) {
        return;
    }

    auto* item = list_widget_->item(row);
    const auto conversation_id = item->data(kConversationIdRole).toString();
    if (conversation_id.isEmpty()) {
        return;
    }

    emit itemActivated(conversation_id,
                       item->data(kTitleRole).toString(),
                       static_cast<ConversationKind>(item->data(kKindRole).toInt()),
                       item->data(kTargetIdRole).toULongLong());
}

}  // namespace liteim::client
