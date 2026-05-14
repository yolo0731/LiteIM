#include "liteim/cache/LoginRateLimiter.hpp"

#include <exception>
#include <limits>
#include <optional>
#include <string>

namespace liteim {
namespace {

// 登录失败记录的 Redis key 键前缀
constexpr const char* kLoginFailureKeyPrefix = "login:failure:";

Status invalidUsernameStatus() {
    return Status::error(ErrorCode::InvalidArgument, "login limiter username must not be empty");
}

Status invalidRemoteIpStatus() {
    return Status::error(ErrorCode::InvalidArgument, "login limiter remote_ip must not be empty");
}

Status invalidMaxFailuresStatus() {
    return Status::error(ErrorCode::InvalidArgument,
                         "login limiter max_failures must be greater than zero");
}

Status invalidTtlStatus() {
    return Status::error(ErrorCode::InvalidArgument, "login limiter ttl must be positive");
}

Status parseFailureCountStatus() {
    return Status::error(ErrorCode::ParseError, "invalid login failure count value");
}

Status validateLoginKey(const LoginAttemptKey& key) {
    if (key.username.empty()) {
        return invalidUsernameStatus();
    }
    if (key.remote_ip.empty()) {
        return invalidRemoteIpStatus();
    }
    return Status::ok();
}

// 构建登录失败记录的 Redis key
std::string loginFailureKey(const LoginAttemptKey& key) {
    return std::string(kLoginFailureKeyPrefix) + std::to_string(key.username.size()) + ':' +
           key.username + ':' + std::to_string(key.remote_ip.size()) + ':' + key.remote_ip;
}

Status parseUint64(const std::string& input, std::uint64_t& value) {
    if (input.empty() || input.front() == '-') {
        return parseFailureCountStatus();
    }

    try {
        std::size_t parsed = 0;
        const auto number = std::stoull(input, &parsed, 10);
        if (parsed != input.size() || number > std::numeric_limits<std::uint64_t>::max()) {
            return parseFailureCountStatus();
        }
        value = static_cast<std::uint64_t>(number);
        return Status::ok();
    } catch (const std::exception&) {
        return parseFailureCountStatus();
    }
}

Status acquireClient(RedisPool& pool, std::chrono::milliseconds timeout,
                     RedisConnectionGuard& guard) {
    const auto status = pool.acquire(timeout, guard);
    if (!status.isOk()) {
        return status;
    }
    if (!guard) {
        return Status::error(ErrorCode::InternalError,
                             "Redis pool returned an empty connection guard");
    }
    return Status::ok();
}

}  // namespace

LoginRateLimiter::LoginRateLimiter(RedisPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(pool), acquire_timeout_(acquire_timeout) {}

// 判断是否允许登录
Status LoginRateLimiter::allow(const LoginAttemptKey& key, std::uint32_t max_failures,
                               bool& allowed) {
    allowed = false;

    const auto key_status = validateLoginKey(key);
    if (!key_status.isOk()) {
        return key_status;
    }
    if (max_failures == 0) {
        return invalidMaxFailuresStatus();
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    std::optional<std::string> value;
    const auto status = guard->get(loginFailureKey(key), value);
    if (!status.isOk()) {
        return status;
    }
    if (!value.has_value()) {
        allowed = true;
        return Status::ok();
    }

    std::uint64_t failure_count = 0;
    const auto parse_status = parseUint64(*value, failure_count);
    if (!parse_status.isOk()) {
        return parse_status;
    }

    allowed = failure_count < max_failures;
    return Status::ok();
}

// 记录一次登录失败
Status LoginRateLimiter::recordFailure(const LoginAttemptKey& key, std::chrono::seconds ttl) {
    const auto key_status = validateLoginKey(key);
    if (!key_status.isOk()) {
        return key_status;
    }
    if (ttl.count() <= 0) {
        return invalidTtlStatus();
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    std::optional<std::string> value;
    return guard->eval("local value = redis.call('INCR', KEYS[1]); "
                       "redis.call('EXPIRE', KEYS[1], ARGV[1]); "
                       "return value",
                       {loginFailureKey(key)}, {std::to_string(ttl.count())}, value);
}

Status LoginRateLimiter::clear(const LoginAttemptKey& key) {
    const auto key_status = validateLoginKey(key);
    if (!key_status.isOk()) {
        return key_status;
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    std::uint64_t removed_count = 0;
    return guard->del(loginFailureKey(key), removed_count);
}

}  // namespace liteim
