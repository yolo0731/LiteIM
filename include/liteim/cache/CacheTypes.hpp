#pragma once

#include "liteim/storage/StorageTypes.hpp"

#include <cstdint>
#include <string>

namespace liteim {
// 定义缓存相关的数据结构，包括在线信息、未读消息键、登录尝试键

struct OnlineSession {
    std::uint64_t user_id{0};
    std::uint64_t session_id{0};
    std::string server_id;
    std::int64_t last_active_time_ms{0};
};

struct UnreadKey {
    std::uint64_t user_id{0};
    ConversationKey conversation;
};

struct LoginAttemptKey {
    std::string username;
    std::string remote_ip;
};

}  // namespace liteim
