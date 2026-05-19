#include "liteim_client/app/ClientApp.hpp"

#include "liteim_client/ui/MainWindow.hpp"

namespace liteim::client {

void connectLoginWindowToMainWindow(LoginWindow& login_window, QObject& context) {
    QObject::connect(&login_window,                 // 监听登录窗口
                     &LoginWindow::loginSucceeded,  // 监听登录成功信号
                     &context,                      //  这个连接归 context 管
                     [&login_window](const AuthResult&) {
                         auto* main_window = new MainWindow(login_window.runtime());  // 没有 parent
                         main_window->setAttribute(Qt::WA_DeleteOnClose);  // close时,QT负责释放它
                         main_window->show();                              // 显示 MainWindow
                         login_window.close();                             // 关闭 LoginWindow
                     });
}

}  // namespace liteim::client
