#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/CacheTypes.hpp"

#include <chrono>
#include <cstdint>

namespace liteim {

class ICache {
public:
    virtual ~ICache() = default;
    // 在线状态
    virtual Status setUserOnline(const OnlineSession& session, std::chrono::seconds ttl) = 0;
    virtual Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl) = 0;
    virtual Status setUserOffline(std::uint64_t user_id) = 0;
    virtual Status isUserOnline(std::uint64_t user_id, bool& online) = 0;
    virtual Status getOnlineSession(std::uint64_t user_id, OnlineSession& session) = 0;
    // 未读计数
    virtual Status incrUnread(const UnreadKey& key, std::uint64_t delta, std::uint64_t& unread_count) = 0;
    virtual Status getUnread(const UnreadKey& key, std::uint64_t& unread_count) = 0;
    virtual Status clearUnread(const UnreadKey& key) = 0;
    // 登录失败限制
    virtual Status allowLoginAttempt(const LoginAttemptKey& key, std::uint32_t max_failures, bool& allowed) = 0;
    virtual Status recordLoginFailure(const LoginAttemptKey& key, std::chrono::seconds ttl) = 0;
    virtual Status clearLoginFailure(const LoginAttemptKey& key) = 0;
};

} // namespace liteim
