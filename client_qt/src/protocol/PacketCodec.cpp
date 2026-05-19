#include "liteim_client/protocol/PacketCodec.hpp"

#include <string>

namespace liteim::client {

// 调用核心协议库的 encodePacket() 来把 Packet 编码成 Bytes，然后转成 QByteArray
Status PacketCodec::encode(const Packet& packet, QByteArray& output) {
    Bytes bytes;
    const auto status = encodePacket(packet, bytes);
    if (!status.isOk()) {
        output.clear();
        return status;
    }

    output =
        QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()));
    return Status::ok();
}

// 将 QString 转成std::string，再调用核心协议库的 appendString() 来追加到 Packet 的 body 里
Status PacketCodec::appendStringField(TlvType type, const QString& value, Packet& packet) {
    const auto utf8 = value.toUtf8();
    const std::string text(utf8.constData(), static_cast<std::size_t>(utf8.size()));
    return appendString(type, text, packet.body);
}

Status PacketCodec::appendUint64Field(TlvType type, std::uint64_t value, Packet& packet) {
    return appendUint64(type, value, packet.body);
}

Status PacketCodec::getStringField(const TlvMap& fields, TlvType type, QString& output) {
    std::string value;
    const auto status = getString(fields, type, value);
    if (!status.isOk()) {
        output.clear();
        return status;
    }
    output = QString::fromUtf8(value.data(), static_cast<int>(value.size()));
    return Status::ok();
}

Status PacketCodec::getUint64Field(const TlvMap& fields, TlvType type, std::uint64_t& output) {
    return getUint64(fields, type, output);
}

Status PacketCodec::parseFields(const Packet& packet, TlvMap& output) {
    return parseTlvMap(packet.body, output);
}

// 把从 QTcpSocket 收到的QByteArray字节喂给 FrameDecoder 来解析成 Packet
Status PacketCodec::feed(const QByteArray& bytes, std::vector<Packet>& output) {
    return decoder_.feed(reinterpret_cast<const Byte*>(bytes.constData()),
                         static_cast<std::size_t>(bytes.size()), output);
}

// 重置 FrameDecoder 的状态，丢弃之前缓存的字节
void PacketCodec::reset() {
    decoder_.reset();
}

}  // namespace liteim::client
