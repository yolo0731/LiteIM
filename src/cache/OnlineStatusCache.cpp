#include "liteim/cache/OnlineStatusCache.hpp"

#include "liteim/base/Timestamp.hpp"

#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace liteim {
namespace {

constexpr const char* kOnlineKeyPrefix = "online:user:";
constexpr const char* kValuePrefix = "v1:";

std::string onlineKey(std::uint64_t user_id) {
    return std::string(kOnlineKeyPrefix) + std::to_string(user_id);
}

Status invalidUserIdStatus() {
    return Status::error(ErrorCode::InvalidArgument, "online cache user_id must be greater than zero");
}

Status invalidSessionIdStatus() {
    return Status::error(ErrorCode::InvalidArgument, "online cache session_id must be greater than zero");
}

Status invalidServerIdStatus() {
    return Status::error(ErrorCode::InvalidArgument, "online cache server_id must not be empty");
}

Status invalidTtlStatus() {
    return Status::error(ErrorCode::InvalidArgument, "online cache ttl must be positive");
}

Status offlineStatus(std::uint64_t user_id) {
    return Status::error(ErrorCode::NotFound, "user is offline: " + std::to_string(user_id));
}

Status parseStatus(const std::string& field) {
    return Status::error(ErrorCode::ParseError, "invalid online session " + field);
}

Status validateUserId(std::uint64_t user_id) {
    if (user_id == 0) {
        return invalidUserIdStatus();
    }
    return Status::ok();
}

Status validateTtl(std::chrono::seconds ttl) {
    if (ttl.count() <= 0) {
        return invalidTtlStatus();
    }
    return Status::ok();
}

Status validateSession(const OnlineSession& session) {
    const auto user_status = validateUserId(session.user_id);
    if (!user_status.isOk()) {
        return user_status;
    }
    if (session.session_id == 0) {
        return invalidSessionIdStatus();
    }
    if (session.server_id.empty()) {
        return invalidServerIdStatus();
    }
    return Status::ok();
}

Status acquireClient(RedisPool& pool, std::chrono::milliseconds timeout, RedisConnectionGuard& guard) {
    const auto status = pool.acquire(timeout, guard);
    if (!status.isOk()) {
        return status;
    }
    if (!guard) {
        return Status::error(ErrorCode::InternalError, "Redis pool returned an empty connection guard");
    }
    return Status::ok();
}

Status parseUint64(const std::string& input, const std::string& field, std::uint64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto number = std::stoull(input, &parsed, 10);
        if (parsed != input.size() || number > std::numeric_limits<std::uint64_t>::max()) {
            return parseStatus(field);
        }
        value = static_cast<std::uint64_t>(number);
        return Status::ok();
    } catch (const std::exception&) {
        return parseStatus(field);
    }
}

Status parseInt64(const std::string& input, const std::string& field, std::int64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto number = std::stoll(input, &parsed, 10);
        if (parsed != input.size()) {
            return parseStatus(field);
        }
        value = static_cast<std::int64_t>(number);
        return Status::ok();
    } catch (const std::exception&) {
        return parseStatus(field);
    }
}

bool readToken(const std::string& value, std::size_t& offset, std::string& token) {
    const auto delimiter = value.find(':', offset);
    if (delimiter == std::string::npos) {
        return false;
    }
    token = value.substr(offset, delimiter - offset);
    offset = delimiter + 1;
    return true;
}

std::string serializeSession(const OnlineSession& session) {
    std::ostringstream output;
    output << kValuePrefix << session.user_id << ':' << session.session_id << ':' << session.last_active_time_ms << ':'
           << session.server_id.size() << ':' << session.server_id;
    return output.str();
}

