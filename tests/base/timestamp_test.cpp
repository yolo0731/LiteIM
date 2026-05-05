#include "liteim/base/Timestamp.hpp"

#include <chrono>

#include <gtest/gtest.h>

TEST(TimestampTest, NowReturnsPositiveEpochMilliseconds) {
    const auto now = liteim::Timestamp::now();

    EXPECT_GT(now.millisecondsSinceEpoch(), 0);
}

TEST(TimestampTest, Iso8601StringUsesUtcFormat) {
    const liteim::Timestamp timestamp{
        liteim::Timestamp::Clock::time_point{std::chrono::seconds{0}}};

    EXPECT_EQ(timestamp.toIso8601String(), "1970-01-01T00:00:00Z");
}
