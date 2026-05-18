#include "liteim_client/MainWindow.hpp"

#include <QWidget>

namespace liteim::client {
// 先构造父类 QMainWindow
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // 设置窗口标题和初始大小
    setWindowTitle("LiteIM");
    resize(1080, 720);
    // 设置最小大小：
    setMinimumSize(900, 600);
    // QMainWindow 需要一个中心窗口部件，父对象是当前主窗口 this
    setCentralWidget(new QWidget(this));
}

}  // namespace liteim::client
