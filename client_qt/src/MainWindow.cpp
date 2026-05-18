#include "liteim_client/MainWindow.hpp"

#include <QWidget>

namespace liteim::client {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("LiteIM");
    resize(1080, 720);
    setMinimumSize(900, 600);
    setCentralWidget(new QWidget(this));
}

}  // namespace liteim::client
