#pragma once

#include "liteim/base/Config.hpp"
#include "liteim/base/Status.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct redisContext;

namespace liteim {

// 对 hiredis 的一层 C++ 封装, 单个 Redis 连接的抽象
class RedisClient {
public:
    RedisClient() = default;
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    RedisClient(RedisClient&& other) noexcept;
    RedisClient& operator=(RedisClient&& other) noexcept;

    Status connect(const RedisConfig& config);  // 连接到 Redis 服务器
    Status ping();                              // 检查连接是否可用
    // 设置带有过期时间的键值对
    Status setex(const std::string& key, const std::string& value, std::chrono::seconds ttl);

    // 获取键值对
    Status get(const std::string& key, std::optional<std::string>& value);
    // 删除键值对，记录实际删除的键数量
    Status del(const std::string& key, std::uint64_t& removed_count);
    Status incr(const std::string& key, std::int64_t& value);  // 将value的整数值加1

    // 更新键的过期时间，updated 表示是否成功更新了过期时间
    Status expire(const std::string& key, std::chrono::seconds ttl, bool& updated);

    // 执行 Lua 脚本，keys 是脚本中使用的键列表，args 是脚本中使用的参数列表，value 用于返回脚本执行结果
    Status eval(const std::string& script, const std::vector<std::string>& keys,
                const std::vector<std::string>& args, std::optional<std::string>& value);

    /* 例如Lua脚本： local value = redis.call('INCR', KEYS[1])
                    redis.call('EXPIRE', KEYS[1], ARGV[1])
                    return value
    Redis 的 EVAL 命令格式是：
    EVAL <script> <numkeys> <key1> <key2> ... <arg1> <arg2> ...numkeys 是 keys 的数量

    则在c++中eval调用：
      guard->eval(
      "local value = redis.call('INCR', KEYS[1]); "
      "redis.call('EXPIRE', KEYS[1], ARGV[1]); "
      "return value",
      {loginFailureKey(key)},
      {std::to_string(ttl.count())},
      value
  );
  Lua脚本好处是在 Redis 服务器内部连续执行多条 Redis 命令，把多步操作变成一次原子操作，
    */

    void close() noexcept;  // 关闭单个 Redis 连接
    bool isConnected() const noexcept;

private:
    redisContext* context_{nullptr};  // redis连接句柄
    bool connected_{false};
};

}  // namespace liteim
