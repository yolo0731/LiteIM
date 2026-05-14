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

    // 让RedisConnectionGuard模仿智能指针的行为
    RedisClient* get() noexcept;  // 返回内部的 RedisClient* 指针
    const RedisClient* get() const noexcept;
    RedisClient& operator*() noexcept;
    RedisClient* operator->() noexcept;
    explicit operator bool() const noexcept;

    void reset() noexcept;

private:
    friend class RedisPool;

    void reset(std::shared_ptr<RedisPoolState> state, RedisClient* client) noexcept;

    std::shared_ptr<RedisPoolState> state_;  // 连接池状态的共享指针
    RedisClient* client_{nullptr};           // 当前借到的 RedisClient 实例，表示一个连接
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
    // 等待连接，成功时将连接放入 guard 中
    Status acquire(std::chrono::milliseconds timeout, RedisConnectionGuard& guard);
    // 显式提供 release 方法，虽然 RedisConnectionGuard 的析构函数会自动释放连接，但有时用户可能希望提前释放连接
    void release(RedisConnectionGuard& guard) noexcept;
    void close() noexcept;

    bool started() const noexcept;
    bool closed() const noexcept;
    std::size_t size() const noexcept;  // 连接池中连接的总数，包括空闲和正在使用的连接

private:
    std::shared_ptr<RedisPoolState> state_;
};

}  // namespace liteim
