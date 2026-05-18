#pragma once

#include <QMainWindow>

namespace liteim::client {

// QMainWindow 是 Qt Widgets 里专门用来做主窗口的类，它提供了菜单栏、工具栏、状态栏等常见的主窗口元素
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);  // 父控件指针，默认为 nullptr，表示没有父窗口
};

}  // namespace liteim::client
