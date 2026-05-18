#include "liteim_client/SideBar.hpp"

#include <QPushButton>
#include <QVBoxLayout>

namespace liteim::client {

SideBar::SideBar(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("sideBar"));
    setFixedWidth(88);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 16, 8, 16);
    layout->setSpacing(10);

    addButton(QStringLiteral("navMessagesButton"), QStringLiteral("Messages"),
              QStringLiteral("messages"));
    addButton(QStringLiteral("navContactsButton"), QStringLiteral("Contacts"),
              QStringLiteral("contacts"));
    addButton(QStringLiteral("navGroupsButton"), QStringLiteral("Groups"),
              QStringLiteral("groups"));
    addButton(QStringLiteral("navAgentButton"), QStringLiteral("Agent"),
              QStringLiteral("agent"));

    layout->addStretch();

    addButton(QStringLiteral("navSettingsButton"), QStringLiteral("Settings"),
              QStringLiteral("settings"));
    setActiveSection(QStringLiteral("messages"));
}

void SideBar::setActiveSection(const QString& section_id) {
    for (auto it = buttons_.begin(); it != buttons_.end(); ++it) {
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

    auto* layout = qobject_cast<QVBoxLayout*>(this->layout());
    layout->addWidget(button);

    connect(button, &QPushButton::clicked, this, [this, section_id] {
        setActiveSection(section_id);
        emit sectionSelected(section_id);
    });
    return button;
}

}  // namespace liteim::client
