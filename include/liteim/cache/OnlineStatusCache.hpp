#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/CacheTypes.hpp"
#include "liteim/cache/RedisPool.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace liteim {

// online的key格式：online:user:<user_id>，value是OnlineSession的序列化字符串
class OnlineStatusCache {
public:
    explicit OnlineStatusCache(RedisPool& pool, std::chrono::milliseconds acquire_timeout =
                                                    std::chrono::milliseconds{200});
    // 把用户设置为在线状态，ttl表示在线状态的过期时间，如果用户在ttl时间内没有刷新在线状态，则会被认为是离线状态
    Status setUserOnline(std::uint64_t user_id, const std::string& server_id,
                         std::uint64_t session_id, std::chrono::seconds ttl);
    Status setUserOnline(const OnlineSession& session, std::chrono::seconds ttl);
    // 刷新用户在线状态的 Redis TTL
    Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl);
    // 把用户设置为离线状态，删除在线状态的 Redis key键
    Status setUserOffline(std::uint64_t user_id);
    // 获取用户在线状态，online参数输出用户是否在线
    Status isUserOnline(std::uint64_t user_id, bool& online);
    // 获取用户在线信息，如果用户在线，session参数输出在线会话信息；如果用户离线，session参数保持不变
    Status getOnlineSession(std::uint64_t user_id, OnlineSession& session);

private:
    RedisPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
};

}  // namespace liteim
