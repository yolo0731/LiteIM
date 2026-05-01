#include "TestUtil.hpp"

#include "liteim/protocol/MessageType.hpp"
#include "liteim/protocol/Packet.hpp"

#include <arpa/inet.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using liteim::protocol::MsgType;
using liteim::protocol::Packet;
using liteim::protocol::PacketHeader;
using liteim::protocol::encodePacket;
using liteim::protocol::kMaxBodyLength;
using liteim::protocol::kPacketHeaderSize;
using liteim::protocol::kPacketMagic;
using liteim::protocol::kPacketVersion;
using liteim::protocol::parseHeader;
using liteim::protocol::toUint16;
using liteim::tests::TestCase;
using liteim::tests::expect;

void writeUint16(std::string& data, std::size_t offset, std::uint16_t value) {
    const std::uint16_t network_value = htons(value);
    const auto* bytes = reinterpret_cast<const char*>(&network_value);
    data.replace(offset, sizeof(network_value), bytes, sizeof(network_value));
}

void writeUint32(std::string& data, std::size_t offset, std::uint32_t value) {
    const std::uint32_t network_value = htonl(value);
    const auto* bytes = reinterpret_cast<const char*>(&network_value);
    data.replace(offset, sizeof(network_value), bytes, sizeof(network_value));
}

Packet makeHeartbeatPacket() {
    Packet packet;
    packet.header.msg_type = toUint16(MsgType::HEARTBEAT_REQ);
    packet.header.seq_id = 42;
    packet.body = R"({"ping":true})";
    return packet;
}

void testEncodePacketNormal() {
    const Packet packet = makeHeartbeatPacket();
    const std::string encoded = encodePacket(packet);

    expect(encoded.size() == kPacketHeaderSize + packet.body.size(), "encoded size mismatch");
    expect(encoded.substr(kPacketHeaderSize) == packet.body, "encoded body mismatch");
}

void testParseHeaderFields() {
    const Packet packet = makeHeartbeatPacket();
    const std::string encoded = encodePacket(packet);
    const auto header = parseHeader(encoded.data(), encoded.size());

    expect(header.has_value(), "valid header should parse");
    expect(header->magic == kPacketMagic, "magic mismatch");
    expect(header->version == kPacketVersion, "version mismatch");
    expect(header->msg_type == toUint16(MsgType::HEARTBEAT_REQ), "msg_type mismatch");
    expect(header->seq_id == 42, "seq_id mismatch");
    expect(header->body_len == packet.body.size(), "body_len mismatch");
}

void testWrongMagicFails() {
    std::string encoded = encodePacket(makeHeartbeatPacket());
    writeUint32(encoded, 0, 0x12345678);

    expect(!parseHeader(encoded.data(), encoded.size()).has_value(), "wrong magic should fail");
}

void testWrongVersionFails() {
    std::string encoded = encodePacket(makeHeartbeatPacket());
    writeUint16(encoded, 4, 2);

    expect(!parseHeader(encoded.data(), encoded.size()).has_value(), "wrong version should fail");
}

void testOversizedBodyLenFails() {
    std::string encoded = encodePacket(makeHeartbeatPacket());
    writeUint32(encoded, 12, kMaxBodyLength + 1);

    expect(!parseHeader(encoded.data(), encoded.size()).has_value(), "oversized body_len should fail");
}

void testShortHeaderFails() {
    const std::string short_header(kPacketHeaderSize - 1, '\0');

    expect(!parseHeader(short_header.data(), short_header.size()).has_value(), "short header should fail");
}

void testEncodeOversizedBodyThrows() {
    Packet packet;
    packet.header.msg_type = toUint16(MsgType::PRIVATE_MSG_REQ);
    packet.body.assign(kMaxBodyLength + 1, 'x');

    bool thrown = false;
    try {
        (void)encodePacket(packet);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(thrown, "encoding oversized body should throw");
}

}  // namespace

std::vector<TestCase> protocolTests() {
    return {
        {"protocol encode packet normal", testEncodePacketNormal},
        {"protocol parse header fields", testParseHeaderFields},
        {"protocol wrong magic fails", testWrongMagicFails},
        {"protocol wrong version fails", testWrongVersionFails},
        {"protocol oversized body_len fails", testOversizedBodyLenFails},
        {"protocol short header fails", testShortHeaderFails},
        {"protocol encode oversized body throws", testEncodeOversizedBodyThrows},
    };
}
