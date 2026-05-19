#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QListWidget;

namespace liteim::client {

struct ContactListItem {
    QString title;
    QString subtitle;
    QString avatar_text;
    bool online{false};
    int unread_count{0};
};

class ContactListWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ContactListWidget(QWidget* parent = nullptr);
    ContactListWidget(const QString& list_object_name, QWidget* parent);

    void setItems(const QVector<ContactListItem>& items);
    QListWidget* listWidget() const noexcept;

private:
    QListWidget* list_widget_{nullptr};
};

}  // namespace liteim::client
