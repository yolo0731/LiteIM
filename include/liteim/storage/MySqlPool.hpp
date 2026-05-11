#pragma once

#include "liteim/base/Config.hpp"
#include "liteim/base/Status.hpp"
#include "liteim/storage/MySqlConnection.hpp"

#include <chrono>
#include <cstddef>
#include <memory>

namespace liteim {

struct MySqlPoolState;

class ConnectionGuard {
public:
    ConnectionGuard() = default;
    ~ConnectionGuard();

    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

    ConnectionGuard(ConnectionGuard&& other) noexcept;
    ConnectionGuard& operator=(ConnectionGuard&& other) noexcept;

    MySqlConnection* get() noexcept;
    const MySqlConnection* get() const noexcept;
    MySqlConnection& operator*() noexcept;
    MySqlConnection* operator->() noexcept;
    explicit operator bool() const noexcept;

    void reset() noexcept;

private:
    friend class MySqlPool;

    void reset(std::shared_ptr<MySqlPoolState> state, MySqlConnection* connection) noexcept;

    std::shared_ptr<MySqlPoolState> state_;
    MySqlConnection* connection_{nullptr};
};

class MySqlPool {
public:
    explicit MySqlPool(MySqlConfig config);
    ~MySqlPool();

    MySqlPool(const MySqlPool&) = delete;
    MySqlPool& operator=(const MySqlPool&) = delete;

    MySqlPool(MySqlPool&&) = delete;
    MySqlPool& operator=(MySqlPool&&) = delete;

    Status start();
    Status acquire(std::chrono::milliseconds timeout, ConnectionGuard& guard);
    void close() noexcept;

    bool started() const noexcept;
    bool closed() const noexcept;
    std::size_t size() const noexcept;

private:
    std::shared_ptr<MySqlPoolState> state_;
};

} // namespace liteim
