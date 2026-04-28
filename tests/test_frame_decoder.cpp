#include "TestUtil.hpp"

#include "protocol/FrameDecoder.hpp"
#include "protocol/MessageType.hpp"
#include "protocol/Packet.hpp"

#include <arpa/inet.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using liteim::protocol::FrameDecoder;
using liteim::protocol::MsgType;
using liteim::protocol::Packet;
using liteim::protocol::encodePacket;
using liteim::protocol::kMaxBodyLength;
using liteim::protocol::kPacketHeaderSize;
using liteim::protocol::toUint16;
using liteim::tests::TestCase;
using liteim::tests::expect;

Packet makePacket(MsgType type, std::uint32_t seq_id, std::string body) {
    Packet packet;
    packet.header.msg_type = toUint16(type);
    packet.header.seq_id = seq_id;
    packet.body = std::move(body);
    return packet;
}

void writeUint32(std::string& data, std::size_t offset, std::uint32_t value) {
    const std::uint32_t network_value = htonl(value);
    const auto* bytes = reinterpret_cast<const char*>(&network_value);
    data.replace(offset, sizeof(network_value), bytes, sizeof(network_value));
}

void testCompletePacket() {
    FrameDecoder decoder;
    const Packet input = makePacket(MsgType::PRIVATE_MSG_REQ, 1, R"({"content":"hello"})");
    const std::string encoded = encodePacket(input);

    const auto packets = decoder.feed(encoded.data(), encoded.size());

    expect(!decoder.hasError(), "decoder should not enter error state");
    expect(packets.size() == 1, "complete packet should produce one packet");
    expect(packets[0].header.msg_type == toUint16(MsgType::PRIVATE_MSG_REQ), "msg_type mismatch");
    expect(packets[0].header.seq_id == 1, "seq_id mismatch");
    expect(packets[0].body == input.body, "body mismatch");
    expect(decoder.bufferedBytes() == 0, "buffer should be empty after complete packet");
}

void testEmptyBodyPacket() {
    FrameDecoder decoder;
    const Packet input = makePacket(MsgType::HEARTBEAT_REQ, 2, "");
    const std::string encoded = encodePacket(input);

    const auto packets = decoder.feed(encoded.data(), encoded.size());

    expect(packets.size() == 1, "empty body packet should produce one packet");
    expect(packets[0].body.empty(), "decoded body should be empty");
    expect(packets[0].header.body_len == 0, "body_len should be zero");
}

void testPartialHeader() {
    FrameDecoder decoder;
    const std::string encoded = encodePacket(makePacket(MsgType::HEARTBEAT_REQ, 3, ""));

    const auto packets = decoder.feed(encoded.data(), kPacketHeaderSize - 1);

    expect(packets.empty(), "partial header should not produce packets");
    expect(!decoder.hasError(), "partial header should not be an error");
    expect(decoder.bufferedBytes() == kPacketHeaderSize - 1, "partial header should remain buffered");
}

void testCompleteHeaderIncompleteBody() {
    FrameDecoder decoder;
    const Packet input = makePacket(MsgType::PRIVATE_MSG_REQ, 4, R"({"content":"hello"})");
    const std::string encoded = encodePacket(input);

    const auto packets = decoder.feed(encoded.data(), kPacketHeaderSize + 3);

    expect(packets.empty(), "incomplete body should not produce packets");
    expect(!decoder.hasError(), "incomplete body should not be an error");
    expect(decoder.bufferedBytes() == kPacketHeaderSize + 3, "incomplete frame should remain buffered");
}

void testTwoCompletePackets() {
    FrameDecoder decoder;
    const Packet first = makePacket(MsgType::PRIVATE_MSG_REQ, 5, R"({"content":"one"})");
    const Packet second = makePacket(MsgType::GROUP_MSG_REQ, 6, R"({"content":"two"})");
    const std::string encoded = encodePacket(first) + encodePacket(second);

    const auto packets = decoder.feed(encoded.data(), encoded.size());

    expect(packets.size() == 2, "sticky packets should produce two packets");
    expect(packets[0].header.seq_id == 5, "first seq_id mismatch");
    expect(packets[0].body == first.body, "first body mismatch");
    expect(packets[1].header.seq_id == 6, "second seq_id mismatch");
    expect(packets[1].body == second.body, "second body mismatch");
    expect(decoder.bufferedBytes() == 0, "buffer should be empty after two packets");
}

