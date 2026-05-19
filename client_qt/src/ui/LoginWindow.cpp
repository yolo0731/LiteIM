#include "liteim_client/ui/LoginWindow.hpp"

#include "liteim_client/ui/RegisterDialog.hpp"

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
      auth_controller_(this) {  // 构造 auth_controller_，并把当前登录窗口 this 传给它当 parent

    buildUi();             // 构建 UI
    loadRecentSettings();  // 加载上次登录信息
    updateLoginButton();   // 更新登录按钮状态

    // 提前登记：发生某件事时，把认证结果接回 UI
    connect(&auth_controller_, &AuthController::registerSucceeded, this,
            &LoginWindow::handleRegisterSucceeded);
    connect(&auth_controller_, &AuthController::loginSucceeded, this,
            &LoginWindow::handleLoginSucceeded);
    connect(&auth_controller_, &AuthController::authFailed, this, &LoginWindow::showError);
    // 请求进行中：禁用 Login 和 Register
    connect(&auth_controller_, &AuthController::busyChanged, this, [this](bool busy) {
        login_button_->setEnabled(!busy && !server_host_edit_->text().trimmed().isEmpty() &&
                                  !username_edit_->text().trimmed().isEmpty() &&
                                  !password_edit_->text().isEmpty());
        register_button_->setEnabled(!busy);
    });
}

void LoginWindow::buildUi() {
    // 设置窗口标题和大小
    setWindowTitle(QStringLiteral("LiteIM Login"));
    resize(420, 260);

    // 服务器地址输入框
    server_host_edit_ = new QLineEdit(this);  // QLineEdit是单行文本输入框
    server_host_edit_->setObjectName(QStringLiteral("serverHostEdit"));
    server_host_edit_->setPlaceholderText(QStringLiteral("127.0.0.1"));

    // 服务器端口输入框
    server_port_spin_ = new QSpinBox(this);  // QSpinBox 是数字输入框
    server_port_spin_->setObjectName(QStringLiteral("serverPortSpinBox"));
    server_port_spin_->setRange(1, 65535);
    server_port_spin_->setValue(9000);

    // 用户名输入框
    username_edit_ = new QLineEdit(this);
    username_edit_->setObjectName(QStringLiteral("usernameEdit"));
    username_edit_->setPlaceholderText(QStringLiteral("username"));

    // 密码输入框
    password_edit_ = new QLineEdit(this);
    password_edit_->setObjectName(QStringLiteral("passwordEdit"));
    password_edit_->setPlaceholderText(QStringLiteral("password"));
    password_edit_->setEchoMode(QLineEdit::Password);  //不明文显示

    // 创建表单布局
    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Server"), server_host_edit_);
    form->addRow(QStringLiteral("Port"), server_port_spin_);
    form->addRow(QStringLiteral("Username"), username_edit_);
    form->addRow(QStringLiteral("Password"), password_edit_);

    // 创建登录和注册按钮
    login_button_ = new QPushButton(QStringLiteral("Login"), this);
    login_button_->setObjectName(QStringLiteral("loginButton"));
    register_button_ = new QPushButton(QStringLiteral("Register"), this);
    register_button_->setObjectName(QStringLiteral("registerButton"));

    // 创建按钮布局
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(register_button_);
    buttons->addWidget(login_button_);

    // 创建状态提示标签
    status_label_ = new QLabel(this);
    status_label_->setObjectName(QStringLiteral("loginStatusLabel"));
    status_label_->setWordWrap(true);  // 错误信息太长时自动换行

    // 创建整体纵向布局
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addWidget(status_label_);

    //  绑定用户事件,输入框变化时，更新登录按钮状态
    connect(server_host_edit_, &QLineEdit::textChanged, this, &LoginWindow::updateLoginButton);
    connect(username_edit_, &QLineEdit::textChanged, this, &LoginWindow::updateLoginButton);
    connect(password_edit_, &QLineEdit::textChanged, this, &LoginWindow::updateLoginButton);
    connect(login_button_, &QPushButton::clicked, this, &LoginWindow::startLogin);
    connect(register_button_, &QPushButton::clicked, this, &LoginWindow::openRegisterDialog);
}

void LoginWindow::loadRecentSettings() {
    QSettings settings;
    server_host_edit_->setText(
        settings.value(QStringLiteral("login/host"), QStringLiteral("127.0.0.1")).toString());
    server_port_spin_->setValue(settings.value(QStringLiteral("login/port"), 9000).toInt());
    username_edit_->setText(settings.value(QStringLiteral("login/username")).toString());
}

void LoginWindow::saveRecentSettings() const {
    QSettings settings;
    settings.setValue(QStringLiteral("login/host"), server_host_edit_->text().trimmed());
    settings.setValue(QStringLiteral("login/port"), server_port_spin_->value());
    settings.setValue(QStringLiteral("login/username"), username_edit_->text().trimmed());
}

// 只有在服务器地址、用户名、密码都不空，并且当前没有登录请求在进行时，才允许点击登录按钮
void LoginWindow::updateLoginButton() {
    const auto enabled = !server_host_edit_->text().trimmed().isEmpty() &&
                         !username_edit_->text().trimmed().isEmpty() &&
                         !password_edit_->text().isEmpty() && !auth_controller_.busy();
    login_button_->setEnabled(enabled);
}
// 保存登录信息并转发给auth_controller_
void LoginWindow::startLogin() {
    saveRecentSettings();
    status_label_->setText(QStringLiteral("Connecting..."));
    auth_controller_.login(server_host_edit_->text(),
                           static_cast<quint16>(server_port_spin_->value()), username_edit_->text(),
                           password_edit_->text());
}
// 打开注册对话框，注册成功后会自动填充用户名，并提示登录
void LoginWindow::openRegisterDialog() {
    RegisterDialog dialog(this);  // 创建注册对话框，并把当前登录窗口 this 传给它当 parent
    //  会打开一个注册弹窗，并阻塞等待用户操作
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    saveRecentSettings();
    status_label_->setText(QStringLiteral("Registering..."));
    auth_controller_.registerUser(server_host_edit_->text(),
                                  static_cast<quint16>(server_port_spin_->value()),
                                  dialog.username(), dialog.password(), dialog.nickname());
}
// 注册成功后，自动填充用户名，并提示登录
void LoginWindow::handleRegisterSucceeded(const AuthResult& result) {
    username_edit_->setText(result.username);
    password_edit_->clear();
    status_label_->setText(QStringLiteral("Register succeeded. Please log in."));
}

void LoginWindow::handleLoginSucceeded(const AuthResult& result) {
    status_label_->setText(QStringLiteral("Login succeeded."));
    // 继续向外通知,登录窗口登录成功,告诉mainwindow
    emit loginSucceeded(result);
}

void LoginWindow::showError(const QString& message) {
    status_label_->setText(message);
    updateLoginButton();
}

}  // namespace liteim::client
