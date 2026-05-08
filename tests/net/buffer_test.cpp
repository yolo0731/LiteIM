#include "liteim/net/Buffer.hpp"

#include <string>

#include <gtest/gtest.h>

namespace {

std::string readableAsString(const liteim::Buffer& buffer) {
    return {reinterpret_cast<const char*>(buffer.peek()), buffer.readableBytes()};
}

}  // namespace

TEST(BufferTest, DefaultBufferHasNoReadableBytes) {
    liteim::Buffer buffer;

    EXPECT_EQ(buffer.readableBytes(), 0U);
    EXPECT_EQ(buffer.writableBytes(), liteim::kDefaultBufferSize);
}

TEST(BufferTest, AppendIncreasesReadableBytes) {
    liteim::Buffer buffer(8);

    const auto status = buffer.append(std::string{"abc"});

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 3U);
    EXPECT_EQ(buffer.writableBytes(), 5U);
    EXPECT_EQ(readableAsString(buffer), "abc");
}

TEST(BufferTest, AppendStringStoresReadableData) {
    liteim::Buffer buffer(8);

    const auto status = buffer.append(std::string{"hello"});

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 5U);
    EXPECT_EQ(readableAsString(buffer), "hello");
}

TEST(BufferTest, AppendBytePointerStoresBytes) {
    liteim::Buffer buffer(8);
    const liteim::Byte bytes[] = {'o', 'k'};

    const auto status = buffer.append(bytes, 2);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.retrieveAllAsString(), "ok");
}

TEST(BufferTest, RetrieveAdvancesReadIndex) {
    liteim::Buffer buffer(8);
    ASSERT_TRUE(buffer.append(std::string{"abcdef"}).isOk());

    const auto status = buffer.retrieve(2);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 4U);
    EXPECT_EQ(readableAsString(buffer), "cdef");
}

TEST(BufferTest, RetrieveAllResetsBuffer) {
    liteim::Buffer buffer(8);
    ASSERT_TRUE(buffer.append(std::string{"abc"}).isOk());

    buffer.retrieveAll();

    EXPECT_EQ(buffer.readableBytes(), 0U);
    EXPECT_EQ(buffer.writableBytes(), 8U);
}

TEST(BufferTest, RetrieveAllAsStringReturnsReadableDataAndClearsBuffer) {
    liteim::Buffer buffer(8);
    ASSERT_TRUE(buffer.append(std::string{"abc"}).isOk());

    const auto result = buffer.retrieveAllAsString();

    EXPECT_EQ(result, "abc");
    EXPECT_EQ(buffer.readableBytes(), 0U);
    EXPECT_EQ(buffer.writableBytes(), 8U);
}

TEST(BufferTest, AppendExpandsWhenNeeded) {
    liteim::Buffer buffer(4);

    const auto status = buffer.append(std::string{"0123456789"});

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 10U);
    EXPECT_EQ(readableAsString(buffer), "0123456789");
}

TEST(BufferTest, AppendCompactsReadableDataBeforeExpanding) {
    liteim::Buffer buffer(8);
    ASSERT_TRUE(buffer.append(std::string{"abcdef"}).isOk());
    ASSERT_TRUE(buffer.retrieve(4).isOk());

    const auto status = buffer.append(std::string{"ghijkl"});

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 8U);
    EXPECT_EQ(readableAsString(buffer), "efghijkl");
}

TEST(BufferTest, AppendExpandsAndPreservesExistingData) {
    liteim::Buffer buffer(4);
    ASSERT_TRUE(buffer.append(std::string{"abcd"}).isOk());

    const auto status = buffer.append(std::string{"efgh"});

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 8U);
    EXPECT_EQ(buffer.retrieveAllAsString(), "abcdefgh");
}

TEST(BufferTest, RetrievePastReadableBytesReturnsError) {
    liteim::Buffer buffer(8);
    ASSERT_TRUE(buffer.append(std::string{"abc"}).isOk());

    const auto status = buffer.retrieve(4);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(buffer.readableBytes(), 3U);
    EXPECT_EQ(readableAsString(buffer), "abc");
}

TEST(BufferTest, NullAppendWithNonzeroLengthReturnsError) {
    liteim::Buffer buffer(8);

    const auto status = buffer.append(static_cast<const liteim::Byte*>(nullptr), 1);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(buffer.readableBytes(), 0U);
}

TEST(BufferTest, NullAppendWithZeroLengthIsOk) {
    liteim::Buffer buffer(8);

    const auto status = buffer.append(static_cast<const liteim::Byte*>(nullptr), 0);

    EXPECT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(buffer.readableBytes(), 0U);
}