Status parseSessionValue(const std::string& value, OnlineSession& session) {
    session = {};

    if (value.rfind(kValuePrefix, 0) != 0) {
        return parseStatus("version");
    }

    std::size_t offset = std::string(kValuePrefix).size();
    std::string token;

    if (!readToken(value, offset, token)) {
        return parseStatus("user_id");
    }
    const auto user_status = parseUint64(token, "user_id", session.user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    if (!readToken(value, offset, token)) {
        return parseStatus("session_id");
    }
    const auto session_status = parseUint64(token, "session_id", session.session_id);
    if (!session_status.isOk()) {
        return session_status;
    }

    if (!readToken(value, offset, token)) {
        return parseStatus("last_active_time_ms");
    }
    const auto timestamp_status = parseInt64(token, "last_active_time_ms", session.last_active_time_ms);
    if (!timestamp_status.isOk()) {
        return timestamp_status;
    }

    if (!readToken(value, offset, token)) {
        return parseStatus("server_id length");
    }
    std::uint64_t server_id_size = 0;
    const auto size_status = parseUint64(token, "server_id length", server_id_size);
    if (!size_status.isOk()) {
        return size_status;
    }
    if (server_id_size > value.size() - offset) {
        return parseStatus("server_id length");
    }

    session.server_id = value.substr(offset);
    if (session.server_id.size() != server_id_size) {
        return parseStatus("server_id length");
    }

    return validateSession(session);
}

} // namespace

OnlineStatusCache::OnlineStatusCache(RedisPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(pool), acquire_timeout_(acquire_timeout) {
}

Status OnlineStatusCache::setUserOnline(std::uint64_t user_id,
                                        const std::string& server_id,
                                        std::uint64_t session_id,
                                        std::chrono::seconds ttl) {
    OnlineSession session;
    session.user_id = user_id;
    session.session_id = session_id;
    session.server_id = server_id;
    session.last_active_time_ms = Timestamp::now().millisecondsSinceEpoch();
    return setUserOnline(session, ttl);
}

Status OnlineStatusCache::setUserOnline(const OnlineSession& session, std::chrono::seconds ttl) {
    OnlineSession stored_session = session;
    if (stored_session.last_active_time_ms <= 0) {
        stored_session.last_active_time_ms = Timestamp::now().millisecondsSinceEpoch();
    }

    const auto session_status = validateSession(stored_session);
    if (!session_status.isOk()) {
        return session_status;
    }
    const auto ttl_status = validateTtl(ttl);
    if (!ttl_status.isOk()) {
        return ttl_status;
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    return guard->setex(onlineKey(stored_session.user_id), serializeSession(stored_session), ttl);
}

Status OnlineStatusCache::refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl) {
    const auto user_status = validateUserId(user_id);
    if (!user_status.isOk()) {
        return user_status;
    }
    const auto ttl_status = validateTtl(ttl);
    if (!ttl_status.isOk()) {
        return ttl_status;
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    bool updated = false;
    const auto expire_status = guard->expire(onlineKey(user_id), ttl, updated);
    if (!expire_status.isOk()) {
        return expire_status;
    }
    if (!updated) {
        return offlineStatus(user_id);
    }
    return Status::ok();
}

Status OnlineStatusCache::setUserOffline(std::uint64_t user_id) {
    const auto user_status = validateUserId(user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    std::uint64_t removed_count = 0;
    return guard->del(onlineKey(user_id), removed_count);
}

Status OnlineStatusCache::isUserOnline(std::uint64_t user_id, bool& online) {
    online = false;

    const auto user_status = validateUserId(user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    std::optional<std::string> value;
    const auto get_status = guard->get(onlineKey(user_id), value);
    if (!get_status.isOk()) {
        return get_status;
    }
    if (!value.has_value()) {
        return Status::ok();
    }

    OnlineSession session;
    const auto parse_status = parseSessionValue(*value, session);
    if (!parse_status.isOk()) {
        return parse_status;
    }

    online = true;
    return Status::ok();
}

Status OnlineStatusCache::getOnlineSession(std::uint64_t user_id, OnlineSession& session) {
    session = {};

    const auto user_status = validateUserId(user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    std::optional<std::string> value;
    const auto get_status = guard->get(onlineKey(user_id), value);
    if (!get_status.isOk()) {
        return get_status;
    }
    if (!value.has_value()) {
        return offlineStatus(user_id);
    }

    return parseSessionValue(*value, session);
}

} // namespace liteim
