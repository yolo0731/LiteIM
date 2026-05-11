#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/CacheTypes.hpp"
#include "liteim/cache/RedisPool.hpp"

#include <chrono>
#include <cstdint>

namespace liteim {

class LoginRateLimiter {
public:
    explicit LoginRateLimiter(RedisPool& pool,
                              std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds{200});

    Status allow(const LoginAttemptKey& key, std::uint32_t max_failures, bool& allowed);
    Status recordFailure(const LoginAttemptKey& key, std::chrono::seconds ttl);
    Status clear(const LoginAttemptKey& key);

private:
    RedisPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
};

} // namespace liteim
