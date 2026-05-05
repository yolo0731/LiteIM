#include "liteim/base/Logger.hpp"

#include <gtest/gtest.h>

TEST(LoggerTest, ParseLogLevelReturnsExpectedLevel) {
    EXPECT_EQ(liteim::parseLogLevel("trace"), liteim::LogLevel::Trace);
    EXPECT_EQ(liteim::parseLogLevel("debug"), liteim::LogLevel::Debug);
    EXPECT_EQ(liteim::parseLogLevel("info"), liteim::LogLevel::Info);
    EXPECT_EQ(liteim::parseLogLevel("warning"), liteim::LogLevel::Warn);
    EXPECT_EQ(liteim::parseLogLevel("error"), liteim::LogLevel::Error);
    EXPECT_EQ(liteim::parseLogLevel("critical"), liteim::LogLevel::Critical);
    EXPECT_EQ(liteim::parseLogLevel("off"), liteim::LogLevel::Off);
}

TEST(LoggerTest, UnknownLogLevelFallsBackToInfo) {
    EXPECT_EQ(liteim::parseLogLevel("verbose"), liteim::LogLevel::Info);
}

TEST(LoggerTest, InitCreatesReusableLogger) {
    liteim::Logger::init(liteim::LogLevel::Debug);

    const auto logger = liteim::Logger::get();

    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->name(), "liteim");
}
