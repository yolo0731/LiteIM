#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
// 主窗口中间那一栏
// 根据左侧 SideBar 选中的区域，显示不同标题和不同占位列表
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
    void populateSettings();

    QLabel* title_label_{nullptr};
    QListWidget* list_widget_{nullptr};
};

}  // namespace liteim::client
