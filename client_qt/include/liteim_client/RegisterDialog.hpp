#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;

namespace liteim::client {

class RegisterDialog final : public QDialog {
    Q_OBJECT

public:
    explicit RegisterDialog(QWidget* parent = nullptr);

    QString username() const;
    QString password() const;
    QString nickname() const;

private:
    void buildUi();
    void updateSubmitButton();

    QLineEdit* username_edit_{nullptr};
    QLineEdit* password_edit_{nullptr};
    QLineEdit* nickname_edit_{nullptr};
    QPushButton* submit_button_{nullptr};
    QPushButton* cancel_button_{nullptr};
};

}  // namespace liteim::client
