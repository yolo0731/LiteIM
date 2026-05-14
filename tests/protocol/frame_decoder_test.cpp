#include "liteim/protocol/FrameDecoder.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

namespace {

liteim::Bytes bytesFromString(const std::string& value) {
    return {value.begin(), value.end()};
}

liteim::Bytes makeEncodedPacket(liteim::MessageType type, std::uint64_t seq_id,
                                const std::string& body_text) {
    liteim::Packet packet;
    packet.header.msg_type = type;
    packet.header.seq_id = seq_id;
    packet.body = bytesFromString(body_text);

    liteim::Bytes encoded;
    const auto status = liteim::encodePacket(packet, encoded);
    EXPECT_TRUE(status.isOk()) << status.message();
    return encoded;
}

}  // namespace

TEST(FrameDecoderTest, CompletePacketEmitsOnePacket) {
    liteim::FrameDecoder decoder;
    const auto encoded = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 7, "hello");
    std::vector<liteim::Packet> packets;

    const auto status = decoder.feed(encoded, packets);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_EQ(packets.size(), 1U);
    EXPECT_EQ(packets[0].header.msg_type, liteim::MessageType::PrivateMessageRequest);
    EXPECT_EQ(packets[0].header.seq_id, 7U);
    EXPECT_EQ(packets[0].body, bytesFromString("hello"));
    EXPECT_EQ(decoder.bufferedBytes(), 0U);
}

TEST(FrameDecoderTest, PacketSplitAcrossFeedsEmitsAfterSecondFeed) {
    liteim::FrameDecoder decoder;
    const auto encoded = makeEncodedPacket(liteim::MessageType::LoginRequest, 8, "alice");
    std::vector<liteim::Packet> packets;

    const auto first_status = decoder.feed(encoded.data(), 5, packets);

    ASSERT_TRUE(first_status.isOk()) << first_status.message();
    EXPECT_TRUE(packets.empty());
    EXPECT_EQ(decoder.bufferedBytes(), 5U);

    const auto second_status = decoder.feed(encoded.data() + 5, encoded.size() - 5, packets);

    ASSERT_TRUE(second_status.isOk()) << second_status.message();
    ASSERT_EQ(packets.size(), 1U);
    EXPECT_EQ(packets[0].header.msg_type, liteim::MessageType::LoginRequest);
    EXPECT_EQ(packets[0].body, bytesFromString("alice"));
    EXPECT_EQ(decoder.bufferedBytes(), 0U);
}

TEST(FrameDecoderTest, MultiplePacketsInOneFeedAreDecoded) {
    liteim::FrameDecoder decoder;
    auto first = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 1, "one");
    const auto second = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 2, "two");
    first.insert(first.end(), second.begin(), second.end());
    std::vector<liteim::Packet> packets;

    const auto status = decoder.feed(first, packets);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_EQ(packets.size(), 2U);
    EXPECT_EQ(packets[0].header.seq_id, 1U);
    EXPECT_EQ(packets[0].body, bytesFromString("one"));
    EXPECT_EQ(packets[1].header.seq_id, 2U);
    EXPECT_EQ(packets[1].body, bytesFromString("two"));
}

TEST(FrameDecoderTest, ManySmallPacketsInOneFeedAreDecoded) {
    liteim::FrameDecoder decoder;
    constexpr std::size_t kPacketCount = 4096;
    const auto encoded = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 1, "x");
    liteim::Bytes stream;
    stream.reserve(encoded.size() * kPacketCount);
    for (std::size_t index = 0; index < kPacketCount; ++index) {
        stream.insert(stream.end(), encoded.begin(), encoded.end());
    }
    std::vector<liteim::Packet> packets;

    const auto status = decoder.feed(stream, packets);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_EQ(packets.size(), kPacketCount);
    EXPECT_EQ(decoder.bufferedBytes(), 0U);
    EXPECT_EQ(packets.front().body, bytesFromString("x"));
    EXPECT_EQ(packets.back().body, bytesFromString("x"));
}

