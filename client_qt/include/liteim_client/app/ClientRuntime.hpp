#pragma once

#include "liteim/protocol/Packet.hpp"
#include "liteim_client/network/ClientSession.hpp"
#include "liteim_client/network/TcpClient.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

namespace liteim::client {

class ClientRuntime final : public QObject {
    Q_OBJECT

public:
    explicit ClientRuntime(QObject* parent = nullptr);

    TcpClient& client() noexcept;
    const TcpClient& client() const noexcept;
    ClientSession& session() noexcept;
    const ClientSession& session() const noexcept;

    void setConnectionEndpoint(const QString& host, quint16 port);
    bool hasConnectionEndpoint() const noexcept;
    const QString& connectionHost() const noexcept;
    quint16 connectionPort() const noexcept;

    void reconnect();
    void setAutoReconnectEnabled(bool enabled) noexcept;
    void setReconnectDelayMs(int delay_ms) noexcept;
    void startHeartbeat(int interval_ms = 30000);
    void stopHeartbeat();

    bool isOnline() const noexcept;
    const QString& connectionStatusText() const noexcept;
    int heartbeatIntervalMs() const noexcept;

signals:
    void connectionStatusChanged(QString status_text, bool online);

private:
    void handleConnected();
    void handleDisconnected();
    void handleTransportError(const QString& message);
    void handlePacketReceived(const Packet& packet);
    void sendHeartbeat();
    void setConnectionStatus(QString status_text, bool online);
    void scheduleAutoReconnect();

    TcpClient client_;
    ClientSession session_;
    QTimer heartbeat_timer_;
    QString connection_host_;
    quint16 connection_port_{0};
    QString connection_status_text_{QStringLiteral("Offline")};
    bool online_{false};
    bool auto_reconnect_enabled_{true};
    bool auto_reconnect_used_{false};
    int reconnect_delay_ms_{1000};
    int heartbeat_interval_ms_{30000};
};

}  // namespace liteim::client
