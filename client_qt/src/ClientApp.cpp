#include "liteim_client/ClientApp.hpp"

#include "liteim_client/MainWindow.hpp"

namespace liteim::client {

void connectLoginWindowToMainWindow(LoginWindow& login_window, QObject& context) {
    QObject::connect(&login_window,
                     &LoginWindow::loginSucceeded,
                     &context,
                     [&login_window](const AuthResult&) {
                         auto* main_window = new MainWindow;
                         main_window->setAttribute(Qt::WA_DeleteOnClose);
                         main_window->show();
                         login_window.close();
                     });
}

}  // namespace liteim::client
