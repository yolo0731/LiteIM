#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"

// QT客户端只接受和发送 Qt 网络层能直接处理的 QByteArray
#include <QByteArray>
#include <QString>

#include <cstdint>
#include <vector>

namespace liteim::client {

class PacketCodec {
public:
    // 把一个 LiteIM 协议包 Packet 编码成 Qt 网络层能直接发送的 QByteArray
    static Status encode(const Packet& packet, QByteArray& output);
    // 往 packet.body 里追加 TLV 字段
    static Status appendStringField(TlvType type, const QString& value, Packet& packet);
    static Status appendUint64Field(TlvType type, std::uint64_t value, Packet& packet);
    // 从服务端返回的 Packet 里解析 TLV 字段，再转成 Qt 客户端能直接使用的类型
    static Status getStringField(const TlvMap& fields, TlvType type, QString& output);
    static Status getUint64Field(const TlvMap& fields, TlvType type, std::uint64_t& output);
    static Status parseFields(const Packet& packet, TlvMap& output);
    // 用来喂入从 QTcpSocket 收到的字节
    Status feed(const QByteArray& bytes, std::vector<Packet>& output);
    void reset();

private:
    FrameDecoder decoder_;
};

}  // namespace liteim::client
