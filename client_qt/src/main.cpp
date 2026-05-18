#include "liteim_client/MainWindow.hpp"

#include <QApplication>
#include <QFile>

namespace {

void loadApplicationStyle(QApplication& app) {
    // Qt Resource 路径以 ":/" 开头，表示从资源文件中加载样式表
    QFile file(":/qss/app.qss");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    // 把整个 QSS 文件内容设置成全局样式
    app.setStyleSheet(QString::fromUtf8(file.readAll()));
}

}  // namespace

int main(int argc, char* argv[]) {
    // 创建 QApplication 对象，管理应用程序的控制流和主要设置
    QApplication app(argc, argv);
    QApplication::setApplicationName("LiteIM");
    QApplication::setOrganizationName("LiteIM");

    // 加载app.qss
    loadApplicationStyle(app);

    // 创建主窗口对象并显示
    liteim::client::MainWindow window;
    window.show();

    // 进入应用程序的事件循环，等待用户交互
    return QApplication::exec();
}
