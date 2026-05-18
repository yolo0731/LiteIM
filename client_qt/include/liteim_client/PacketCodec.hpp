#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <vector>

namespace liteim::client {

class PacketCodec {
public:
    static Status encode(const Packet& packet, QByteArray& output);

    static Status appendStringField(TlvType type, const QString& value, Packet& packet);
    static Status appendUint64Field(TlvType type, std::uint64_t value, Packet& packet);

    static Status getStringField(const TlvMap& fields, TlvType type, QString& output);
    static Status getUint64Field(const TlvMap& fields, TlvType type, std::uint64_t& output);
    static Status parseFields(const Packet& packet, TlvMap& output);

    Status feed(const QByteArray& bytes, std::vector<Packet>& output);
    void reset();

private:
    FrameDecoder decoder_;
};

}  // namespace liteim::client
