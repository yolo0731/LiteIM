#include "liteim/protocol/Packet.hpp"

#include <cstddef>
#include <cstdint>

namespace liteim {
namespace {

void appendUint16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void appendUint32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void appendUint64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFULL));
    }
}

std::uint16_t readUint16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                      static_cast<std::uint16_t>(data[1]));
}

std::uint32_t readUint32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint64_t readUint64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | static_cast<std::uint64_t>(data[index]);
    }
    return value;
}

}  // namespace

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

Status encodePacket(const Packet& packet, std::vector<std::uint8_t>& output) {
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
    appendUint32(output, header.magic);
    output.push_back(header.version);
    output.push_back(header.flags);
    appendUint16(output, static_cast<std::uint16_t>(header.msg_type));
    appendUint64(output, header.seq_id);
    appendUint32(output, header.body_len);
    output.insert(output.end(), packet.body.begin(), packet.body.end());
    return Status::ok();
}

Status parseHeader(const std::uint8_t* data, std::size_t len, PacketHeader& output) {
    if (data == nullptr) {
        return Status::error(ErrorCode::InvalidArgument, "packet header data is null");
    }
    if (len < kPacketHeaderSize) {
        return Status::error(ErrorCode::ParseError, "packet header is incomplete");
    }

    PacketHeader header;
    header.magic = readUint32(data);
    header.version = data[4];
    header.flags = data[5];
    header.msg_type = static_cast<MessageType>(readUint16(data + 6));
    header.seq_id = readUint64(data + 8);
    header.body_len = readUint32(data + 16);

    const auto status = validateHeader(header);
    if (!status.isOk()) {
        return status;
    }

    output = header;
    return Status::ok();
}

}  // namespace liteim
