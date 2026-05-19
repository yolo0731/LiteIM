#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim_client/protocol/PacketCodec.hpp"

#include <QAbstractSocket>
#include <QObject>
#include <QString>
#include <QTcpSocket>

/*
TcpClient 是 Qt 客户端的网络层封装，内部用 QTcpSocket 连接服务器，用 PacketCodec 把 LiteIM 的 Packet 编码/解码，然后通过 Qt 信号把连接状态、收到的包、错误抛给上层 UI 或业务逻辑*/

// liteim::Packet 是一个可以被 Qt 元对象系统识别的类型
Q_DECLARE_METATYPE(liteim::Packet)

namespace liteim::client {

// 必须继承 QObject
class TcpClient final : public QObject {
Q_OBJECT  // QT的元对象系统需要这个宏来支持信号和槽机制，生成对应的 moc 源码

    public : explicit TcpClient(QObject* parent = nullptr);

    void connectToHost(const QString& host, quint16 port);
    void disconnectFromHost();
    Status sendPacket(const Packet& packet);
    bool isConnected() const noexcept;

    // 这些函数是 Qt 对外信号,由 Qt 的 moc 生成相关调用代码, 通过 emit 关键字发出信号
signals:
    void connected();
    void disconnected();
    void packetReceived(liteim::Packet packet);
    void errorOccurred(QString message);

private:
    void handleReadyRead();
    void handleSocketError(QAbstractSocket::SocketError error);

    //  负责 TCP 连接、收发字节
    QTcpSocket socket_;
    // 负责 Packet <-> QByteArray
    PacketCodec codec_;
};

}  // namespace liteim::client
