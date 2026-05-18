#include "liteim_client/MainWindow.hpp"

#include <QApplication>
#include <QFile>

namespace {

void loadApplicationStyle(QApplication& app) {
    QFile file(":/qss/app.qss");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    app.setStyleSheet(QString::fromUtf8(file.readAll()));
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("LiteIM");
    QApplication::setOrganizationName("LiteIM");

    loadApplicationStyle(app);

    liteim::client::MainWindow window;
    window.show();

    return QApplication::exec();
}
