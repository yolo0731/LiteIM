#pragma once

#include "liteim_client/auth/AuthController.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace liteim::client {
// LoginWindow 是 登录 UI 层，只收集输入、显示状态、转发请求

class ClientRuntime;

class LoginWindow final : public QWidget {
    Q_OBJECT

public:
    explicit LoginWindow(QWidget* parent = nullptr);
    ClientRuntime& runtime() noexcept;
    const ClientRuntime& runtime() const noexcept;

signals:
    void loginSucceeded(liteim::client::AuthResult result);

private:
    // 创建UI组件
    void buildUi();
    // 用QSettings 读取上次登录信息
    void loadRecentSettings();
    void saveRecentSettings() const;
    void updateLoginButton();
    void startLogin();
    void openRegisterDialog();
    void handleRegisterSucceeded(const AuthResult& result);
    void handleLoginSucceeded(const AuthResult& result);
    void showError(const QString& message);

    AuthController auth_controller_;  // 负责认证请求

    QLineEdit* server_host_edit_{nullptr};
    QSpinBox* server_port_spin_{nullptr};
    QLineEdit* username_edit_{nullptr};
    QLineEdit* password_edit_{nullptr};
    QPushButton* login_button_{nullptr};
    QPushButton* register_button_{nullptr};
    QLabel* status_label_{nullptr};
};

}  // namespace liteim::client
