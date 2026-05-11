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

class RedisClient {
public:
    RedisClient() = default;
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    RedisClient(RedisClient&& other) noexcept;
    RedisClient& operator=(RedisClient&& other) noexcept;

    Status connect(const RedisConfig& config);
    Status ping();
    Status setex(const std::string& key, const std::string& value, std::chrono::seconds ttl);
    Status get(const std::string& key, std::optional<std::string>& value);
    Status del(const std::string& key, std::uint64_t& removed_count);
    Status incr(const std::string& key, std::int64_t& value);
    Status expire(const std::string& key, std::chrono::seconds ttl, bool& updated);
    Status eval(const std::string& script,
                const std::vector<std::string>& keys,
                const std::vector<std::string>& args,
                std::optional<std::string>& value);

    void close() noexcept;
    bool isConnected() const noexcept;

private:
    redisContext* context_{nullptr};
    bool connected_{false};
};

} // namespace liteim
