#pragma once

#include <QMainWindow>

namespace liteim::client {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
};

}  // namespace liteim::client
