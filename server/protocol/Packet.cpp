#include "protocol/Packet.hpp"

#include <arpa/inet.h>

#include <cstring>
#include <stdexcept>

namespace liteim::protocol {
namespace {

void appendUint16(std::string& output, std::uint16_t value) {
    const auto network_value = htons(value);
    const auto* bytes = reinterpret_cast<const char*>(&network_value);
    output.append(bytes, sizeof(network_value));
}

void appendUint32(std::string& output, std::uint32_t value) {
    const auto network_value = htonl(value);
    const auto* bytes = reinterpret_cast<const char*>(&network_value);
    output.append(bytes, sizeof(network_value));
}

std::uint16_t readUint16(const char* data) {
    std::uint16_t network_value = 0;
    std::memcpy(&network_value, data, sizeof(network_value));
    return ntohs(network_value);
}

std::uint32_t readUint32(const char* data) {
    std::uint32_t network_value = 0;
    std::memcpy(&network_value, data, sizeof(network_value));
    return ntohl(network_value);
}

}  // namespace

std::string encodePacket(const Packet& packet) {
    if (packet.body.size() > kMaxBodyLength) {
        throw std::invalid_argument("packet body exceeds max length");
    }

    PacketHeader header = packet.header;
    header.magic = kPacketMagic;
    header.version = kPacketVersion;
    header.body_len = static_cast<std::uint32_t>(packet.body.size());

    std::string output;
    output.reserve(kPacketHeaderSize + packet.body.size());
    appendUint32(output, header.magic);
    appendUint16(output, header.version);
    appendUint16(output, header.msg_type);
    appendUint32(output, header.seq_id);
    appendUint32(output, header.body_len);
    output.append(packet.body);

    return output;
}

std::optional<PacketHeader> parseHeader(const char* data, std::size_t len) {
    if (data == nullptr || len < kPacketHeaderSize) {
        return std::nullopt;
    }

    PacketHeader header;
    header.magic = readUint32(data);
    header.version = readUint16(data + 4);
    header.msg_type = readUint16(data + 6);
    header.seq_id = readUint32(data + 8);
    header.body_len = readUint32(data + 12);

    if (header.magic != kPacketMagic) {
        return std::nullopt;
    }
    if (header.version != kPacketVersion) {
        return std::nullopt;
    }
    if (header.body_len > kMaxBodyLength) {
        return std::nullopt;
    }

    return header;
}

}  // namespace liteim::protocol

