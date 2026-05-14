#pragma once

#include "liteim/cache/ICache.hpp"
#include "liteim/cache/LoginRateLimiter.hpp"
#include "liteim/cache/OnlineStatusCache.hpp"
#include "liteim/cache/RedisPool.hpp"
#include "liteim/cache/UnreadCounter.hpp"

#include <chrono>
#include <cstdint>

namespace liteim {

class RedisCache final : public ICache {
public:
    explicit RedisCache(RedisPool& pool,
                        std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds{200});

    Status setUserOnline(const OnlineSession& session, std::chrono::seconds ttl) override;
    Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl) override;
    Status setUserOffline(std::uint64_t user_id) override;
    Status isUserOnline(std::uint64_t user_id, bool& online) override;
    Status getOnlineSession(std::uint64_t user_id, OnlineSession& session) override;

    Status incrUnread(const UnreadKey& key, std::uint64_t delta,
                      std::uint64_t& unread_count) override;
    Status getUnread(const UnreadKey& key, std::uint64_t& unread_count) override;
    Status clearUnread(const UnreadKey& key) override;

    Status allowLoginAttempt(const LoginAttemptKey& key, std::uint32_t max_failures,
                             bool& allowed) override;
    Status recordLoginFailure(const LoginAttemptKey& key, std::chrono::seconds ttl) override;
    Status clearLoginFailure(const LoginAttemptKey& key) override;

private:
    OnlineStatusCache online_;
    UnreadCounter unread_;
    LoginRateLimiter login_limiter_;
};

}  // namespace liteim
