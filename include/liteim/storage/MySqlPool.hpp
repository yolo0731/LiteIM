#pragma once

#include "liteim/base/Config.hpp"
#include "liteim/base/Status.hpp"
#include "liteim/storage/MySqlConnection.hpp"

#include <chrono>
#include <cstddef>
#include <memory>

namespace liteim {

struct MySqlPoolState;

class ConnectionGuard {  // 借出的连接，负责自动归还
public:
    ConnectionGuard() = default;
    ~ConnectionGuard();

    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

    ConnectionGuard(ConnectionGuard&& other) noexcept;
    ConnectionGuard& operator=(ConnectionGuard&& other) noexcept;

    // 像指针一样使用连接
    MySqlConnection* get() noexcept;
    const MySqlConnection* get() const noexcept;
    MySqlConnection& operator*() noexcept;
    MySqlConnection* operator->() noexcept;
    explicit operator bool() const noexcept;

    void reset() noexcept;  // 归还连接并重置状态

private:
    friend class MySqlPool;

    // 由连接池调用，设置新的连接并负责归还旧连接
    void reset(std::shared_ptr<MySqlPoolState> state, MySqlConnection* connection) noexcept;

    std::shared_ptr<MySqlPoolState> state_;  // 保存连接池内部共享状态
    MySqlConnection* connection_{nullptr};   // 当前借出的连接指针
};

class MySqlPool {  // 固定大小 MySQL 连接池
public:
    explicit MySqlPool(MySqlConfig config);
    ~MySqlPool();

    MySqlPool(const MySqlPool&) = delete;
    MySqlPool& operator=(const MySqlPool&) = delete;

    MySqlPool(MySqlPool&&) = delete;
    MySqlPool& operator=(MySqlPool&&) = delete;

    Status start();
    Status acquire(std::chrono::milliseconds timeout,
                   ConnectionGuard&
                       guard);  // 从连接池借出一条连接,最多等待 timeout 时间，借出的连接放到guard里
    void close() noexcept;

    bool started() const noexcept;
    bool closed() const noexcept;
    std::size_t size() const noexcept;

private:
    std::shared_ptr<MySqlPoolState> state_;  // 真实状态都藏在 MySqlPoolState里
    //  guard 和 pool 共享同一个 MySqlPoolState
};

}  // namespace liteim
