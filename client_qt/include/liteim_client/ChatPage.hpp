#pragma once

#include <QString>
#include <QWidget>

class QLabel;

namespace liteim::client {

class ChatPage final : public QWidget {
    Q_OBJECT

public:
    explicit ChatPage(QWidget* parent = nullptr);

    void setCurrentUser(const QString& nickname, bool online);
    void setActiveSection(const QString& section_title);

private:
    QLabel* current_user_label_{nullptr};
    QLabel* online_status_label_{nullptr};
    QLabel* chat_title_label_{nullptr};
};

}  // namespace liteim::client
