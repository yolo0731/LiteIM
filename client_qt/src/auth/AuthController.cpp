#include "liteim_client/auth/AuthController.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim_client/protocol/PacketCodec.hpp"

#include <QMetaType>

#include <utility>

namespace liteim::client {
namespace {

// 去掉字符串首尾的空白字符
QString trimmedCopy(const QString& value) {
    return value.trimmed();
}

QString statusMessage(const Status& status) {
    return QString::fromStdString(status.message());
}

Status parseAuthResult(const Packet& packet, AuthResult& result) {
    TlvMap fields;
    auto status = PacketCodec::parseFields(packet, fields);
    if (!status.isOk()) {
        return status;
    }

    // 从 TLV 字段里解析出 user_id、username、nickname，登录成功的话还有 session_id
    status = PacketCodec::getUint64Field(fields, TlvType::UserId, result.user_id);
    if (!status.isOk()) {
        return status;
    }
    status = PacketCodec::getStringField(fields, TlvType::Username, result.username);
    if (!status.isOk()) {
        return status;
    }
    status = PacketCodec::getStringField(fields, TlvType::Nickname, result.nickname);
    if (!status.isOk()) {
        return status;
    }

    if (packet.header.msg_type == MessageType::LoginResponse) {
        status = PacketCodec::getUint64Field(fields, TlvType::SessionId, result.session_id);
        if (!status.isOk()) {
            return status;
        }
    }
    return Status::ok();
}

QString parseErrorMessage(const Packet& packet) {
    TlvMap fields;
    if (!PacketCodec::parseFields(packet, fields).isOk()) {
        return QStringLiteral("server returned an error");
    }

    QString message;
    if (!PacketCodec::getStringField(fields, TlvType::ErrorMessage, message).isOk()) {
        return QStringLiteral("server returned an error");
    }
    return message;
}

}  // namespace

AuthController::AuthController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<liteim::client::AuthResult>("liteim::client::AuthResult");

    connect(&runtime_.client(), &TcpClient::connected, this, &AuthController::sendPendingRequest);
    connect(&runtime_.client(), &TcpClient::packetReceived, this,
            &AuthController::handlePacketReceived);
    connect(&runtime_.client(), &TcpClient::errorOccurred, this,
            &AuthController::handleTransportError);
}

void AuthController::login(const QString& host, quint16 port, const QString& username,
                           const QString& password) {
    startRequest(PendingAction::Login, host, port, username, password, {});
}

void AuthController::registerUser(const QString& host, quint16 port, const QString& username,
                                  const QString& password, const QString& nickname) {
    startRequest(PendingAction::Register, host, port, username, password, nickname);
}

const ClientSession& AuthController::session() const noexcept {
    return runtime_.session();
}

ClientRuntime& AuthController::runtime() noexcept {
    return runtime_;
}

const ClientRuntime& AuthController::runtime() const noexcept {
    return runtime_;
}

bool AuthController::busy() const noexcept {
    return busy_;
}
// 输入检查、保存 pending 请求信息、设置 busy，然后连接服务器。
void AuthController::startRequest(PendingAction action, const QString& host, quint16 port,
                                  const QString& username, const QString& password,
                                  const QString& nickname) {
    const auto clean_host = trimmedCopy(host);
    const auto clean_username = trimmedCopy(username);
    if (clean_host.isEmpty() || clean_username.isEmpty() || password.isEmpty()) {
        finishWithError(QStringLiteral("server, username and password are required"));
        return;
    }

    pending_action_ = action;
    pending_host_ = clean_host;
    pending_port_ = port;
    pending_username_ = clean_username;
    pending_password_ = password;
    pending_nickname_ = trimmedCopy(nickname);
    setBusy(true);
    runtime_.setConnectionEndpoint(pending_host_, pending_port_);

    if (runtime_.client().isConnected() && connected_host_ == pending_host_ &&
        connected_port_ == pending_port_) {
        sendPendingRequest();
        return;
    }
    connected_host_.clear();
    connected_port_ = 0;
    runtime_.client().connectToHost(pending_host_, pending_port_);
}

