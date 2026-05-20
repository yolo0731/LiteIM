#include "liteim/protocol/Tlv.hpp"

#include <gtest/gtest.h>

TEST(TlvTypeTest, CoreTypesReturnReadableNames) {
    EXPECT_STREQ(liteim::toString(liteim::TlvType::Username), "USERNAME");
    EXPECT_STREQ(liteim::toString(liteim::TlvType::UserId), "USER_ID");
    EXPECT_STREQ(liteim::toString(liteim::TlvType::FriendId), "FRIEND_ID");
    EXPECT_STREQ(liteim::toString(liteim::TlvType::OnlineStatus), "ONLINE_STATUS");
    EXPECT_STREQ(liteim::toString(liteim::TlvType::GroupId), "GROUP_ID");
    EXPECT_STREQ(liteim::toString(liteim::TlvType::ConversationId), "CONVERSATION_ID");
    EXPECT_STREQ(liteim::toString(liteim::TlvType::MessageText), "MESSAGE_TEXT");
    EXPECT_STREQ(liteim::toString(liteim::TlvType::UnreadCount), "UNREAD_COUNT");
    EXPECT_STREQ(liteim::toString(liteim::TlvType::DeliveryStatus), "DELIVERY_STATUS");
    EXPECT_STREQ(liteim::toString(liteim::TlvType::ErrorMessage), "ERROR_MESSAGE");
}

TEST(TlvTypeTest, UnknownTypeReturnsUnknown) {
    EXPECT_STREQ(liteim::toString(liteim::TlvType::Unknown), "UNKNOWN");
    EXPECT_STREQ(liteim::toString(static_cast<liteim::TlvType>(100)), "UNKNOWN");
    EXPECT_STREQ(liteim::toString(static_cast<liteim::TlvType>(101)), "UNKNOWN");
    EXPECT_STREQ(liteim::toString(static_cast<liteim::TlvType>(65535)), "UNKNOWN");
}
