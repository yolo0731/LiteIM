#include "liteim/net/Buffer.hpp"

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

TEST(BufferTest, DefaultBufferHasNoReadableBytes) {
    liteim::Buffer buffer;

    EXPECT_EQ(buffer.readableBytes(), 0U);
    EXPECT_EQ(buffer.writableBytes(), liteim::kDefaultBufferSize);
}

TEST(BufferTest, AppendIncreasesReadableBytes) {
    liteim::Buffer buffer(8);

    const auto status = buffer.append("abc", 3);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 3U);
    EXPECT_EQ(buffer.writableBytes(), 5U);
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "abc");
}

TEST(BufferTest, AppendStringStoresReadableData) {
    liteim::Buffer buffer(8);

    buffer.appendString("hello");

    EXPECT_EQ(buffer.readableBytes(), 5U);
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "hello");
}

TEST(BufferTest, AppendUint8PointerStoresBytes) {
    liteim::Buffer buffer(8);
    const std::uint8_t bytes[] = {'o', 'k'};

    const auto status = buffer.append(bytes, 2);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.retrieveAllAsString(), "ok");
}

TEST(BufferTest, RetrieveAdvancesReadIndex) {
    liteim::Buffer buffer(8);
    buffer.appendString("abcdef");

    const auto status = buffer.retrieve(2);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 4U);
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "cdef");
}

TEST(BufferTest, RetrieveAllResetsBuffer) {
    liteim::Buffer buffer(8);
    buffer.appendString("abc");

    buffer.retrieveAll();

    EXPECT_EQ(buffer.readableBytes(), 0U);
    EXPECT_EQ(buffer.writableBytes(), 8U);
}

TEST(BufferTest, RetrieveAllAsStringReturnsReadableDataAndClearsBuffer) {
    liteim::Buffer buffer(8);
    buffer.appendString("abc");

    const auto result = buffer.retrieveAllAsString();

    EXPECT_EQ(result, "abc");
    EXPECT_EQ(buffer.readableBytes(), 0U);
    EXPECT_EQ(buffer.writableBytes(), 8U);
}

TEST(BufferTest, EnsureWritableBytesExpandsWhenNeeded) {
    liteim::Buffer buffer(4);

    buffer.ensureWritableBytes(10);

    EXPECT_GE(buffer.writableBytes(), 10U);
}

TEST(BufferTest, EnsureWritableBytesCompactsReadableDataBeforeExpanding) {
    liteim::Buffer buffer(8);
    buffer.appendString("abcdef");
    ASSERT_TRUE(buffer.retrieve(4).isOk());

    buffer.ensureWritableBytes(6);

    EXPECT_EQ(buffer.readableBytes(), 2U);
    EXPECT_GE(buffer.writableBytes(), 6U);
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "ef");
}

TEST(BufferTest, AppendExpandsAndPreservesExistingData) {
    liteim::Buffer buffer(4);
    buffer.appendString("abcd");

    const auto status = buffer.append("efgh", 4);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 8U);
    EXPECT_EQ(buffer.retrieveAllAsString(), "abcdefgh");
}

TEST(BufferTest, RetrievePastReadableBytesReturnsError) {
    liteim::Buffer buffer(8);
    buffer.appendString("abc");

    const auto status = buffer.retrieve(4);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(buffer.readableBytes(), 3U);
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "abc");
}

TEST(BufferTest, NullAppendWithNonzeroLengthReturnsError) {
    liteim::Buffer buffer(8);

    const auto status = buffer.append(static_cast<const char*>(nullptr), 1);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(buffer.readableBytes(), 0U);
}

TEST(BufferTest, NullAppendWithZeroLengthIsOk) {
    liteim::Buffer buffer(8);

    const auto status = buffer.append(static_cast<const char*>(nullptr), 0);

    EXPECT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 0U);
}
