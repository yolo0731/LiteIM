#include "liteim/protocol/Packet.hpp"

#include "liteim/protocol/ByteOrder.hpp"

namespace liteim {
Status validateHeader(const PacketHeader& header) {
    if (header.magic != kPacketMagic) {
        return Status::error(ErrorCode::ParseError, "invalid packet magic");
    }
    if (header.version != kPacketVersion) {
        return Status::error(ErrorCode::ParseError, "unsupported packet version");
    }
    if (header.body_len > kMaxPacketBodyLength) {
        return Status::error(ErrorCode::ParseError, "packet body is too large");
    }
    return Status::ok();
}

Status encodePacket(const Packet& packet, Bytes& output) {
    output.clear();

    if (packet.body.size() > kMaxPacketBodyLength) {
        return Status::error(ErrorCode::InvalidArgument, "packet body is too large");
    }

    PacketHeader header = packet.header;
    header.body_len = static_cast<std::uint32_t>(packet.body.size());

    const auto status = validateHeader(header);
    if (!status.isOk()) {
        return status;
    }

    output.reserve(kPacketHeaderSize + packet.body.size());
    appendUint32BE(output, header.magic);
    output.push_back(header.version);
    output.push_back(header.flags);
    appendUint16BE(output, static_cast<std::uint16_t>(header.msg_type));
    appendUint64BE(output, header.seq_id);
    appendUint32BE(output, header.body_len);
    output.insert(output.end(), packet.body.begin(), packet.body.end());
    return Status::ok();
}

Status parseHeader(const Byte* data, std::size_t len, PacketHeader& output) {
    if (data == nullptr) {
        return Status::error(ErrorCode::InvalidArgument, "packet header data is null");
    }
    if (len < kPacketHeaderSize) {
        return Status::error(ErrorCode::ParseError, "packet header is incomplete");
    }

    PacketHeader header;
    header.magic = readUint32BE(data);
    header.version = data[4];
    header.flags = data[5];
    header.msg_type = static_cast<MessageType>(readUint16BE(data + 6));
    header.seq_id = readUint64BE(data + 8);
    header.body_len = readUint32BE(data + 16);

    const auto status = validateHeader(header);
    if (!status.isOk()) {
        return status;
    }

    output = header;
    return Status::ok();
}

}  // namespace liteim
