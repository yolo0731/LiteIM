#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim_client/PacketCodec.hpp"

#include <QAbstractSocket>
#include <QObject>
#include <QString>
#include <QTcpSocket>

Q_DECLARE_METATYPE(liteim::Packet)

namespace liteim::client {

class TcpClient final : public QObject {
    Q_OBJECT

public:
    explicit TcpClient(QObject* parent = nullptr);

    void connectToHost(const QString& host, quint16 port);
    void disconnectFromHost();
    Status sendPacket(const Packet& packet);
    bool isConnected() const noexcept;

signals:
    void connected();
    void disconnected();
    void packetReceived(liteim::Packet packet);
    void errorOccurred(QString message);

private:
    void handleReadyRead();
    void handleSocketError(QAbstractSocket::SocketError error);

    QTcpSocket socket_;
    PacketCodec codec_;
};

}  // namespace liteim::client
