#pragma once

#include <QString>
#include <QWidget>

class QLabel;

// 主窗口右侧的 占位聊天区域
/*顶部：当前区域标题 + 当前用户昵称 + 在线状态
中间：聊天内容占位
底部：禁用的输入框占位*/
namespace liteim::client {

class ChatPage final : public QWidget {
    Q_OBJECT

public:
    explicit ChatPage(QWidget* parent = nullptr);
    // 设置当前用户昵称和在线状态
    void setCurrentUser(const QString& nickname, bool online);
    // 设置当前右侧标题
    void setActiveSection(const QString& section_title);

private:
    QLabel* current_user_label_{nullptr};
    QLabel* online_status_label_{nullptr};
    QLabel* chat_title_label_{nullptr};
};

}  // namespace liteim::client
