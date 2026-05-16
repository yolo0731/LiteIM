#include "liteim/base/ErrorCode.hpp"
#include "liteim/base/Status.hpp"

#include <gtest/gtest.h>

TEST(ErrorCodeTest, ToStringReturnsReadableNames) {
    EXPECT_STREQ(liteim::toString(liteim::ErrorCode::Ok), "Ok");
    EXPECT_STREQ(liteim::toString(liteim::ErrorCode::InvalidArgument), "InvalidArgument");
    EXPECT_STREQ(liteim::toString(liteim::ErrorCode::NotFound), "NotFound");
    EXPECT_STREQ(liteim::toString(liteim::ErrorCode::AlreadyExists), "AlreadyExists");
    EXPECT_STREQ(liteim::toString(liteim::ErrorCode::IoError), "IoError");
    EXPECT_STREQ(liteim::toString(liteim::ErrorCode::ParseError), "ParseError");
    EXPECT_STREQ(liteim::toString(liteim::ErrorCode::ConfigError), "ConfigError");
    EXPECT_STREQ(liteim::toString(liteim::ErrorCode::InternalError), "InternalError");
}

TEST(StatusTest, OkStatusHasOkCode) {
    const auto status = liteim::Status::ok();

    EXPECT_TRUE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::Ok);
    EXPECT_TRUE(status.message().empty());
}

TEST(StatusTest, ErrorStatusCarriesCodeAndMessage) {
    const auto status = liteim::Status::error(liteim::ErrorCode::ConfigError, "invalid config");

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ConfigError);
    EXPECT_EQ(status.message(), "invalid config");
}
