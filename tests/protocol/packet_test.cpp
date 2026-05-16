#include "liteim/protocol/Packet.hpp"

#include <cstdint>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace {

liteim::Bytes bytesFromString(const std::string& value) {
    return {value.begin(), value.end()};
}

liteim::Packet makePacket(liteim::Bytes body) {
    liteim::Packet packet;
    packet.header.msg_type = liteim::MessageType::PrivateMessageRequest;
    packet.header.seq_id = 42;
    packet.body = std::move(body);
    return packet;
}

}  // namespace

TEST(PacketTest, EncodePacketThenParseHeader) {
    const auto body = bytesFromString("hello");
    const auto packet = makePacket(body);
    liteim::Bytes encoded;

    const auto encode_status = liteim::encodePacket(packet, encoded);

    ASSERT_TRUE(encode_status.isOk()) << encode_status.message();
    ASSERT_EQ(encoded.size(), liteim::kPacketHeaderSize + body.size());

    liteim::PacketHeader header;
    const auto parse_status = liteim::parseHeader(encoded.data(), encoded.size(), header);

    ASSERT_TRUE(parse_status.isOk()) << parse_status.message();
    EXPECT_EQ(header.magic, liteim::kPacketMagic);
    EXPECT_EQ(header.version, liteim::kPacketVersion);
    EXPECT_EQ(header.flags, liteim::kPacketFlagsNone);
    EXPECT_EQ(header.msg_type, liteim::MessageType::PrivateMessageRequest);
    EXPECT_EQ(header.seq_id, 42U);
    EXPECT_EQ(header.body_len, body.size());

    const liteim::Bytes parsed_body{encoded.begin() + liteim::kPacketHeaderSize, encoded.end()};
    EXPECT_EQ(parsed_body, body);
}

TEST(PacketTest, EmptyBodyCanBeEncoded) {
    const auto packet = makePacket({});
    liteim::Bytes encoded;

    const auto encode_status = liteim::encodePacket(packet, encoded);

    ASSERT_TRUE(encode_status.isOk()) << encode_status.message();
    EXPECT_EQ(encoded.size(), liteim::kPacketHeaderSize);

    liteim::PacketHeader header;
    const auto parse_status = liteim::parseHeader(encoded.data(), encoded.size(), header);

    ASSERT_TRUE(parse_status.isOk()) << parse_status.message();
    EXPECT_EQ(header.body_len, 0U);
}

TEST(PacketTest, Utf8BodyCanBeEncoded) {
    const auto body = bytesFromString("你好，LiteIM 👋");
    const auto packet = makePacket(body);
    liteim::Bytes encoded;

    const auto encode_status = liteim::encodePacket(packet, encoded);

    ASSERT_TRUE(encode_status.isOk()) << encode_status.message();

    liteim::PacketHeader header;
    const auto parse_status = liteim::parseHeader(encoded.data(), encoded.size(), header);

    ASSERT_TRUE(parse_status.isOk()) << parse_status.message();
    EXPECT_EQ(header.body_len, body.size());

    const std::string parsed_body{encoded.begin() + liteim::kPacketHeaderSize, encoded.end()};
    EXPECT_EQ(parsed_body, "你好，LiteIM 👋");
}

TEST(PacketTest, HeaderUsesNetworkByteOrder) {
    auto packet = makePacket(bytesFromString("x"));
    packet.header.msg_type = liteim::MessageType::GroupMessageRequest;
    packet.header.seq_id = 0x0102030405060708ULL;
    liteim::Bytes encoded;

    const auto encode_status = liteim::encodePacket(packet, encoded);

    ASSERT_TRUE(encode_status.isOk()) << encode_status.message();
    ASSERT_GE(encoded.size(), liteim::kPacketHeaderSize);

    EXPECT_EQ(encoded[0], 0x4C);
    EXPECT_EQ(encoded[1], 0x49);
    EXPECT_EQ(encoded[2], 0x4D);
    EXPECT_EQ(encoded[3], 0x31);
    EXPECT_EQ(encoded[4], liteim::kPacketVersion);
    EXPECT_EQ(encoded[5], liteim::kPacketFlagsNone);
    EXPECT_EQ(encoded[6], 0x01);
    EXPECT_EQ(encoded[7], 0x96);
    EXPECT_EQ(encoded[8], 0x01);
    EXPECT_EQ(encoded[9], 0x02);
    EXPECT_EQ(encoded[10], 0x03);
    EXPECT_EQ(encoded[11], 0x04);
    EXPECT_EQ(encoded[12], 0x05);
    EXPECT_EQ(encoded[13], 0x06);
    EXPECT_EQ(encoded[14], 0x07);
    EXPECT_EQ(encoded[15], 0x08);
    EXPECT_EQ(encoded[16], 0x00);
    EXPECT_EQ(encoded[17], 0x00);
    EXPECT_EQ(encoded[18], 0x00);
    EXPECT_EQ(encoded[19], 0x01);
}

TEST(PacketTest, InvalidMagicReturnsError) {
    auto header = liteim::PacketHeader{};
    header.magic = 0;

    const auto status = liteim::validateHeader(header);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
}

TEST(PacketTest, InvalidVersionReturnsError) {
    auto header = liteim::PacketHeader{};
    header.version = 2;

    const auto status = liteim::validateHeader(header);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
}

TEST(PacketTest, UnsupportedFlagsReturnError) {
    auto header = liteim::PacketHeader{};
    header.flags = 1;

    const auto status = liteim::validateHeader(header);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
}

TEST(PacketTest, OversizedBodyLengthReturnsError) {
    auto header = liteim::PacketHeader{};
    header.body_len = liteim::kMaxPacketBodyLength + 1;

    const auto status = liteim::validateHeader(header);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
}

TEST(PacketTest, EncodingOversizedBodyReturnsError) {
    auto packet = makePacket(liteim::Bytes(liteim::kMaxPacketBodyLength + 1, 'x'));
    liteim::Bytes encoded;

    const auto status = liteim::encodePacket(packet, encoded);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_TRUE(encoded.empty());
}

TEST(PacketTest, IncompleteHeaderReturnsError) {
    liteim::Bytes incomplete(liteim::kPacketHeaderSize - 1, 0);
    liteim::PacketHeader header;

    const auto status = liteim::parseHeader(incomplete.data(), incomplete.size(), header);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
}

TEST(PacketTest, NullHeaderDataReturnsError) {
    liteim::PacketHeader header;

    const auto status = liteim::parseHeader(nullptr, liteim::kPacketHeaderSize, header);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
}
