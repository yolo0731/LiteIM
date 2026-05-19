#pragma once

#include "liteim_client/model/ConversationModel.hpp"
#include "liteim_client/ui/ContactListWidget.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QListView;
class QListWidget;
class QStackedWidget;

namespace liteim::client {

class ConversationListWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ConversationListWidget(QWidget* parent = nullptr);

    void setSection(const QString& section_id);
    ConversationModel* conversationModel() noexcept;
    void applyIncomingMessage(const ConversationUpdate& update,
                              const QString& active_conversation_id = QString());
    void markConversationRead(const QString& conversation_id);

private:
    void seedDemoData();

    QLabel* title_label_{nullptr};
    QStackedWidget* stack_{nullptr};
    QListView* conversation_view_{nullptr};
    ContactListWidget* contact_list_{nullptr};
    ContactListWidget* group_list_{nullptr};
    QListWidget* settings_list_{nullptr};
    ConversationModel conversation_model_;
};

}  // namespace liteim::client
