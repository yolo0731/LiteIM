#include "liteim/cache/RedisClient.hpp"

#include <cstring>
#include <memory>
#include <utility>

#include <hiredis/hiredis.h>

namespace liteim {
namespace {

constexpr long kConnectTimeoutSeconds = 2;  // 连接超时时间，单位为秒

// 自定义 deleter，用于在 RedisReplyPtr 超出作用域时正确释放 redisReply 对象
struct RedisReplyDeleter {
    void operator()(redisReply* reply) const noexcept {
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
    }
};

// 让 redisReply 自动释放，避免忘记 free，unique_ptr第二个参数是自定义删除器类型(不能用默认的 delete)
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

// 执行 Redis 命令，将vector<string>格式的命令参数转换为 hiredis 需要的格式，并处理返回的 redisReply 对象
// context是 Redis 连接句柄，arguments 是 Redis 命令及其参数列表，action 是用于错误消息的上下文描述，reply 用于返回 Redis 的回复
Status runCommand(redisContext* context, const std::vector<std::string>& arguments,
                  const std::string& action, RedisReplyPtr& reply) {
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

    //调用 hiredis 的 redisCommandArgv 函数执行 Redis 命令，传入命令参数的数量、参数地址和参数长度地址
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

}  // namespace

RedisClient::~RedisClient() {
    close();
}

RedisClient::RedisClient(RedisClient&& other) noexcept
    : context_(std::exchange(other.context_, nullptr)),
      connected_(std::exchange(other.connected_, false)) {}

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
    // hiredis 使用 timeval 结构体来指定连接超时时间，tv_sec 是秒，tv_usec 是微秒
    timeval timeout{};
    timeout.tv_sec = kConnectTimeoutSeconds;
    timeout.tv_usec = 0;

    // 调用hiredis的redisConnectWithTimeout函数连接到 Redis 服务器，传入主机地址、端口号和连接超时时间
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

    // 如果配置了 password，执行 AUTH
    if (!config.password.empty()) {
        RedisReplyPtr reply;
        const auto status =
            runCommand(context_, {"AUTH", config.password}, "Redis AUTH failed", reply);
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
        // 如果 db != 0，执行 SELECT
        const auto status = runCommand(context_, {"SELECT", std::to_string(config.db)},
                                       "Redis SELECT failed", reply);
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

// 先检查当前是否 connected,执行 PING, 期望 Redis 返回 PONG
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

// setex 设置带过期时间的 key-value对，ttl 是过期时间
Status RedisClient::setex(const std::string& key, const std::string& value,
                          std::chrono::seconds ttl) {
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
    const auto status = runCommand(context_, {"SETEX", key, std::to_string(ttl.count()), value},
                                   "Redis SETEX failed", reply);
    if (!status.isOk()) {
        return status;
    }
    return expectStatusOk(*reply, "Redis SETEX failed");
}

// get 获取 key 的值，如果 key 不存在，value 将保持 std::nullopt
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

// del 根据 key 删除 key-value对，removed_count 返回实际删除的键数量
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

// incr 将 key对应的value的整数值加1，要求value是整数，value 返回加1后的值，如果 key 不存在，Redis 会先将 key 的值设为0再执行加1操作
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

// 刷新过期时间,updated 输出是否成功更新了过期时间，如果 key 不存在或者已经过期，Redis 无法更新过期时间，updated 将被设置为 false
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
    const auto status = runCommand(context_, {"EXPIRE", key, std::to_string(ttl.count())},
                                   "Redis EXPIRE failed", reply);
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

// 执行 Lua 脚本,让多条 Redis 命令在 Redis 服务器里一次性执行
Status RedisClient::eval(const std::string& script, const std::vector<std::string>& keys,
                         const std::vector<std::string>& args, std::optional<std::string>& value) {
    value.reset();

    const auto connected_status = validateConnected(*this);
    if (!connected_status.isOk()) {
        return connected_status;
    }
    if (script.empty()) {
        return Status::error(ErrorCode::InvalidArgument, "Redis eval script must not be empty");
    }

    /*
    Redis 的 EVAL 命令格式是：
    EVAL <script> <numkeys> <key1> <key2> ... <arg1> <arg2> ...
    例子：
    command[0] = "EVAL"
    command[1] = "local value = redis.call('INCR', KEYS[1]); redis.call('EXPIRE',
    KEYS[1], ARGV[1]); return value"
    command[2] = "1" 表示numkeys，key的数量
    command[3] = "login:failure:5:alice:9:127.0.0.1"
    command[4] = "60"
    */
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
        redisFree(context_);  // 释放 Redis 连接
        context_ = nullptr;
    }
    connected_ = false;
}

bool RedisClient::isConnected() const noexcept {
    return connected_ && context_ != nullptr;
}

}  // namespace liteim
