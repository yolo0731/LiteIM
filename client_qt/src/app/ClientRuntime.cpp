#include "liteim_client/app/ClientRuntime.hpp"

#include "liteim/protocol/MessageType.hpp"

#include <utility>

namespace liteim::client {

ClientRuntime::ClientRuntime(QObject* parent) : QObject(parent), client_(this) {
    heartbeat_timer_.setParent(this);
    heartbeat_timer_.setSingleShot(false);
    heartbeat_timer_.setInterval(heartbeat_interval_ms_);

    connect(&client_, &TcpClient::connected, this, &ClientRuntime::handleConnected);
    connect(&client_, &TcpClient::disconnected, this, &ClientRuntime::handleDisconnected);
    connect(&client_, &TcpClient::errorOccurred, this, &ClientRuntime::handleTransportError);
    connect(&client_, &TcpClient::packetReceived, this, &ClientRuntime::handlePacketReceived);
    connect(&heartbeat_timer_, &QTimer::timeout, this, &ClientRuntime::sendHeartbeat);
}

TcpClient& ClientRuntime::client() noexcept {
    return client_;
}

const TcpClient& ClientRuntime::client() const noexcept {
    return client_;
}

ClientSession& ClientRuntime::session() noexcept {
    return session_;
}

const ClientSession& ClientRuntime::session() const noexcept {
    return session_;
}

void ClientRuntime::setConnectionEndpoint(const QString& host, quint16 port) {
    connection_host_ = host.trimmed();
    connection_port_ = port;
    auto_reconnect_used_ = false;
}

bool ClientRuntime::hasConnectionEndpoint() const noexcept {
    return !connection_host_.isEmpty() && connection_port_ != 0;
}

const QString& ClientRuntime::connectionHost() const noexcept {
    return connection_host_;
}

quint16 ClientRuntime::connectionPort() const noexcept {
    return connection_port_;
}

void ClientRuntime::reconnect() {
    if (!hasConnectionEndpoint()) {
        setConnectionStatus(QStringLiteral("Offline: no server configured"), false);
        return;
    }
    setConnectionStatus(QStringLiteral("Connecting..."), false);
    client_.connectToHost(connection_host_, connection_port_);
}

void ClientRuntime::setAutoReconnectEnabled(bool enabled) noexcept {
    auto_reconnect_enabled_ = enabled;
}

void ClientRuntime::setReconnectDelayMs(int delay_ms) noexcept {
    reconnect_delay_ms_ = delay_ms < 0 ? 0 : delay_ms;
}

void ClientRuntime::startHeartbeat(int interval_ms) {
    heartbeat_interval_ms_ = interval_ms <= 0 ? 30000 : interval_ms;
    heartbeat_timer_.setInterval(heartbeat_interval_ms_);
    if (client_.isConnected() && session_.isLoggedIn()) {
        heartbeat_timer_.start();
    }
}

void ClientRuntime::stopHeartbeat() {
    heartbeat_timer_.stop();
}

bool ClientRuntime::isOnline() const noexcept {
    return online_;
}

const QString& ClientRuntime::connectionStatusText() const noexcept {
    return connection_status_text_;
}

int ClientRuntime::heartbeatIntervalMs() const noexcept {
    return heartbeat_interval_ms_;
}

void ClientRuntime::handleConnected() {
    setConnectionStatus(QStringLiteral("Online"), true);
    if (session_.isLoggedIn()) {
        startHeartbeat(heartbeat_interval_ms_);
    }
}

void ClientRuntime::handleDisconnected() {
    stopHeartbeat();
    setConnectionStatus(QStringLiteral("Offline"), false);
    scheduleAutoReconnect();
}

void ClientRuntime::handleTransportError(const QString& message) {
    if (message.isEmpty()) {
        setConnectionStatus(QStringLiteral("Connection error"), false);
        return;
    }
    setConnectionStatus(QStringLiteral("Connection error: ") + message, false);
}

void ClientRuntime::handlePacketReceived(const Packet& packet) {
    if (packet.header.msg_type != MessageType::HeartbeatResponse) {
        return;
    }

    const auto pending = session_.pendingRequest(packet.header.seq_id);
    if (!pending.has_value() || pending->request_type != MessageType::HeartbeatRequest) {
        return;
    }
    session_.takePending(packet.header.seq_id);
    setConnectionStatus(QStringLiteral("Online"), true);
}

void ClientRuntime::sendHeartbeat() {
    if (!client_.isConnected() || !session_.isLoggedIn()) {
        return;
    }

    Packet packet;
    packet.header.msg_type = MessageType::HeartbeatRequest;
    packet.header.seq_id = session_.trackRequest(packet.header.msg_type);
    const auto status = client_.sendPacket(packet);
    if (!status.isOk()) {
        session_.takePending(packet.header.seq_id);
        setConnectionStatus(QStringLiteral("Connection error: ") +
                                QString::fromStdString(status.message()),
                            false);
    }
}

void ClientRuntime::setConnectionStatus(QString status_text, bool online) {
    if (connection_status_text_ == status_text && online_ == online) {
        return;
    }
    connection_status_text_ = std::move(status_text);
    online_ = online;
    emit connectionStatusChanged(connection_status_text_, online_);
}

void ClientRuntime::scheduleAutoReconnect() {
    if (!auto_reconnect_enabled_ || auto_reconnect_used_ || !hasConnectionEndpoint() ||
        !session_.isLoggedIn()) {
        return;
    }

    auto_reconnect_used_ = true;
    QTimer::singleShot(reconnect_delay_ms_, this, [this] {
        if (!client_.isConnected()) {
            reconnect();
        }
    });
}

}  // namespace liteim::client
