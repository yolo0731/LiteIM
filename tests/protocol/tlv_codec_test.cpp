#include "liteim/protocol/TlvCodec.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

liteim::TlvMap parseBody(const std::vector<std::uint8_t>& body) {
    liteim::TlvMap map;
    const auto status = liteim::parseTlvMap(body, map);
    EXPECT_TRUE(status.isOk()) << status.message();
    return map;
}

}  // namespace

TEST(TlvCodecTest, StringFieldCanBeEncodedAndDecoded) {
    std::vector<std::uint8_t> body;

    const auto append_status = liteim::appendString(liteim::TlvType::Username, "alice", body);

    ASSERT_TRUE(append_status.isOk()) << append_status.message();
    ASSERT_EQ(body.size(), liteim::kTlvHeaderSize + 5U);

    const auto map = parseBody(body);
    std::string username;
    const auto get_status = liteim::getString(map, liteim::TlvType::Username, username);

    ASSERT_TRUE(get_status.isOk()) << get_status.message();
    EXPECT_EQ(username, "alice");
}

TEST(TlvCodecTest, MultipleFieldsCanBeEncodedAndDecoded) {
    std::vector<std::uint8_t> body;

    ASSERT_TRUE(liteim::appendString(liteim::TlvType::Username, "alice", body).isOk());
    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::UserId, 1001, body).isOk());
    ASSERT_TRUE(liteim::appendString(liteim::TlvType::MessageText, "hello", body).isOk());

    const auto map = parseBody(body);
    std::string username;
    std::string text;
    std::uint64_t user_id = 0;

    ASSERT_TRUE(liteim::getString(map, liteim::TlvType::Username, username).isOk());
    ASSERT_TRUE(liteim::getUint64(map, liteim::TlvType::UserId, user_id).isOk());
    ASSERT_TRUE(liteim::getString(map, liteim::TlvType::MessageText, text).isOk());

    EXPECT_EQ(username, "alice");
    EXPECT_EQ(user_id, 1001U);
    EXPECT_EQ(text, "hello");
}

TEST(TlvCodecTest, Utf8StringCanBeEncodedAndDecoded) {
    std::vector<std::uint8_t> body;

    const auto append_status = liteim::appendString(liteim::TlvType::MessageText,
                                                    "你好，LiteIM 👋",
                                                    body);

    ASSERT_TRUE(append_status.isOk()) << append_status.message();

    const auto map = parseBody(body);
    std::string text;
    const auto get_status = liteim::getString(map, liteim::TlvType::MessageText, text);

    ASSERT_TRUE(get_status.isOk()) << get_status.message();
    EXPECT_EQ(text, "你好，LiteIM 👋");
}

TEST(TlvCodecTest, RepeatedStringFieldsArePreserved) {
    std::vector<std::uint8_t> body;

    ASSERT_TRUE(liteim::appendString(liteim::TlvType::GroupName, "dev", body).isOk());
    ASSERT_TRUE(liteim::appendString(liteim::TlvType::GroupName, "study", body).isOk());

    const auto map = parseBody(body);
    std::vector<std::string> names;
    const auto get_status = liteim::getRepeatedString(map, liteim::TlvType::GroupName, names);

    ASSERT_TRUE(get_status.isOk()) << get_status.message();
    ASSERT_EQ(names.size(), 2U);
    EXPECT_EQ(names[0], "dev");
    EXPECT_EQ(names[1], "study");
}

TEST(TlvCodecTest, RepeatedUint64FieldsArePreserved) {
    std::vector<std::uint8_t> body;

    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::FriendId, 1001, body).isOk());
    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::FriendId, 1002, body).isOk());
    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::FriendId, 1003, body).isOk());

    const auto map = parseBody(body);
    std::vector<std::uint64_t> friend_ids;
    const auto get_status = liteim::getRepeatedUint64(map, liteim::TlvType::FriendId, friend_ids);

    ASSERT_TRUE(get_status.isOk()) << get_status.message();
    ASSERT_EQ(friend_ids.size(), 3U);
    EXPECT_EQ(friend_ids[0], 1001U);
    EXPECT_EQ(friend_ids[1], 1002U);
    EXPECT_EQ(friend_ids[2], 1003U);
}

TEST(TlvCodecTest, Uint64UsesNetworkByteOrder) {
    std::vector<std::uint8_t> body;

    const auto append_status = liteim::appendUint64(liteim::TlvType::MessageId,
                                                    0x0102030405060708ULL,
                                                    body);

    ASSERT_TRUE(append_status.isOk()) << append_status.message();
    ASSERT_EQ(body.size(), liteim::kTlvHeaderSize + 8U);
    EXPECT_EQ(body[0], 0x00);
    EXPECT_EQ(body[1], 0x2A);
    EXPECT_EQ(body[2], 0x00);
    EXPECT_EQ(body[3], 0x00);
    EXPECT_EQ(body[4], 0x00);
    EXPECT_EQ(body[5], 0x08);
    EXPECT_EQ(body[6], 0x01);
    EXPECT_EQ(body[7], 0x02);
    EXPECT_EQ(body[8], 0x03);
    EXPECT_EQ(body[9], 0x04);
    EXPECT_EQ(body[10], 0x05);
    EXPECT_EQ(body[11], 0x06);
    EXPECT_EQ(body[12], 0x07);
    EXPECT_EQ(body[13], 0x08);

    const auto map = parseBody(body);
    std::uint64_t message_id = 0;
    ASSERT_TRUE(liteim::getUint64(map, liteim::TlvType::MessageId, message_id).isOk());
    EXPECT_EQ(message_id, 0x0102030405060708ULL);
}

TEST(TlvCodecTest, TlvLengthOutOfBoundsReturnsError) {
    std::vector<std::uint8_t> body;
    ASSERT_TRUE(liteim::appendString(liteim::TlvType::Username, "a", body).isOk());
    body[5] = 0x02;

    liteim::TlvMap map;
    const auto status = liteim::parseTlvMap(body, map);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
}

TEST(TlvCodecTest, IncompleteTlvHeaderReturnsError) {
    const std::vector<std::uint8_t> body(liteim::kTlvHeaderSize - 1, 0);
    liteim::TlvMap map;

    const auto status = liteim::parseTlvMap(body, map);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
}

TEST(TlvCodecTest, MissingStringFieldReturnsError) {
    const liteim::TlvMap map;
    std::string username;

    const auto status = liteim::getString(map, liteim::TlvType::Username, username);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::NotFound);
}

TEST(TlvCodecTest, MissingUint64FieldReturnsError) {
    const liteim::TlvMap map;
    std::uint64_t user_id = 0;

    const auto status = liteim::getUint64(map, liteim::TlvType::UserId, user_id);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::NotFound);
}

TEST(TlvCodecTest, WrongUint64LengthReturnsError) {
    std::vector<std::uint8_t> body;
    ASSERT_TRUE(liteim::appendString(liteim::TlvType::UserId, "bad", body).isOk());
    const auto map = parseBody(body);
    std::uint64_t user_id = 0;

    const auto status = liteim::getUint64(map, liteim::TlvType::UserId, user_id);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);
}

TEST(TlvCodecTest, UnknownTypeCannotBeEncoded) {
    std::vector<std::uint8_t> body;

    const auto status = liteim::appendString(liteim::TlvType::Unknown, "bad", body);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_TRUE(body.empty());
}
