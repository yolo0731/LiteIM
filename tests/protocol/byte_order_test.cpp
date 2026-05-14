#include "liteim/protocol/ByteOrder.hpp"

#include <cstdint>

#include <gtest/gtest.h>

TEST(ByteOrderTest, AppendsUnsignedIntegersAsBigEndianBytes) {
    liteim::Bytes output;

    liteim::appendUint16BE(output, 0x1234U);
    liteim::appendUint32BE(output, 0x01020304U);
    liteim::appendUint64BE(output, 0x0102030405060708ULL);

    const liteim::Bytes expected{
        0x12, 0x34, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    };
    EXPECT_EQ(output, expected);
}

TEST(ByteOrderTest, ReadsUnsignedIntegersFromBigEndianBytes) {
    const liteim::Bytes input{
        0x12, 0x34, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    };

    EXPECT_EQ(liteim::readUint16BE(input.data()), 0x1234U);
    EXPECT_EQ(liteim::readUint32BE(input.data() + 2), 0x01020304U);
    EXPECT_EQ(liteim::readUint64BE(input.data() + 6), 0x0102030405060708ULL);
}
