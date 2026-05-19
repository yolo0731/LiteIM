#pragma once

#include <QString>
#include <QWidget>

class QPushButton;
class QTextEdit;

namespace liteim::client {

class ChatInputBar final : public QWidget {
    Q_OBJECT

public:
    explicit ChatInputBar(QWidget* parent = nullptr);

    QString text() const;
    void clear();

signals:
    void sendRequested(QString text);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void submit();
    void updateSendButton();

    QTextEdit* input_edit_{nullptr};
    QPushButton* send_button_{nullptr};
};

}  // namespace liteim::client
