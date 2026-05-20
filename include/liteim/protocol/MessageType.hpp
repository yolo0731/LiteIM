#pragma once

#include <cstdint>

namespace liteim {

enum class MessageType : std::uint16_t {
    Unknown = 0,

    HeartbeatRequest = 1,
    HeartbeatResponse = 2,

    RegisterRequest = 100,
    RegisterResponse = 101,
    LoginRequest = 102,
    LoginResponse = 103,
    LogoutRequest = 104,
    LogoutResponse = 105,

    AddFriendRequest = 200,
    AddFriendResponse = 201,
    ListFriendsRequest = 202,
    ListFriendsResponse = 203,

    PrivateMessageRequest = 300,
    PrivateMessageResponse = 301,
    PrivateMessagePush = 302,

    CreateGroupRequest = 400,
    CreateGroupResponse = 401,
    JoinGroupRequest = 402,
    JoinGroupResponse = 403,
    ListGroupsRequest = 404,
    ListGroupsResponse = 405,
    GroupMessageRequest = 406,
    GroupMessageResponse = 407,
    GroupMessagePush = 408,

    OfflineMessagesRequest = 500,
    OfflineMessagesResponse = 501,
    HistoryRequest = 502,
    HistoryResponse = 503,
    OfflineMessagesAckRequest = 504,
    OfflineMessagesAckResponse = 505,
    DeliveryAckRequest = 506,
    DeliveryAckResponse = 507,

    ErrorResponse = 900,
};

const char* toString(MessageType type) noexcept;
bool isRequestType(MessageType type) noexcept;
bool isResponseType(MessageType type) noexcept;
bool isPushType(MessageType type) noexcept;

}  // namespace liteim
