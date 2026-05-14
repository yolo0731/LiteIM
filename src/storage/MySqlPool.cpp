#include "liteim/storage/MySqlPool.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace liteim {

// 连接池内部状态，使用 shared_ptr 共享给 ConnectionGuard
struct MySqlPoolState {
    explicit MySqlPoolState(MySqlConfig config_in) : config(std::move(config_in)) {}

    MySqlConfig config;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::unique_ptr<MySqlConnection>> connections;
    std::deque<MySqlConnection*> idle_connections;
    bool started{false};
    bool closed{false};
};

namespace {

Status poolNotStartedStatus() {
    return Status::error(ErrorCode::InvalidArgument, "MySQL connection pool is not started");
}

Status poolClosedStatus() {
    return Status::error(ErrorCode::InvalidArgument, "MySQL connection pool is closed");
}

Status acquireTimeoutStatus() {
    return Status::error(ErrorCode::IoError, "timed out waiting for a MySQL connection");
}

// 确保连接可用，不可用则尝试重连
Status ensureConnectionReady(const std::shared_ptr<MySqlPoolState>& state,
                             MySqlConnection& connection) {
    if (!connection.isConnected()) {
        return connection.connect(state->config);
    }

    const auto ping_status = connection.ping();
    if (ping_status.isOk()) {
        return Status::ok();
    }

    connection.close();
    return connection.connect(state->config);
}

// 归还连接到连接池，如果连接池已关闭则直接关闭连接
void releaseConnection(const std::shared_ptr<MySqlPoolState>& state,
                       MySqlConnection* connection) noexcept {
    if (!state || connection == nullptr) {
        return;
    }

    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->started && !state->closed) {
            state->idle_connections.push_back(connection);  // 连接放回空闲队列
            notify = true;
        } else {
            connection->close();
        }
    }

    if (notify) {
        state->condition.notify_one();  // 唤醒一个正在acquire等待连接的线程
    }
}

// 关闭当前空闲队列里的连接，借出去的连接：等 guard 归还时再关闭
void closeIdleConnections(const std::shared_ptr<MySqlPoolState>& state) noexcept {
    if (!state) {
        return;
    }

    std::vector<MySqlConnection*> idle_connections;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed) {
            return;
        }

        state->closed = true;  // 锁内改状态、搬走指针
        while (!state->idle_connections.empty()) {
            idle_connections.push_back(state->idle_connections.front());
            state->idle_connections.pop_front();
        }
    }
    // 锁外关闭连接
    for (auto* connection : idle_connections) {
        connection->close();
    }
    state->condition.notify_all();
}

}  // namespace

ConnectionGuard::~ConnectionGuard() {
    reset();
}

ConnectionGuard::ConnectionGuard(ConnectionGuard&& other) noexcept
    : state_(std::move(other.state_)), connection_(std::exchange(other.connection_, nullptr)) {}

ConnectionGuard& ConnectionGuard::operator=(ConnectionGuard&& other) noexcept {
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        connection_ = std::exchange(other.connection_, nullptr);
    }
    return *this;
}

MySqlConnection* ConnectionGuard::get() noexcept {
    return connection_;
}

const MySqlConnection* ConnectionGuard::get() const noexcept {
    return connection_;
}

MySqlConnection& ConnectionGuard::operator*() noexcept {
    return *connection_;
}

MySqlConnection* ConnectionGuard::operator->() noexcept {
    return connection_;
}

ConnectionGuard::operator bool() const noexcept {
    return connection_ != nullptr;
}

void ConnectionGuard::reset() noexcept {
    auto state = std::move(state_);
    auto* connection = std::exchange(connection_, nullptr);
    releaseConnection(state, connection);
}

void ConnectionGuard::reset(std::shared_ptr<MySqlPoolState> state,
                            MySqlConnection* connection) noexcept {
    reset();
    state_ = std::move(state);
    connection_ = connection;
}

MySqlPool::MySqlPool(MySqlConfig config)
    : state_(std::make_shared<MySqlPoolState>(std::move(config))) {}

MySqlPool::~MySqlPool() {
    close();
}

Status MySqlPool::start() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->started) {
        return Status::error(ErrorCode::InvalidArgument,
                             "MySQL connection pool has already started");
    }
    if (state_->closed) {
        return poolClosedStatus();
    }
    if (state_->config.pool_size == 0) {
        return Status::error(ErrorCode::InvalidArgument,
                             "MySQL connection pool size must be greater than zero");
    }

    std::vector<std::unique_ptr<MySqlConnection>> connections;
    std::deque<MySqlConnection*> idle_connections;
    connections.reserve(state_->config.pool_size);

    for (std::uint32_t index = 0; index < state_->config.pool_size; ++index) {
        auto connection = std::make_unique<MySqlConnection>();
        const auto status = connection->connect(state_->config);
        if (!status.isOk()) {
            return status;
        }

        idle_connections.push_back(connection.get());
        connections.push_back(std::move(connection));
    }

    state_->connections = std::move(connections);
    state_->idle_connections = std::move(idle_connections);
    state_->started = true;
    return Status::ok();
}

Status MySqlPool::acquire(std::chrono::milliseconds timeout, ConnectionGuard& guard) {
    if (timeout.count() < 0) {
        return Status::error(ErrorCode::InvalidArgument,
                             "MySQL connection acquire timeout must not be negative");
    }
    if (guard) {
        return Status::error(ErrorCode::InvalidArgument,
                             "ConnectionGuard already owns a MySQL connection");
    }

    MySqlConnection* connection = nullptr;
    {
        std::unique_lock<std::mutex> lock(state_->mutex);
        if (!state_->started) {
            return poolNotStartedStatus();
        }
        if (state_->closed) {
            return poolClosedStatus();
        }

        const bool ready = state_->condition.wait_for(
            lock, timeout, [&]() { return state_->closed || !state_->idle_connections.empty(); });
        if (!ready) {
            return acquireTimeoutStatus();
        }
        if (state_->closed) {
            return poolClosedStatus();
        }

        connection = state_->idle_connections.front();  // 从空闲队列拿出一条连接
        state_->idle_connections.pop_front();
    }

    const auto status = ensureConnectionReady(state_, *connection);
    if (!status.isOk()) {
        releaseConnection(state_, connection);
        return status;
    }

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->closed) {
            connection->close();
            return poolClosedStatus();
        }
    }

    guard.reset(state_, connection);
    return Status::ok();
}

void MySqlPool::close() noexcept {
    closeIdleConnections(state_);
}

bool MySqlPool::started() const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->started;
}

bool MySqlPool::closed() const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->closed;
}

std::size_t MySqlPool::size() const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->connections.size();
}

}  // namespace liteim
