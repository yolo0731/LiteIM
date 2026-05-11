#include "liteim/cache/RedisClient.hpp"

#include <cstring>
#include <memory>
#include <utility>

#include <hiredis/hiredis.h>

namespace liteim {
namespace {

constexpr long kConnectTimeoutSeconds = 2;

struct RedisReplyDeleter {
    void operator()(redisReply* reply) const noexcept {
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
    }
};

using RedisReplyPtr = std::unique_ptr<redisReply, RedisReplyDeleter>;

Status invalidKeyStatus() {
    return Status::error(ErrorCode::InvalidArgument, "Redis key must not be empty");
}

Status invalidTtlStatus() {
    return Status::error(ErrorCode::InvalidArgument, "Redis ttl must be positive");
}

Status redisContextError(redisContext* context, const std::string& action) {
    if (context == nullptr) {
        return Status::error(ErrorCode::IoError, action + ": Redis context is null");
    }
    if (std::strlen(context->errstr) > 0) {
        return Status::error(ErrorCode::IoError, action + ": " + std::string(context->errstr));
    }
    return Status::error(ErrorCode::IoError, action + ": Redis command failed");
}

Status redisReplyError(const redisReply& reply, const std::string& action) {
    if (reply.type == REDIS_REPLY_ERROR) {
        return Status::error(ErrorCode::IoError, action + ": " + std::string(reply.str, reply.len));
    }
    return Status::ok();
}

Status runCommand(redisContext* context,
                  const std::vector<std::string>& arguments,
                  const std::string& action,
                  RedisReplyPtr& reply) {
    if (context == nullptr) {
        return Status::error(ErrorCode::InvalidArgument, "Redis client is not connected");
    }
    if (arguments.empty()) {
        return Status::error(ErrorCode::InvalidArgument, "Redis command must not be empty");
    }

    std::vector<const char*> argv;
    std::vector<std::size_t> argv_lengths;
    argv.reserve(arguments.size());
    argv_lengths.reserve(arguments.size());
    for (const auto& argument : arguments) {
        argv.push_back(argument.data());
        argv_lengths.push_back(argument.size());
    }

    auto* raw_reply = static_cast<redisReply*>(
        redisCommandArgv(context, static_cast<int>(argv.size()), argv.data(), argv_lengths.data()));
    if (raw_reply == nullptr) {
        return redisContextError(context, action);
    }

    reply.reset(raw_reply);
    return redisReplyError(*reply, action);
}

Status expectStatusOk(const redisReply& reply, const std::string& action) {
    if (reply.type != REDIS_REPLY_STATUS || std::string(reply.str, reply.len) != "OK") {
        return Status::error(ErrorCode::IoError, action + ": unexpected Redis reply type");
    }
    return Status::ok();
}

Status expectIntegerReply(const redisReply& reply, const std::string& action, std::int64_t& value) {
    if (reply.type != REDIS_REPLY_INTEGER) {
        return Status::error(ErrorCode::IoError, action + ": expected integer Redis reply");
    }
    value = reply.integer;
    return Status::ok();
}

Status validateConnected(const RedisClient& client) {
    if (!client.isConnected()) {
        return Status::error(ErrorCode::InvalidArgument, "Redis client is not connected");
    }
    return Status::ok();
}

} // namespace

RedisClient::~RedisClient() {
    close();
}

RedisClient::RedisClient(RedisClient&& other) noexcept
    : context_(std::exchange(other.context_, nullptr)),
      connected_(std::exchange(other.connected_, false)) {
}

RedisClient& RedisClient::operator=(RedisClient&& other) noexcept {
    if (this != &other) {
        close();
        context_ = std::exchange(other.context_, nullptr);
        connected_ = std::exchange(other.connected_, false);
    }
    return *this;
}

Status RedisClient::connect(const RedisConfig& config) {
    close();

    if (config.host.empty()) {
        return Status::error(ErrorCode::InvalidArgument, "Redis host must not be empty");
    }
    if (config.port == 0) {
        return Status::error(ErrorCode::InvalidArgument, "Redis port must not be zero");
    }

    timeval timeout{};
    timeout.tv_sec = kConnectTimeoutSeconds;
    timeout.tv_usec = 0;

    context_ = redisConnectWithTimeout(config.host.c_str(), config.port, timeout);
    if (context_ == nullptr) {
        return Status::error(ErrorCode::IoError, "redisConnectWithTimeout failed");
    }
    if (context_->err != 0) {
        const auto status = redisContextError(context_, "redisConnectWithTimeout failed");
        close();
        return status;
    }

    connected_ = true;

    if (!config.password.empty()) {
        RedisReplyPtr reply;
        const auto status = runCommand(context_, {"AUTH", config.password}, "Redis AUTH failed", reply);
        if (!status.isOk()) {
            close();
            return status;
        }
        const auto ok_status = expectStatusOk(*reply, "Redis AUTH failed");
        if (!ok_status.isOk()) {
            close();
            return ok_status;
        }
    }

    if (config.db != 0) {
        RedisReplyPtr reply;
        const auto status = runCommand(context_, {"SELECT", std::to_string(config.db)}, "Redis SELECT failed", reply);
        if (!status.isOk()) {
            close();
            return status;
        }
        const auto ok_status = expectStatusOk(*reply, "Redis SELECT failed");
        if (!ok_status.isOk()) {
            close();
            return ok_status;
        }
    }

    return Status::ok();
}

Status RedisClient::ping() {
    const auto connected_status = validateConnected(*this);
    if (!connected_status.isOk()) {
        return connected_status;
    }

    RedisReplyPtr reply;
    const auto status = runCommand(context_, {"PING"}, "Redis PING failed", reply);
    if (!status.isOk()) {
        return status;
    }
    if (reply->type != REDIS_REPLY_STATUS || std::string(reply->str, reply->len) != "PONG") {
        return Status::error(ErrorCode::IoError, "Redis PING failed: unexpected Redis reply");
    }
    return Status::ok();
}

Status RedisClient::setex(const std::string& key, const std::string& value, std::chrono::seconds ttl) {
    const auto connected_status = validateConnected(*this);
    if (!connected_status.isOk()) {
        return connected_status;
    }
    if (key.empty()) {
        return invalidKeyStatus();
    }
    if (ttl.count() <= 0) {
        return invalidTtlStatus();
    }

    RedisReplyPtr reply;
    const auto status =
        runCommand(context_, {"SETEX", key, std::to_string(ttl.count()), value}, "Redis SETEX failed", reply);
    if (!status.isOk()) {
        return status;
    }
    return expectStatusOk(*reply, "Redis SETEX failed");
}

Status RedisClient::get(const std::string& key, std::optional<std::string>& value) {
    value.reset();

    const auto connected_status = validateConnected(*this);
    if (!connected_status.isOk()) {
        return connected_status;
    }
    if (key.empty()) {
        return invalidKeyStatus();
    }

    RedisReplyPtr reply;
    const auto status = runCommand(context_, {"GET", key}, "Redis GET failed", reply);
    if (!status.isOk()) {
        return status;
    }
    if (reply->type == REDIS_REPLY_NIL) {
        return Status::ok();
    }
    if (reply->type != REDIS_REPLY_STRING) {
        return Status::error(ErrorCode::IoError, "Redis GET failed: expected string Redis reply");
    }

    value = std::string(reply->str, reply->len);
    return Status::ok();
}

Status RedisClient::del(const std::string& key, std::uint64_t& removed_count) {
    removed_count = 0;

    const auto connected_status = validateConnected(*this);
    if (!connected_status.isOk()) {
        return connected_status;
    }
    if (key.empty()) {
        return invalidKeyStatus();
    }

    RedisReplyPtr reply;
    const auto status = runCommand(context_, {"DEL", key}, "Redis DEL failed", reply);
    if (!status.isOk()) {
        return status;
    }

    std::int64_t value = 0;
    const auto integer_status = expectIntegerReply(*reply, "Redis DEL failed", value);
    if (!integer_status.isOk()) {
        return integer_status;
    }
    removed_count = static_cast<std::uint64_t>(value);
    return Status::ok();
}

Status RedisClient::incr(const std::string& key, std::int64_t& value) {
    value = 0;

    const auto connected_status = validateConnected(*this);
    if (!connected_status.isOk()) {
        return connected_status;
    }
    if (key.empty()) {
        return invalidKeyStatus();
    }

    RedisReplyPtr reply;
    const auto status = runCommand(context_, {"INCR", key}, "Redis INCR failed", reply);
    if (!status.isOk()) {
        return status;
    }
    return expectIntegerReply(*reply, "Redis INCR failed", value);
}

Status RedisClient::expire(const std::string& key, std::chrono::seconds ttl, bool& updated) {
    updated = false;

    const auto connected_status = validateConnected(*this);
    if (!connected_status.isOk()) {
        return connected_status;
    }
    if (key.empty()) {
        return invalidKeyStatus();
    }
    if (ttl.count() <= 0) {
        return invalidTtlStatus();
    }

    RedisReplyPtr reply;
    const auto status =
        runCommand(context_, {"EXPIRE", key, std::to_string(ttl.count())}, "Redis EXPIRE failed", reply);
    if (!status.isOk()) {
        return status;
    }

    std::int64_t value = 0;
    const auto integer_status = expectIntegerReply(*reply, "Redis EXPIRE failed", value);
    if (!integer_status.isOk()) {
        return integer_status;
    }
    updated = value == 1;
    return Status::ok();
}

Status RedisClient::eval(const std::string& script,
                         const std::vector<std::string>& keys,
                         const std::vector<std::string>& args,
                         std::optional<std::string>& value) {
    value.reset();

    const auto connected_status = validateConnected(*this);
    if (!connected_status.isOk()) {
        return connected_status;
    }
    if (script.empty()) {
        return Status::error(ErrorCode::InvalidArgument, "Redis eval script must not be empty");
    }

    std::vector<std::string> command;
    command.reserve(3U + keys.size() + args.size());
    command.push_back("EVAL");
    command.push_back(script);
    command.push_back(std::to_string(keys.size()));
    command.insert(command.end(), keys.begin(), keys.end());
    command.insert(command.end(), args.begin(), args.end());

    RedisReplyPtr reply;
    const auto status = runCommand(context_, command, "Redis EVAL failed", reply);
    if (!status.isOk()) {
        return status;
    }

    if (reply->type == REDIS_REPLY_NIL) {
        return Status::ok();
    }
    if (reply->type == REDIS_REPLY_STRING || reply->type == REDIS_REPLY_STATUS) {
        value = std::string(reply->str, reply->len);
        return Status::ok();
    }
    if (reply->type == REDIS_REPLY_INTEGER) {
        value = std::to_string(reply->integer);
        return Status::ok();
    }

    return Status::error(ErrorCode::IoError, "Redis EVAL failed: unsupported Redis reply type");
}

void RedisClient::close() noexcept {
    if (context_ != nullptr) {
        redisFree(context_);
        context_ = nullptr;
    }
    connected_ = false;
}

bool RedisClient::isConnected() const noexcept {
    return connected_ && context_ != nullptr;
}

} // namespace liteim
