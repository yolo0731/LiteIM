#pragma once

#include "liteim_client/LoginWindow.hpp"

#include <QObject>

namespace liteim::client {

void connectLoginWindowToMainWindow(LoginWindow& login_window, QObject& context);

}  // namespace liteim::client