void AuthController::sendPendingRequest() {
    if (pending_action_ == PendingAction::None) {
        return;
    }
    connected_host_ = pending_host_;
    connected_port_ = pending_port_;

    Packet packet;
    // 设置 msg_type
    packet.header.msg_type = pending_action_ == PendingAction::Register
                                 ? MessageType::RegisterRequest
                                 : MessageType::LoginRequest;
    //  生成 seq_id
    packet.header.seq_id = runtime_.session().trackRequest(packet.header.msg_type);
    const auto seq_id = packet.header.seq_id;
    auto cleanupPendingAndFail = [this, seq_id](const QString& message) {
        runtime_.session().takePending(seq_id);
        finishWithError(message);
    };

    // 给TLV字段赋值，追加到 packet.body 里
    auto status = PacketCodec::appendStringField(TlvType::Username, pending_username_, packet);
    if (!status.isOk()) {
        cleanupPendingAndFail(statusMessage(status));
        return;
    }
    status = PacketCodec::appendStringField(TlvType::Password, pending_password_, packet);
    if (!status.isOk()) {
        cleanupPendingAndFail(statusMessage(status));
        return;
    }
    if (pending_action_ == PendingAction::Register && !pending_nickname_.isEmpty()) {
        status = PacketCodec::appendStringField(TlvType::Nickname, pending_nickname_, packet);
        if (!status.isOk()) {
            cleanupPendingAndFail(statusMessage(status));
            return;
        }
    }

    status = runtime_.client().sendPacket(packet);
    if (!status.isOk()) {
        cleanupPendingAndFail(statusMessage(status));
    }
}
// 处理服务端响应的登录/注册结果，或者连接过程中发生的网络错误,保存到AuthResult里，发出对应的信号
void AuthController::handlePacketReceived(const Packet& packet) {
    const auto pending = runtime_.session().pendingRequest(packet.header.seq_id);
    if (!pending.has_value()) {
        return;
    }
    if (pending->request_type != MessageType::RegisterRequest &&
        pending->request_type != MessageType::LoginRequest) {
        return;
    }

    runtime_.session().takePending(packet.header.seq_id);

    if (packet.header.msg_type == MessageType::ErrorResponse) {
        pending_action_ = PendingAction::None;
        setBusy(false);
        emit authFailed(parseErrorMessage(packet));
        return;
    }

    AuthResult result;
    const auto status = parseAuthResult(packet, result);
    if (!status.isOk()) {
        finishWithError(statusMessage(status));
        return;
    }

    const auto expected_type = pending->request_type == MessageType::RegisterRequest
                                   ? MessageType::RegisterResponse
                                   : MessageType::LoginResponse;
    if (packet.header.msg_type != expected_type) {
        finishWithError(QStringLiteral("unexpected auth response"));
        return;
    }

    const auto finished_action = pending_action_;
    pending_action_ = PendingAction::None;
    setBusy(false);

    if (finished_action == PendingAction::Register) {
        emit registerSucceeded(result);
        return;
    }

    runtime_.session().markLoggedIn(result.user_id, {}, QString::number(result.session_id));
    runtime_.setAutoReconnectEnabled(true);
    runtime_.startHeartbeat();
    emit loginSucceeded(result);
}

void AuthController::handleTransportError(const QString& message) {
    if (pending_action_ == PendingAction::None && !busy_) {
        return;
    }
    finishWithError(message);
}

void AuthController::finishWithError(const QString& message) {
    pending_action_ = PendingAction::None;
    setBusy(false);
    emit authFailed(message);
}

void AuthController::setBusy(bool busy) {
    if (busy_ == busy) {
        return;
    }
    busy_ = busy;
    emit busyChanged(busy_);
}

}  // namespace liteim::client
