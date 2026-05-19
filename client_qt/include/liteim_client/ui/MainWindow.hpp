#pragma once

#include <QMainWindow>
#include <QString>

// 把三个子控件组合成常见 IM 三栏界面

class QSplitter;  // QSplitter 是 Qt 中的一个控件，用于在窗口中创建可调整大小的分割区域,三栏分割器

namespace liteim::client {

class ChatPage;
class ConversationListWidget;
class SideBar;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    // 负责根据左侧导航切换中间栏和右侧标题
    void switchSection(const QString& section_id);

    QSplitter* splitter_{nullptr};
    SideBar* side_bar_{nullptr};
    ConversationListWidget* conversation_list_{nullptr};
    ChatPage* chat_page_{nullptr};
};

}  // namespace liteim::client
