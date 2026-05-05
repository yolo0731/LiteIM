#include <gtest/gtest.h>

TEST(SmokeTest, GoogleTestWorks) {
    static_assert(__cplusplus >= 201703L, "LiteIM requires C++17 or newer");
    EXPECT_EQ(1 + 1, 2);
}
