#pragma once

#include "liteim/protocol/MessageType.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim_client/network/ClientSession.hpp"
#include "liteim_client/network/TcpClient.hpp"

#include <QObject>
#include <QString>

#include <cstdint>

namespace liteim::client {

struct AuthResult {
    std::uint64_t user_id{0};
    QString username;
    QString nickname;
    std::uint64_t session_id{0};
};

class AuthController final : public QObject {
Q_OBJECT  // 可以使用 Qt 信号槽

    public : explicit AuthController(QObject* parent = nullptr);

    // UI 点登录时调用它
    void login(const QString& host, quint16 port, const QString& username, const QString& password);
    void registerUser(const QString& host, quint16 port, const QString& username,
                      const QString& password, const QString& nickname);

    // 返回当前客户端本地登录状态
    const ClientSession& session() const noexcept;
    bool busy() const noexcept;

signals:
    void registerSucceeded(liteim::client::AuthResult result);
    void loginSucceeded(liteim::client::AuthResult result);
    void authFailed(QString message);
    void busyChanged(bool busy);

private:
    // 当前正在做什么
    enum class PendingAction {
        None,
        Register,
        Login,
    };

    void startRequest(PendingAction action, const QString& host, quint16 port,
                      const QString& username, const QString& password, const QString& nickname);
    void sendPendingRequest();
    void handlePacketReceived(const Packet& packet);
    void handleTransportError(const QString& message);
    void finishWithError(const QString& message);
    void setBusy(bool busy);

    TcpClient client_;
    ClientSession session_;

    PendingAction pending_action_{PendingAction::None};
    QString connected_host_;
    quint16 connected_port_{0};
    QString pending_host_;
    quint16 pending_port_{0};
    QString pending_username_;
    QString pending_password_;
    QString pending_nickname_;
    bool busy_{false};
};

}  // namespace liteim::client

Q_DECLARE_METATYPE(liteim::client::AuthResult)
