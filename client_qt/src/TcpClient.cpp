#include "liteim_client/TcpClient.hpp"

#include "liteim/base/ErrorCode.hpp"

#include <QMetaType>

#include <vector>

namespace liteim::client {

TcpClient::TcpClient(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<liteim::Packet>("liteim::Packet");

    socket_.setParent(this);

    connect(&socket_, &QTcpSocket::connected, this, &TcpClient::connected);
    connect(&socket_, &QTcpSocket::disconnected, this, &TcpClient::disconnected);
    connect(&socket_, &QTcpSocket::readyRead, this, &TcpClient::handleReadyRead);
    connect(&socket_,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this,
            &TcpClient::handleSocketError);
}

void TcpClient::connectToHost(const QString& host, quint16 port) {
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.abort();
        codec_.reset();
    }
    socket_.connectToHost(host, port);
}

void TcpClient::disconnectFromHost() {
    if (socket_.state() == QAbstractSocket::UnconnectedState) {
        return;
    }
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

    const auto written = socket_.write(encoded);
    if (written < 0 || written != encoded.size()) {
        return Status::error(ErrorCode::IoError, socket_.errorString().toStdString());
    }
    socket_.flush();
    return Status::ok();
}

bool TcpClient::isConnected() const noexcept {
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void TcpClient::handleReadyRead() {
    const auto bytes = socket_.readAll();
    std::vector<Packet> packets;
    const auto status = codec_.feed(bytes, packets);
    if (!status.isOk()) {
        emit errorOccurred(QString::fromStdString(status.message()));
        socket_.disconnectFromHost();
        return;
    }

    for (const auto& packet : packets) {
        emit packetReceived(packet);
    }
}

void TcpClient::handleSocketError(QAbstractSocket::SocketError error) {
    if (error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }
    emit errorOccurred(socket_.errorString());
}

}  // namespace liteim::client
