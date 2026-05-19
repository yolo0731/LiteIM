#include "liteim_client/ui/SideBar.hpp"

#include <QPushButton>
#include <QVBoxLayout>

namespace liteim::client {

SideBar::SideBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sideBar"));
    setFixedWidth(88);  //左侧导航栏固定宽度 88 像素

    // 创建竖直布局，并设置边距和间距
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 16, 8, 16);
    layout->setSpacing(10);

    // 添加按钮,这些按钮放在上半部分
    addButton(QStringLiteral("navMessagesButton"), QStringLiteral("Messages"),
              QStringLiteral("messages"));
    addButton(QStringLiteral("navContactsButton"), QStringLiteral("Contacts"),
              QStringLiteral("contacts"));
    addButton(QStringLiteral("navGroupsButton"), QStringLiteral("Groups"),
              QStringLiteral("groups"));

    // 加一个弹性空白，把 Settings 按钮推到底部
    layout->addStretch();

    addButton(QStringLiteral("navSettingsButton"), QStringLiteral("Settings"),
              QStringLiteral("settings"));
    // 默认选中 Messages
    setActiveSection(QStringLiteral("messages"));
}

void SideBar::setActiveSection(const QString& section_id) {
    for (auto it = buttons_.begin(); it != buttons_.end(); ++it) {
        // 如果当前按钮的 key 等于传进来的 section_id，就设为选中
        it.value()->setChecked(it.key() == section_id);
    }
}

QPushButton* SideBar::addButton(const QString& object_name, const QString& label,
                                const QString& section_id) {
    auto* button = new QPushButton(label, this);
    button->setObjectName(object_name);
    button->setCheckable(true);
    button->setMinimumHeight(44);
    button->setCursor(Qt::PointingHandCursor);
    buttons_.insert(section_id, button);

    auto* layout = qobject_cast<QVBoxLayout*>(this->layout());  // 获取当前布局
    layout->addWidget(button);                                  // 把按钮添加到布局中

    // 绑定点击事件，当按钮被点击时，调用 lambda 函数
    connect(button, &QPushButton::clicked, this, [this, section_id] {
        // 更新按钮选中状态
        setActiveSection(section_id);
        // 通知 MainWindow 切换页面
        emit sectionSelected(section_id);
    });
    return button;
}

}  // namespace liteim::client