TEST(FrameDecoderTest, HalfPacketThenStickyPacketAreDecodedTogether) {
    liteim::FrameDecoder decoder;
    const auto first = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 11, "first");
    const auto second = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 12, "second");
    const std::size_t split = liteim::kPacketHeaderSize + 2;
    std::vector<liteim::Packet> packets;

    const auto first_status = decoder.feed(first.data(), split, packets);

    ASSERT_TRUE(first_status.isOk()) << first_status.message();
    EXPECT_TRUE(packets.empty());

    liteim::Bytes rest;
    rest.insert(rest.end(), first.begin() + static_cast<std::ptrdiff_t>(split), first.end());
    rest.insert(rest.end(), second.begin(), second.end());
    const auto second_status = decoder.feed(rest, packets);

    ASSERT_TRUE(second_status.isOk()) << second_status.message();
    ASSERT_EQ(packets.size(), 2U);
    EXPECT_EQ(packets[0].header.seq_id, 11U);
    EXPECT_EQ(packets[0].body, bytesFromString("first"));
    EXPECT_EQ(packets[1].header.seq_id, 12U);
    EXPECT_EQ(packets[1].body, bytesFromString("second"));
}

TEST(FrameDecoderTest, InvalidMagicEntersErrorState) {
    liteim::FrameDecoder decoder;
    auto encoded = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 1, "bad");
    encoded[0] = 0;
    std::vector<liteim::Packet> packets;

    const auto status = decoder.feed(encoded, packets);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
    EXPECT_TRUE(decoder.hasError());
    EXPECT_TRUE(packets.empty());
}

TEST(FrameDecoderTest, InvalidVersionEntersErrorState) {
    liteim::FrameDecoder decoder;
    auto encoded = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 1, "bad");
    encoded[4] = 2;
    std::vector<liteim::Packet> packets;

    const auto status = decoder.feed(encoded, packets);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
    EXPECT_TRUE(decoder.hasError());
}

TEST(FrameDecoderTest, OversizedBodyLengthEntersErrorState) {
    liteim::FrameDecoder decoder;
    auto encoded = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 1, "bad");
    encoded[16] = 0x00;
    encoded[17] = 0x10;
    encoded[18] = 0x00;
    encoded[19] = 0x01;
    std::vector<liteim::Packet> packets;

    const auto status = decoder.feed(encoded, packets);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
    EXPECT_TRUE(decoder.hasError());
}

TEST(FrameDecoderTest, ErrorStateRejectsFurtherFeedUntilReset) {
    liteim::FrameDecoder decoder;
    auto invalid = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 1, "bad");
    invalid[0] = 0;
    std::vector<liteim::Packet> packets;
    ASSERT_FALSE(decoder.feed(invalid, packets).isOk());

    const auto valid = makeEncodedPacket(liteim::MessageType::PrivateMessageRequest, 2, "ok");
    const auto rejected_status = decoder.feed(valid, packets);

    EXPECT_FALSE(rejected_status.isOk());
    EXPECT_EQ(rejected_status.code(), liteim::ErrorCode::ParseError);
    EXPECT_TRUE(packets.empty());

    decoder.reset();
    const auto reset_status = decoder.feed(valid, packets);

    ASSERT_TRUE(reset_status.isOk()) << reset_status.message();
    ASSERT_EQ(packets.size(), 1U);
    EXPECT_EQ(packets[0].header.seq_id, 2U);
    EXPECT_FALSE(decoder.hasError());
}

TEST(FrameDecoderTest, NullInputWithNonzeroLengthReturnsError) {
    liteim::FrameDecoder decoder;
    std::vector<liteim::Packet> packets;

    const auto status = decoder.feed(nullptr, 1, packets);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_FALSE(decoder.hasError());
}
