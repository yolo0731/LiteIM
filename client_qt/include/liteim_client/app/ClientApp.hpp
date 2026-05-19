#pragma once

#include "liteim_client/ui/LoginWindow.hpp"

#include <QObject>

namespace liteim::client {

// 连接登录窗口和主窗口的信号槽
void connectLoginWindowToMainWindow(LoginWindow& login_window, QObject& context);

}  // namespace liteim::client
