#include "liteim/cache/RedisCache.hpp"

namespace liteim {

RedisCache::RedisCache(RedisPool& pool, std::chrono::milliseconds acquire_timeout)
    : online_(pool, acquire_timeout), unread_(pool, acquire_timeout),
      login_limiter_(pool, acquire_timeout) {}

Status RedisCache::setUserOnline(const OnlineSession& session, std::chrono::seconds ttl) {
    return online_.setUserOnline(session, ttl);
}

Status RedisCache::refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl) {
    return online_.refreshUserOnline(user_id, ttl);
}

Status RedisCache::setUserOffline(std::uint64_t user_id) {
    return online_.setUserOffline(user_id);
}

Status RedisCache::isUserOnline(std::uint64_t user_id, bool& online) {
    return online_.isUserOnline(user_id, online);
}

Status RedisCache::getOnlineSession(std::uint64_t user_id, OnlineSession& session) {
    return online_.getOnlineSession(user_id, session);
}

Status RedisCache::incrUnread(const UnreadKey& key, std::uint64_t delta,
                              std::uint64_t& unread_count) {
    return unread_.incrUnread(key, delta, unread_count);
}

Status RedisCache::getUnread(const UnreadKey& key, std::uint64_t& unread_count) {
    return unread_.getUnread(key, unread_count);
}

Status RedisCache::clearUnread(const UnreadKey& key) {
    return unread_.clearUnread(key);
}

Status RedisCache::allowLoginAttempt(const LoginAttemptKey& key, std::uint32_t max_failures,
                                     bool& allowed) {
    return login_limiter_.allow(key, max_failures, allowed);
}

Status RedisCache::recordLoginFailure(const LoginAttemptKey& key, std::chrono::seconds ttl) {
    return login_limiter_.recordFailure(key, ttl);
}

Status RedisCache::clearLoginFailure(const LoginAttemptKey& key) {
    return login_limiter_.clear(key);
}

}  // namespace liteim
