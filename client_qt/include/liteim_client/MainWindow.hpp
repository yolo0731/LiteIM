#pragma once

#include <QMainWindow>
#include <QString>

class QSplitter;

namespace liteim::client {

class ChatPage;
class ConversationListWidget;
class SideBar;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void switchSection(const QString& section_id);

    QSplitter* splitter_{nullptr};
    SideBar* side_bar_{nullptr};
    ConversationListWidget* conversation_list_{nullptr};
    ChatPage* chat_page_{nullptr};
};

}  // namespace liteim::client
