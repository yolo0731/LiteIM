#include "liteim_client/ui/RegisterDialog.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace liteim::client {

RegisterDialog::RegisterDialog(QWidget* parent) : QDialog(parent) {
    buildUi();
    updateSubmitButton();
}

QString RegisterDialog::username() const {
    return username_edit_->text().trimmed();
}

QString RegisterDialog::password() const {
    return password_edit_->text();
}

QString RegisterDialog::nickname() const {
    return nickname_edit_->text().trimmed();
}

void RegisterDialog::buildUi() {
    setWindowTitle(QStringLiteral("Register LiteIM Account"));
    setModal(true);

    // 创建输入框
    username_edit_ = new QLineEdit(this);
    username_edit_->setObjectName(QStringLiteral("registerUsernameEdit"));
    username_edit_->setPlaceholderText(QStringLiteral("username"));

    password_edit_ = new QLineEdit(this);
    password_edit_->setObjectName(QStringLiteral("registerPasswordEdit"));
    password_edit_->setPlaceholderText(QStringLiteral("password"));
    password_edit_->setEchoMode(QLineEdit::Password);

    nickname_edit_ = new QLineEdit(this);
    nickname_edit_->setObjectName(QStringLiteral("registerNicknameEdit"));
    nickname_edit_->setPlaceholderText(QStringLiteral("optional nickname"));

    // 创建表单布局
    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Username"), username_edit_);
    form->addRow(QStringLiteral("Password"), password_edit_);
    form->addRow(QStringLiteral("Nickname"), nickname_edit_);

    // 创建按钮
    submit_button_ = new QPushButton(QStringLiteral("Register"), this);
    submit_button_->setObjectName(QStringLiteral("registerSubmitButton"));
    cancel_button_ = new QPushButton(QStringLiteral("Cancel"), this);
    cancel_button_->setObjectName(QStringLiteral("registerCancelButton"));

    // 按钮布局
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel_button_);
    buttons->addWidget(submit_button_);

    // 整体布局
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(buttons);

    // 信号连接,用户名或密码变化时，更新注册按钮状态
    connect(username_edit_, &QLineEdit::textChanged, this, &RegisterDialog::updateSubmitButton);
    connect(password_edit_, &QLineEdit::textChanged, this, &RegisterDialog::updateSubmitButton);
    // 点击取消：关闭弹窗，并返回 Rejected
    connect(cancel_button_, &QPushButton::clicked, this, &RegisterDialog::reject);
    // 点击注册：关闭弹窗，并返回 Accepted
    connect(submit_button_, &QPushButton::clicked, this, &RegisterDialog::accept);
}

void RegisterDialog::updateSubmitButton() {
    submit_button_->setEnabled(!username().isEmpty() && !password().isEmpty());
}

}  // namespace liteim::client
