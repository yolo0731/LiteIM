#pragma once

#include "liteim/base/Config.hpp"
#include "liteim/base/Status.hpp"
#include "liteim/cache/RedisClient.hpp"

#include <chrono>
#include <cstddef>
#include <memory>

namespace liteim {

struct RedisPoolState;

class RedisConnectionGuard {
public:
    RedisConnectionGuard() = default;
    ~RedisConnectionGuard();

    RedisConnectionGuard(const RedisConnectionGuard&) = delete;
    RedisConnectionGuard& operator=(const RedisConnectionGuard&) = delete;

    RedisConnectionGuard(RedisConnectionGuard&& other) noexcept;
    RedisConnectionGuard& operator=(RedisConnectionGuard&& other) noexcept;

    RedisClient* get() noexcept;
    const RedisClient* get() const noexcept;
    RedisClient& operator*() noexcept;
    RedisClient* operator->() noexcept;
    explicit operator bool() const noexcept;

    void reset() noexcept;

private:
    friend class RedisPool;

    void reset(std::shared_ptr<RedisPoolState> state, RedisClient* client) noexcept;

    std::shared_ptr<RedisPoolState> state_;
    RedisClient* client_{nullptr};
};

class RedisPool {
public:
    explicit RedisPool(RedisConfig config);
    ~RedisPool();

    RedisPool(const RedisPool&) = delete;
    RedisPool& operator=(const RedisPool&) = delete;

    RedisPool(RedisPool&&) = delete;
    RedisPool& operator=(RedisPool&&) = delete;

    Status start();
    Status acquire(std::chrono::milliseconds timeout, RedisConnectionGuard& guard);
    void release(RedisConnectionGuard& guard) noexcept;
    void close() noexcept;

    bool started() const noexcept;
    bool closed() const noexcept;
    std::size_t size() const noexcept;

private:
    std::shared_ptr<RedisPoolState> state_;
};

} // namespace liteim
