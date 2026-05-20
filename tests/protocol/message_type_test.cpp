#include "liteim/protocol/MessageType.hpp"

#include <array>

#include <gtest/gtest.h>

TEST(MessageTypeTest, CoreTypesReturnReadableNames) {
    EXPECT_STREQ(liteim::toString(liteim::MessageType::HeartbeatRequest), "HEARTBEAT_REQUEST");
    EXPECT_STREQ(liteim::toString(liteim::MessageType::LoginRequest), "LOGIN_REQUEST");
    EXPECT_STREQ(liteim::toString(liteim::MessageType::PrivateMessageRequest),
                 "PRIVATE_MESSAGE_REQUEST");
    EXPECT_STREQ(liteim::toString(liteim::MessageType::ListGroupsRequest), "LIST_GROUPS_REQUEST");
    EXPECT_STREQ(liteim::toString(liteim::MessageType::GroupMessagePush), "GROUP_MESSAGE_PUSH");
    EXPECT_STREQ(liteim::toString(liteim::MessageType::OfflineMessagesAckRequest),
                 "OFFLINE_MESSAGES_ACK_REQUEST");
    EXPECT_STREQ(liteim::toString(liteim::MessageType::OfflineMessagesAckResponse),
                 "OFFLINE_MESSAGES_ACK_RESPONSE");
    EXPECT_STREQ(liteim::toString(liteim::MessageType::ErrorResponse), "ERROR_RESPONSE");
}

TEST(MessageTypeTest, UnknownTypeReturnsUnknown) {
    EXPECT_STREQ(liteim::toString(liteim::MessageType::Unknown), "UNKNOWN");
    EXPECT_STREQ(liteim::toString(static_cast<liteim::MessageType>(65535)), "UNKNOWN");
}

TEST(MessageTypeTest, RequestTypesAreClassified) {
    constexpr std::array request_types{
        liteim::MessageType::HeartbeatRequest,      liteim::MessageType::RegisterRequest,
        liteim::MessageType::LoginRequest,          liteim::MessageType::LogoutRequest,
        liteim::MessageType::AddFriendRequest,      liteim::MessageType::ListFriendsRequest,
        liteim::MessageType::PrivateMessageRequest, liteim::MessageType::CreateGroupRequest,
        liteim::MessageType::JoinGroupRequest,      liteim::MessageType::ListGroupsRequest,
        liteim::MessageType::GroupMessageRequest,   liteim::MessageType::OfflineMessagesRequest,
        liteim::MessageType::OfflineMessagesAckRequest, liteim::MessageType::HistoryRequest,
    };

    for (const auto type : request_types) {
        EXPECT_TRUE(liteim::isRequestType(type)) << liteim::toString(type);
        EXPECT_FALSE(liteim::isResponseType(type)) << liteim::toString(type);
        EXPECT_FALSE(liteim::isPushType(type)) << liteim::toString(type);
    }
}

TEST(MessageTypeTest, ResponseTypesAreClassified) {
    constexpr std::array response_types{
        liteim::MessageType::HeartbeatResponse,      liteim::MessageType::RegisterResponse,
        liteim::MessageType::LoginResponse,          liteim::MessageType::LogoutResponse,
        liteim::MessageType::AddFriendResponse,      liteim::MessageType::ListFriendsResponse,
        liteim::MessageType::PrivateMessageResponse, liteim::MessageType::CreateGroupResponse,
        liteim::MessageType::JoinGroupResponse,      liteim::MessageType::ListGroupsResponse,
        liteim::MessageType::GroupMessageResponse,   liteim::MessageType::OfflineMessagesResponse,
        liteim::MessageType::OfflineMessagesAckResponse, liteim::MessageType::HistoryResponse,
        liteim::MessageType::ErrorResponse,
    };

    for (const auto type : response_types) {
        EXPECT_FALSE(liteim::isRequestType(type)) << liteim::toString(type);
        EXPECT_TRUE(liteim::isResponseType(type)) << liteim::toString(type);
        EXPECT_FALSE(liteim::isPushType(type)) << liteim::toString(type);
    }
}

TEST(MessageTypeTest, PushTypesAreClassified) {
    constexpr std::array push_types{
        liteim::MessageType::PrivateMessagePush,
        liteim::MessageType::GroupMessagePush,
    };

    for (const auto type : push_types) {
        EXPECT_FALSE(liteim::isRequestType(type)) << liteim::toString(type);
        EXPECT_FALSE(liteim::isResponseType(type)) << liteim::toString(type);
        EXPECT_TRUE(liteim::isPushType(type)) << liteim::toString(type);
    }
}

TEST(MessageTypeTest, UnknownTypesAreNotClassified) {
    constexpr std::array unknown_types{
        liteim::MessageType::Unknown,
        static_cast<liteim::MessageType>(600),
        static_cast<liteim::MessageType>(601),
        static_cast<liteim::MessageType>(602),
        static_cast<liteim::MessageType>(65535),
    };

    for (const auto type : unknown_types) {
        EXPECT_FALSE(liteim::isRequestType(type)) << liteim::toString(type);
        EXPECT_FALSE(liteim::isResponseType(type)) << liteim::toString(type);
        EXPECT_FALSE(liteim::isPushType(type)) << liteim::toString(type);
    }
}
