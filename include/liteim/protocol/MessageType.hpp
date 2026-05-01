#pragma once

#include <cstdint>

namespace liteim::protocol {

enum class MsgType : std::uint16_t {
    REGISTER_REQ = 1001,
    REGISTER_RESP = 1002,
    LOGIN_REQ = 1003,
    LOGIN_RESP = 1004,

    FRIEND_LIST_REQ = 1101,
    FRIEND_LIST_RESP = 1102,

    PRIVATE_MSG_REQ = 2001,
    PRIVATE_MSG_PUSH = 2002,

    GROUP_MSG_REQ = 3001,
    GROUP_MSG_PUSH = 3002,

    HISTORY_REQ = 4001,
    HISTORY_RESP = 4002,

    HEARTBEAT_REQ = 5001,
    HEARTBEAT_RESP = 5002,

    ERROR_RESP = 9000,
};

constexpr std::uint16_t toUint16(MsgType type) {
    return static_cast<std::uint16_t>(type);
}

}  // namespace liteim::protocol

