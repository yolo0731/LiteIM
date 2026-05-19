#include "liteim_client/network/TcpClient.hpp"

#include "liteim/base/ErrorCode.hpp"

#include <QMetaType>

#include <vector>

namespace liteim::client {

TcpClient::TcpClient(QObject* parent) : QObject(parent) {
    // 注册 liteim ::Packet，让 Qt 信号系统能安全传递这个类型。
    qRegisterMetaType<liteim::Packet>("liteim::Packet");

    // 把 socket_ 的生命周期交给 TcpClient 管理
    socket_.setParent(this);

    // 连接 QTcpSocket 的信号到 TcpClient 的槽函数，槽函数会在对应事件发生时被调用
    connect(&socket_, &QTcpSocket::connected, this, &TcpClient::connected);
    connect(&socket_, &QTcpSocket::disconnected, this, &TcpClient::disconnected);
    connect(&socket_, &QTcpSocket::readyRead, this, &TcpClient::handleReadyRead);
    connect(&socket_, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this,
            &TcpClient::handleSocketError);
}

void TcpClient::connectToHost(const QString& host, quint16 port) {
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.abort();  // 先断开当前连接，abort() 会立即关闭连接并丢弃未发送的数据
        codec_.reset();   // 重置 PacketCodec 的状态，丢弃之前未完整解析的字节
    }
    // 调用 Qt 的 connectToHost() 连接新地址
    socket_.connectToHost(host, port);
}

void TcpClient::disconnectFromHost() {
    if (socket_.state() == QAbstractSocket::UnconnectedState) {
        return;
    }
    // 正常断开
    socket_.disconnectFromHost();
}

Status TcpClient::sendPacket(const Packet& packet) {
    if (!isConnected()) {
        return Status::error(ErrorCode::IoError, "qt tcp client is not connected");
    }

    QByteArray encoded;
    auto status = PacketCodec::encode(packet, encoded);
    if (!status.isOk()) {
        return status;
    }
    // 将QByteArray写入 TCP socket
    const auto written = socket_.write(encoded);
    if (written < 0 || written != encoded.size()) {
        return Status::error(ErrorCode::IoError, socket_.errorString().toStdString());
    }
    socket_.flush();  // 尽量把数据推给 Qt socket
    return Status::ok();
}

bool TcpClient::isConnected() const noexcept {
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void TcpClient::handleReadyRead() {
    // 收到QByteArray字节
    const auto bytes = socket_.readAll();
    std::vector<Packet> packets;
    // 解析成 Packet
    const auto status = codec_.feed(bytes, packets);
    if (!status.isOk()) {
        emit errorOccurred(QString::fromStdString(status.message()));
        socket_.disconnectFromHost();
        return;
    }

    // 把每个完整 Packet 通过 Qt 信号发给上层
    for (const auto& packet : packets) {
        emit packetReceived(packet);
    }
}

void TcpClient::handleSocketError(QAbstractSocket::SocketError error) {
    // 忽略远程主机关闭连接的错误，因为这是正常的断开流程
    if (error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }
    // 其他错误通过 errorOccurred 信号发给上层
    emit errorOccurred(socket_.errorString());
}

}  // namespace liteim::client
