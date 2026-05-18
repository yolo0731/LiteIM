#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

class QPushButton;

namespace liteim::client {

class SideBar final : public QWidget {
    Q_OBJECT

public:
    explicit SideBar(QWidget* parent = nullptr);

    void setActiveSection(const QString& section_id);

signals:
    void sectionSelected(QString section_id);

private:
    QPushButton* addButton(const QString& object_name, const QString& label,
                           const QString& section_id);

    QHash<QString, QPushButton*> buttons_;
};

}  // namespace liteim::client
