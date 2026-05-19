#pragma once

#include "liteim_client/model/ConversationModel.hpp"
#include "liteim_client/ui/ContactListWidget.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QListView;
class QListWidget;
class QStackedWidget;

// UI 实现 + model 连接层,也是中间栏总容器
namespace liteim::client {

class ConversationListWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ConversationListWidget(QWidget* parent = nullptr);
    // 根据左侧导航切换中间栏显示内容
    void setSection(const QString& section_id);
    // 访问会话数据模型，外部可以直接操作模型来更新会话列表
    ConversationModel* conversationModel() noexcept;
    // 收到新消息时更新会话列表
    void applyIncomingMessage(const ConversationUpdate& update,
                              const QString& active_conversation_id = QString());
    //   把某个会话未读数清零
    void markConversationRead(const QString& conversation_id);

signals:
    void conversationActivated(QString conversation_id,
                               QString title,
                               liteim::client::ConversationKind kind,
                               quint64 target_id);

private:
    void seedDemoData();
    void handleConversationClicked(const QModelIndex& index);

    QLabel* title_label_{nullptr};
    QStackedWidget* stack_{nullptr};
    QListView* conversation_view_{nullptr};
    ContactListWidget* contact_list_{nullptr};
    ContactListWidget* group_list_{nullptr};
    QListWidget* settings_list_{nullptr};
    ConversationModel conversation_model_;
};

}  // namespace liteim::client
