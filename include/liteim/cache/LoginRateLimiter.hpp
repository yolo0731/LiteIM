#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/CacheTypes.hpp"
#include "liteim/cache/RedisPool.hpp"

#include <chrono>
#include <cstdint>

namespace liteim {

//key格式：login:failure:<username length>:<username>:<remote_ip length>:<remote_ip>
class LoginRateLimiter {
public:
    explicit LoginRateLimiter(RedisPool& pool,
                              std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds{
                                  200});
    // 检查是否允许登录，max_failures 是允许的最大失败次数
    Status allow(const LoginAttemptKey& key, std::uint32_t max_failures, bool& allowed);
    // 记录一次登录失败，ttl 是失败记录的过期时间，超过 ttl 后失败记录会被自动清除
    Status recordFailure(const LoginAttemptKey& key, std::chrono::seconds ttl);
    // 清除登录失败记录，通常在登录成功后调用
    Status clear(const LoginAttemptKey& key);

private:
    RedisPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
};

}  // namespace liteim
