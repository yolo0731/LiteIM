#include "liteim/cache/UnreadCounter.hpp"

#include <exception>
#include <limits>
#include <optional>
#include <string>

namespace liteim {
namespace {

constexpr const char* kUnreadKeyPrefix = "unread:user:";

Status invalidUserIdStatus() {
    return Status::error(ErrorCode::InvalidArgument, "unread counter user_id must be greater than zero");
}

Status invalidConversationStatus() {
    return Status::error(ErrorCode::InvalidArgument, "unread counter conversation is invalid");
}

Status invalidDeltaStatus() {
    return Status::error(ErrorCode::InvalidArgument, "unread counter delta must be greater than zero");
}

Status deltaTooLargeStatus() {
    return Status::error(ErrorCode::InvalidArgument, "unread counter delta exceeds Redis signed integer range");
}

Status parseUnreadStatus() {
    return Status::error(ErrorCode::ParseError, "invalid unread counter value");
}

Status validateConversation(const ConversationKey& conversation) {
    if (conversation.type != ConversationType::kPrivate && conversation.type != ConversationType::kGroup) {
        return invalidConversationStatus();
    }
    if (conversation.id == 0) {
        return invalidConversationStatus();
    }
    return Status::ok();
}

Status validateUnreadKey(const UnreadKey& key) {
    if (key.user_id == 0) {
        return invalidUserIdStatus();
    }
    return validateConversation(key.conversation);
}

std::string conversationTypeToken(ConversationType type) {
    return std::to_string(static_cast<std::uint32_t>(type));
}

std::string unreadKey(const UnreadKey& key) {
    return std::string(kUnreadKeyPrefix) + std::to_string(key.user_id) + ":conversation:" +
           conversationTypeToken(key.conversation.type) + ':' + std::to_string(key.conversation.id);
}

Status parseUint64(const std::string& input, std::uint64_t& value) {
    if (input.empty() || input.front() == '-') {
        return parseUnreadStatus();
    }

    try {
        std::size_t parsed = 0;
        const auto number = std::stoull(input, &parsed, 10);
        if (parsed != input.size() || number > std::numeric_limits<std::uint64_t>::max()) {
            return parseUnreadStatus();
        }
        value = static_cast<std::uint64_t>(number);
        return Status::ok();
    } catch (const std::exception&) {
        return parseUnreadStatus();
    }
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

} // namespace

UnreadCounter::UnreadCounter(RedisPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(pool), acquire_timeout_(acquire_timeout) {
}

Status UnreadCounter::incrUnread(const UnreadKey& key, std::uint64_t delta, std::uint64_t& unread_count) {
    unread_count = 0;

    const auto key_status = validateUnreadKey(key);
    if (!key_status.isOk()) {
        return key_status;
    }
    if (delta == 0) {
        return invalidDeltaStatus();
    }
    if (delta > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return deltaTooLargeStatus();
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    std::optional<std::string> value;
    const auto status = guard->eval("return redis.call('INCRBY', KEYS[1], ARGV[1])",
                                    {unreadKey(key)},
                                    {std::to_string(delta)},
                                    value);
    if (!status.isOk()) {
        return status;
    }
    if (!value.has_value()) {
        return Status::error(ErrorCode::InternalError, "Redis INCRBY returned nil unread value");
    }

    return parseUint64(*value, unread_count);
}

Status UnreadCounter::getUnread(const UnreadKey& key, std::uint64_t& unread_count) {
    unread_count = 0;

    const auto key_status = validateUnreadKey(key);
    if (!key_status.isOk()) {
        return key_status;
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    std::optional<std::string> value;
    const auto status = guard->get(unreadKey(key), value);
    if (!status.isOk()) {
        return status;
    }
    if (!value.has_value()) {
        return Status::ok();
    }

    return parseUint64(*value, unread_count);
}

Status UnreadCounter::clearUnread(const UnreadKey& key) {
    const auto key_status = validateUnreadKey(key);
    if (!key_status.isOk()) {
        return key_status;
    }

    RedisConnectionGuard guard;
    const auto acquire_status = acquireClient(pool_, acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    std::uint64_t removed_count = 0;
    return guard->del(unreadKey(key), removed_count);
}

} // namespace liteim
