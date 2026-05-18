#pragma once

#include "liteim_client/AuthController.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace liteim::client {

class LoginWindow final : public QWidget {
    Q_OBJECT

public:
    explicit LoginWindow(QWidget* parent = nullptr);

signals:
    void loginSucceeded(liteim::client::AuthResult result);

private:
    void buildUi();
    void loadRecentSettings();
    void saveRecentSettings() const;
    void updateLoginButton();
    void startLogin();
    void openRegisterDialog();
    void handleRegisterSucceeded(const AuthResult& result);
    void handleLoginSucceeded(const AuthResult& result);
    void showError(const QString& message);

    AuthController auth_controller_;

    QLineEdit* server_host_edit_{nullptr};
    QSpinBox* server_port_spin_{nullptr};
    QLineEdit* username_edit_{nullptr};
    QLineEdit* password_edit_{nullptr};
    QPushButton* login_button_{nullptr};
    QPushButton* register_button_{nullptr};
    QLabel* status_label_{nullptr};
};

}  // namespace liteim::client
