#pragma once

#include "liteim_client/model/ConversationModel.hpp"

#include <QMainWindow>
#include <QString>
#include <QVector>

#include <cstdint>

// 把三个子控件组合成常见 IM 三栏界面

class QSplitter;  // QSplitter 是 Qt 中的一个控件，用于在窗口中创建可调整大小的分割区域,三栏分割器
class QLabel;
class QPushButton;

namespace liteim::client {

class ChatPage;
class ChatController;
class ClientRuntime;
class ConversationListWidget;
class SideBar;
struct ChatMessage;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    explicit MainWindow(ClientRuntime& runtime, QWidget* parent = nullptr);

private:
    void initializeWindow();
    void buildUi();
    void buildStatusBar();
    void connectChatFlow();
    // 负责根据左侧导航切换中间栏和右侧标题
    void switchSection(const QString& section_id);
    void openConversation(const QString& conversation_id,
                          const QString& title,
                          ConversationKind kind,
                          std::uint64_t target_id);
    void requestHistory(const QString& conversation_id, quint64 before_message_id);
    void sendMessage(const QString& conversation_id, const QString& text);
    void handleIncomingMessage(const ChatMessage& message);
    void handleDeliveredMessage(const ChatMessage& message);
    void handleRequestFailed(const QString& message);
    void applyHistoryMessages(const QVector<ChatMessage>& messages);
    void updateConnectionStatus(const QString& status_text, bool online);

    QSplitter* splitter_{nullptr};
    SideBar* side_bar_{nullptr};
    ConversationListWidget* conversation_list_{nullptr};
    ChatPage* chat_page_{nullptr};
    ClientRuntime* runtime_{nullptr};
    ClientRuntime* owned_runtime_{nullptr};
    ChatController* chat_controller_{nullptr};
    QLabel* connection_status_label_{nullptr};
    QPushButton* reconnect_button_{nullptr};
    QString active_conversation_id_;
    QString active_conversation_title_;
    ConversationKind active_conversation_kind_{ConversationKind::Private};
    std::uint64_t active_target_id_{0};
    std::uint64_t active_wire_conversation_id_{0};
};

}  // namespace liteim::client
