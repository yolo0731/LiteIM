#include "liteim/protocol/MessageType.hpp"

#include <array>

#include <gtest/gtest.h>

TEST(MessageTypeTest, CoreTypesReturnReadableNames) {
    EXPECT_EQ(liteim::toString(liteim::MessageType::HeartbeatRequest),
              "HEARTBEAT_REQUEST");
    EXPECT_EQ(liteim::toString(liteim::MessageType::LoginRequest), "LOGIN_REQUEST");
    EXPECT_EQ(liteim::toString(liteim::MessageType::PrivateMessageRequest),
              "PRIVATE_MESSAGE_REQUEST");
    EXPECT_EQ(liteim::toString(liteim::MessageType::GroupMessagePush),
              "GROUP_MESSAGE_PUSH");
    EXPECT_EQ(liteim::toString(liteim::MessageType::BotChatRequest), "BOT_CHAT_REQUEST");
    EXPECT_EQ(liteim::toString(liteim::MessageType::ErrorResponse), "ERROR_RESPONSE");
}

TEST(MessageTypeTest, UnknownTypeReturnsUnknown) {
    EXPECT_EQ(liteim::toString(liteim::MessageType::Unknown), "UNKNOWN");
    EXPECT_EQ(liteim::toString(static_cast<liteim::MessageType>(65535)), "UNKNOWN");
}

TEST(MessageTypeTest, RequestTypesAreClassified) {
    constexpr std::array request_types{
        liteim::MessageType::HeartbeatRequest,
        liteim::MessageType::RegisterRequest,
        liteim::MessageType::LoginRequest,
        liteim::MessageType::LogoutRequest,
        liteim::MessageType::AddFriendRequest,
        liteim::MessageType::ListFriendsRequest,
        liteim::MessageType::PrivateMessageRequest,
        liteim::MessageType::CreateGroupRequest,
        liteim::MessageType::JoinGroupRequest,
        liteim::MessageType::GroupMessageRequest,
        liteim::MessageType::OfflineMessagesRequest,
        liteim::MessageType::HistoryRequest,
        liteim::MessageType::BotChatRequest,
    };

    for (const auto type : request_types) {
        EXPECT_TRUE(liteim::isRequestType(type)) << liteim::toString(type);
        EXPECT_FALSE(liteim::isResponseType(type)) << liteim::toString(type);
    }
}

TEST(MessageTypeTest, ResponseTypesAreClassified) {
    constexpr std::array response_types{
        liteim::MessageType::HeartbeatResponse,
        liteim::MessageType::RegisterResponse,
        liteim::MessageType::LoginResponse,
        liteim::MessageType::LogoutResponse,
        liteim::MessageType::AddFriendResponse,
        liteim::MessageType::ListFriendsResponse,
        liteim::MessageType::PrivateMessageResponse,
        liteim::MessageType::CreateGroupResponse,
        liteim::MessageType::JoinGroupResponse,
        liteim::MessageType::GroupMessageResponse,
        liteim::MessageType::OfflineMessagesResponse,
        liteim::MessageType::HistoryResponse,
        liteim::MessageType::BotChatResponse,
        liteim::MessageType::ErrorResponse,
    };

    for (const auto type : response_types) {
        EXPECT_FALSE(liteim::isRequestType(type)) << liteim::toString(type);
        EXPECT_TRUE(liteim::isResponseType(type)) << liteim::toString(type);
    }
}

TEST(MessageTypeTest, PushAndUnknownTypesAreNotRequestOrResponse) {
    constexpr std::array neutral_types{
        liteim::MessageType::Unknown,
        liteim::MessageType::PrivateMessagePush,
        liteim::MessageType::GroupMessagePush,
        liteim::MessageType::BotMessagePush,
        static_cast<liteim::MessageType>(65535),
    };

    for (const auto type : neutral_types) {
        EXPECT_FALSE(liteim::isRequestType(type)) << liteim::toString(type);
        EXPECT_FALSE(liteim::isResponseType(type)) << liteim::toString(type);
    }
}
