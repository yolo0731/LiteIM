#pragma once

#include <cstdint>
#include <string_view>

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
    GroupMessageRequest = 404,
    GroupMessageResponse = 405,
    GroupMessagePush = 406,

    OfflineMessagesRequest = 500,
    OfflineMessagesResponse = 501,
    HistoryRequest = 502,
    HistoryResponse = 503,

    BotChatRequest = 600,
    BotChatResponse = 601,
    BotMessagePush = 602,

    ErrorResponse = 900,
};

std::string_view toString(MessageType type) noexcept;
bool isRequestType(MessageType type) noexcept;
bool isResponseType(MessageType type) noexcept;

}  // namespace liteim
