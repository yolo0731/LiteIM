#pragma once

#include <cstdint>
#include <string_view>

namespace liteim {

enum class TlvType : std::uint16_t {
    Unknown = 0,

    Username = 1,
    Password = 2,
    UserId = 3,
    Nickname = 4,
    Token = 5,
    SessionId = 6,

    FriendId = 20,
    TargetUserId = 21,

    GroupId = 30,
    GroupName = 31,

    ConversationType = 40,
    ConversationId = 41,
    MessageId = 42,
    MessageText = 43,
    SenderId = 44,
    ReceiverId = 45,
    TimestampMs = 46,
    Offset = 47,
    Limit = 48,
    UnreadCount = 49,

    ErrorCode = 90,
    ErrorMessage = 91,

    BotId = 100,
    PersonaId = 101,
};

std::string_view toString(TlvType type) noexcept;

}  // namespace liteim