void testPartialThenRemaining() {
    FrameDecoder decoder;
    const Packet input = makePacket(MsgType::PRIVATE_MSG_REQ, 7, R"({"content":"split"})");
    const std::string encoded = encodePacket(input);
    const std::size_t split_at = 5;

    const auto first = decoder.feed(encoded.data(), split_at);
    const auto second = decoder.feed(encoded.data() + split_at, encoded.size() - split_at);

    expect(first.empty(), "first partial feed should not produce packets");
    expect(second.size() == 1, "second feed should complete one packet");
    expect(second[0].body == input.body, "decoded body mismatch after split feed");
}

void testPartialAndStickyMixed() {
    FrameDecoder decoder;
    const Packet first = makePacket(MsgType::PRIVATE_MSG_REQ, 8, R"({"content":"first"})");
    const Packet second = makePacket(MsgType::GROUP_MSG_REQ, 9, R"({"content":"second"})");
    const std::string combined = encodePacket(first) + encodePacket(second);
    const std::size_t split_at = kPacketHeaderSize + first.body.size() + 4;

    const auto first_feed = decoder.feed(combined.data(), split_at);
    const auto second_feed = decoder.feed(combined.data() + split_at, combined.size() - split_at);

    expect(first_feed.size() == 1, "first feed should produce first packet only");
    expect(first_feed[0].body == first.body, "first packet body mismatch");
    expect(second_feed.size() == 1, "second feed should complete second packet");
    expect(second_feed[0].body == second.body, "second packet body mismatch");
}

void testWrongMagicSetsError() {
    FrameDecoder decoder;
    std::string encoded = encodePacket(makePacket(MsgType::HEARTBEAT_REQ, 10, ""));
    writeUint32(encoded, 0, 0x12345678);

    const auto packets = decoder.feed(encoded.data(), encoded.size());

    expect(packets.empty(), "wrong magic should not produce packets");
    expect(decoder.hasError(), "wrong magic should set error");
    expect(decoder.bufferedBytes() == 0, "error should clear buffer");
}

void testOversizedBodyLenSetsError() {
    FrameDecoder decoder;
    std::string encoded = encodePacket(makePacket(MsgType::HEARTBEAT_REQ, 11, ""));
    writeUint32(encoded, 12, kMaxBodyLength + 1);

    const auto packets = decoder.feed(encoded.data(), encoded.size());

    expect(packets.empty(), "oversized body_len should not produce packets");
    expect(decoder.hasError(), "oversized body_len should set error");
}

void testResetAfterError() {
    FrameDecoder decoder;
    std::string bad = encodePacket(makePacket(MsgType::HEARTBEAT_REQ, 12, ""));
    writeUint32(bad, 0, 0x12345678);
    (void)decoder.feed(bad.data(), bad.size());
    expect(decoder.hasError(), "bad packet should set error before reset");

    decoder.reset();
    const Packet good_packet = makePacket(MsgType::HEARTBEAT_REQ, 13, "");
    const std::string good = encodePacket(good_packet);
    const auto packets = decoder.feed(good.data(), good.size());

    expect(!decoder.hasError(), "reset should clear error state");
    expect(packets.size() == 1, "decoder should work after reset");
    expect(packets[0].header.seq_id == 13, "seq_id mismatch after reset");
}

}  // namespace

std::vector<TestCase> frameDecoderTests() {
    return {
        {"frame decoder complete packet", testCompletePacket},
        {"frame decoder empty body packet", testEmptyBodyPacket},
        {"frame decoder partial header", testPartialHeader},
        {"frame decoder complete header incomplete body", testCompleteHeaderIncompleteBody},
        {"frame decoder two complete packets", testTwoCompletePackets},
        {"frame decoder partial then remaining", testPartialThenRemaining},
        {"frame decoder partial and sticky mixed", testPartialAndStickyMixed},
        {"frame decoder wrong magic sets error", testWrongMagicSetsError},
        {"frame decoder oversized body_len sets error", testOversizedBodyLenSetsError},
        {"frame decoder reset after error", testResetAfterError},
    };
}

