#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;

namespace liteim::client {

class ConversationListWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ConversationListWidget(QWidget* parent = nullptr);

    void setSection(const QString& section_id);

private:
    void populateMessages();
    void populateContacts();
    void populateGroups();
    void populateAgent();
    void populateSettings();

    QLabel* title_label_{nullptr};
    QListWidget* list_widget_{nullptr};
};

}  // namespace liteim::client
