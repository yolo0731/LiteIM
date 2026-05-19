#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

class QPushButton;

namespace liteim::client {
// 主窗口左侧的侧边栏，包含几个按钮，分别对应不同的功能区域（消息、联系人、群聊、设置）
// SideBar 是一个 Qt 控件，继承自 QWidget
class SideBar final : public QWidget {
    Q_OBJECT

public:
    explicit SideBar(QWidget* parent = nullptr);
    // 设置当前选中的区域
    void setActiveSection(const QString& section_id);

signals:
    // 当用户点击按钮时，SideBar 发出这个信号，告诉外部哪个区域被选中了
    void sectionSelected(QString section_id);

private:
    // 添加一个按钮到侧边栏，参数包括按钮的对象名称、显示标签和对应的区域 ID
    QPushButton* addButton(const QString& object_name, const QString& label,
                           const QString& section_id);

    QHash<QString, QPushButton*> buttons_;
};

}  // namespace liteim::client
