#include "liteim/base/ErrorCode.hpp"
#include "liteim/base/Status.hpp"

#include <gtest/gtest.h>

TEST(ErrorCodeTest, ToStringReturnsReadableNames) {
    EXPECT_EQ(liteim::toString(liteim::ErrorCode::Ok), "Ok");
    EXPECT_EQ(liteim::toString(liteim::ErrorCode::InvalidArgument), "InvalidArgument");
    EXPECT_EQ(liteim::toString(liteim::ErrorCode::NotFound), "NotFound");
    EXPECT_EQ(liteim::toString(liteim::ErrorCode::IoError), "IoError");
    EXPECT_EQ(liteim::toString(liteim::ErrorCode::ParseError), "ParseError");
    EXPECT_EQ(liteim::toString(liteim::ErrorCode::ConfigError), "ConfigError");
    EXPECT_EQ(liteim::toString(liteim::ErrorCode::InternalError), "InternalError");
}

TEST(StatusTest, OkStatusHasOkCode) {
    const auto status = liteim::Status::ok();

    EXPECT_TRUE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::Ok);
    EXPECT_TRUE(status.message().empty());
}

TEST(StatusTest, ErrorStatusCarriesCodeAndMessage) {
    const auto status = liteim::Status::error(liteim::ErrorCode::ConfigError,
                                             "invalid config");

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ConfigError);
    EXPECT_EQ(status.message(), "invalid config");
}
