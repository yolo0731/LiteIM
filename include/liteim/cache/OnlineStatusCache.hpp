#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/CacheTypes.hpp"
#include "liteim/cache/RedisPool.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace liteim {

class OnlineStatusCache {
public:
    explicit OnlineStatusCache(RedisPool& pool,
                               std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds{200});

    Status setUserOnline(std::uint64_t user_id,
                         const std::string& server_id,
                         std::uint64_t session_id,
                         std::chrono::seconds ttl);
    Status setUserOnline(const OnlineSession& session, std::chrono::seconds ttl);
    Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl);
    Status setUserOffline(std::uint64_t user_id);
    Status isUserOnline(std::uint64_t user_id, bool& online);
    Status getOnlineSession(std::uint64_t user_id, OnlineSession& session);

private:
    RedisPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
};

} // namespace liteim
