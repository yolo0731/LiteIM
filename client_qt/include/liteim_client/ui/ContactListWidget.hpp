#pragma once

#include "liteim_client/model/ConversationModel.hpp"

#include <QString>
#include <QVector>
#include <QWidget>

#include <cstdint>
#include <utility>

class QListWidget;  // 显示一条一条的列表项

// 只负责“简单列表怎么显示”, 联系人/列表显示控件
namespace liteim::client {

// 联系人列表项
struct ContactListItem {
    ContactListItem() = default;
    ContactListItem(QString title,
                    QString subtitle,
                    QString avatar_text,
                    bool online,
                    int unread_count,
                    std::uint64_t target_id = 0,
                    QString conversation_id = {},
                    ConversationKind kind = ConversationKind::Private)
        : title(std::move(title)),
          subtitle(std::move(subtitle)),
          avatar_text(std::move(avatar_text)),
          online(online),
          unread_count(unread_count),
          target_id(target_id),
          conversation_id(std::move(conversation_id)),
          kind(kind) {}

    QString title;
    QString subtitle;
    QString avatar_text;
    bool online{false};
    int unread_count{0};
    std::uint64_t target_id{0};
    QString conversation_id;
    ConversationKind kind{ConversationKind::Private};
};

class ContactListWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ContactListWidget(QWidget* parent = nullptr);
    ContactListWidget(const QString& list_object_name, QWidget* parent);

    void setItems(const QVector<ContactListItem>& items);
    QListWidget* listWidget() const noexcept;

signals:
    void itemActivated(QString conversation_id,
                       QString title,
                       liteim::client::ConversationKind kind,
                       quint64 target_id);

private:
    void handleItemClicked(int row);

    QListWidget* list_widget_{nullptr};
};

}  // namespace liteim::client
