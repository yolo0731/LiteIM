#include "liteim_client/LoginWindow.hpp"

#include "liteim_client/RegisterDialog.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace liteim::client {

LoginWindow::LoginWindow(QWidget* parent)
    : QWidget(parent),
      auth_controller_(this) {
    buildUi();
    loadRecentSettings();
    updateLoginButton();

    connect(&auth_controller_, &AuthController::registerSucceeded, this,
            &LoginWindow::handleRegisterSucceeded);
    connect(&auth_controller_, &AuthController::loginSucceeded, this,
            &LoginWindow::handleLoginSucceeded);
    connect(&auth_controller_, &AuthController::authFailed, this, &LoginWindow::showError);
    connect(&auth_controller_, &AuthController::busyChanged, this, [this](bool busy) {
        login_button_->setEnabled(!busy && !server_host_edit_->text().trimmed().isEmpty() &&
                                  !username_edit_->text().trimmed().isEmpty() &&
                                  !password_edit_->text().isEmpty());
        register_button_->setEnabled(!busy);
    });
}

void LoginWindow::buildUi() {
    setWindowTitle(QStringLiteral("LiteIM Login"));
    resize(420, 260);

    server_host_edit_ = new QLineEdit(this);
    server_host_edit_->setObjectName(QStringLiteral("serverHostEdit"));
    server_host_edit_->setPlaceholderText(QStringLiteral("127.0.0.1"));

    server_port_spin_ = new QSpinBox(this);
    server_port_spin_->setObjectName(QStringLiteral("serverPortSpinBox"));
    server_port_spin_->setRange(1, 65535);
    server_port_spin_->setValue(9000);

    username_edit_ = new QLineEdit(this);
    username_edit_->setObjectName(QStringLiteral("usernameEdit"));
    username_edit_->setPlaceholderText(QStringLiteral("username"));

    password_edit_ = new QLineEdit(this);
    password_edit_->setObjectName(QStringLiteral("passwordEdit"));
    password_edit_->setPlaceholderText(QStringLiteral("password"));
    password_edit_->setEchoMode(QLineEdit::Password);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Server"), server_host_edit_);
    form->addRow(QStringLiteral("Port"), server_port_spin_);
    form->addRow(QStringLiteral("Username"), username_edit_);
    form->addRow(QStringLiteral("Password"), password_edit_);

    login_button_ = new QPushButton(QStringLiteral("Login"), this);
    login_button_->setObjectName(QStringLiteral("loginButton"));
    register_button_ = new QPushButton(QStringLiteral("Register"), this);
    register_button_->setObjectName(QStringLiteral("registerButton"));

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(register_button_);
    buttons->addWidget(login_button_);

    status_label_ = new QLabel(this);
    status_label_->setObjectName(QStringLiteral("loginStatusLabel"));
    status_label_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addWidget(status_label_);

    connect(server_host_edit_, &QLineEdit::textChanged, this, &LoginWindow::updateLoginButton);
    connect(username_edit_, &QLineEdit::textChanged, this, &LoginWindow::updateLoginButton);
    connect(password_edit_, &QLineEdit::textChanged, this, &LoginWindow::updateLoginButton);
    connect(login_button_, &QPushButton::clicked, this, &LoginWindow::startLogin);
    connect(register_button_, &QPushButton::clicked, this, &LoginWindow::openRegisterDialog);
}

void LoginWindow::loadRecentSettings() {
    QSettings settings;
    server_host_edit_->setText(settings.value(QStringLiteral("login/host"),
                                              QStringLiteral("127.0.0.1"))
                                   .toString());
    server_port_spin_->setValue(settings.value(QStringLiteral("login/port"), 9000).toInt());
    username_edit_->setText(settings.value(QStringLiteral("login/username")).toString());
}

void LoginWindow::saveRecentSettings() const {
    QSettings settings;
    settings.setValue(QStringLiteral("login/host"), server_host_edit_->text().trimmed());
    settings.setValue(QStringLiteral("login/port"), server_port_spin_->value());
    settings.setValue(QStringLiteral("login/username"), username_edit_->text().trimmed());
}

void LoginWindow::updateLoginButton() {
    const auto enabled = !server_host_edit_->text().trimmed().isEmpty() &&
                         !username_edit_->text().trimmed().isEmpty() &&
                         !password_edit_->text().isEmpty() && !auth_controller_.busy();
    login_button_->setEnabled(enabled);
}

void LoginWindow::startLogin() {
    saveRecentSettings();
    status_label_->setText(QStringLiteral("Connecting..."));
    auth_controller_.login(server_host_edit_->text(),
                           static_cast<quint16>(server_port_spin_->value()),
                           username_edit_->text(),
                           password_edit_->text());
}

void LoginWindow::openRegisterDialog() {
    RegisterDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    saveRecentSettings();
    status_label_->setText(QStringLiteral("Registering..."));
    auth_controller_.registerUser(server_host_edit_->text(),
                                  static_cast<quint16>(server_port_spin_->value()),
                                  dialog.username(),
                                  dialog.password(),
                                  dialog.nickname());
}

void LoginWindow::handleRegisterSucceeded(const AuthResult& result) {
    username_edit_->setText(result.username);
    password_edit_->clear();
    status_label_->setText(QStringLiteral("Register succeeded. Please log in."));
}

void LoginWindow::handleLoginSucceeded(const AuthResult& result) {
    status_label_->setText(QStringLiteral("Login succeeded."));
    emit loginSucceeded(result);
}

void LoginWindow::showError(const QString& message) {
    status_label_->setText(message);
    updateLoginButton();
}

}  // namespace liteim::client
