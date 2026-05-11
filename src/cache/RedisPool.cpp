#include "liteim/cache/RedisPool.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace liteim {

struct RedisPoolState {
    explicit RedisPoolState(RedisConfig config_in) : config(std::move(config_in)) {}

    RedisConfig config;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::unique_ptr<RedisClient>> clients;
    std::deque<RedisClient*> idle_clients;
    bool started{false};
    bool closed{false};
};

namespace {

Status poolNotStartedStatus() {
    return Status::error(ErrorCode::InvalidArgument, "Redis connection pool is not started");
}

Status poolClosedStatus() {
    return Status::error(ErrorCode::InvalidArgument, "Redis connection pool is closed");
}

Status acquireTimeoutStatus() {
    return Status::error(ErrorCode::IoError, "timed out waiting for a Redis connection");
}

Status ensureClientReady(const std::shared_ptr<RedisPoolState>& state, RedisClient& client) {
    if (!client.isConnected()) {
        return client.connect(state->config);
    }

    const auto ping_status = client.ping();
    if (ping_status.isOk()) {
        return Status::ok();
    }

    client.close();
    return client.connect(state->config);
}

void releaseClient(const std::shared_ptr<RedisPoolState>& state, RedisClient* client) noexcept {
    if (!state || client == nullptr) {
        return;
    }

    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->started && !state->closed) {
            state->idle_clients.push_back(client);
            notify = true;
        } else {
            client->close();
        }
    }

    if (notify) {
        state->condition.notify_one();
    }
}

void closeIdleClients(const std::shared_ptr<RedisPoolState>& state) noexcept {
    if (!state) {
        return;
    }

    std::vector<RedisClient*> idle_clients;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed) {
            return;
        }

        state->closed = true;
        while (!state->idle_clients.empty()) {
            idle_clients.push_back(state->idle_clients.front());
            state->idle_clients.pop_front();
        }
    }

    for (auto* client : idle_clients) {
        client->close();
    }
    state->condition.notify_all();
}

} // namespace

RedisConnectionGuard::~RedisConnectionGuard() {
    reset();
}

RedisConnectionGuard::RedisConnectionGuard(RedisConnectionGuard&& other) noexcept
    : state_(std::move(other.state_)), client_(std::exchange(other.client_, nullptr)) {
}

RedisConnectionGuard& RedisConnectionGuard::operator=(RedisConnectionGuard&& other) noexcept {
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        client_ = std::exchange(other.client_, nullptr);
    }
    return *this;
}

RedisClient* RedisConnectionGuard::get() noexcept {
    return client_;
}

const RedisClient* RedisConnectionGuard::get() const noexcept {
    return client_;
}

RedisClient& RedisConnectionGuard::operator*() noexcept {
    return *client_;
}

RedisClient* RedisConnectionGuard::operator->() noexcept {
    return client_;
}

RedisConnectionGuard::operator bool() const noexcept {
    return client_ != nullptr;
}

void RedisConnectionGuard::reset() noexcept {
    auto state = std::move(state_);
    auto* client = std::exchange(client_, nullptr);
    releaseClient(state, client);
}

void RedisConnectionGuard::reset(std::shared_ptr<RedisPoolState> state, RedisClient* client) noexcept {
    reset();
    state_ = std::move(state);
    client_ = client;
}

RedisPool::RedisPool(RedisConfig config) : state_(std::make_shared<RedisPoolState>(std::move(config))) {}

RedisPool::~RedisPool() {
    close();
}

Status RedisPool::start() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->started) {
        return Status::error(ErrorCode::InvalidArgument, "Redis connection pool has already started");
    }
    if (state_->closed) {
        return poolClosedStatus();
    }
    if (state_->config.pool_size == 0) {
        return Status::error(ErrorCode::InvalidArgument, "Redis connection pool size must be greater than zero");
    }

    std::vector<std::unique_ptr<RedisClient>> clients;
    std::deque<RedisClient*> idle_clients;
    clients.reserve(state_->config.pool_size);

    for (std::uint32_t index = 0; index < state_->config.pool_size; ++index) {
        auto client = std::make_unique<RedisClient>();
        const auto status = client->connect(state_->config);
        if (!status.isOk()) {
            return status;
        }

        idle_clients.push_back(client.get());
        clients.push_back(std::move(client));
    }

    state_->clients = std::move(clients);
    state_->idle_clients = std::move(idle_clients);
    state_->started = true;
    return Status::ok();
}

Status RedisPool::acquire(std::chrono::milliseconds timeout, RedisConnectionGuard& guard) {
    if (timeout.count() < 0) {
        return Status::error(ErrorCode::InvalidArgument, "Redis connection acquire timeout must not be negative");
    }
    if (guard) {
        return Status::error(ErrorCode::InvalidArgument, "RedisConnectionGuard already owns a Redis connection");
    }

    RedisClient* client = nullptr;
    {
        std::unique_lock<std::mutex> lock(state_->mutex);
        if (!state_->started) {
            return poolNotStartedStatus();
        }
        if (state_->closed) {
            return poolClosedStatus();
        }

        const bool ready = state_->condition.wait_for(lock, timeout, [&]() {
            return state_->closed || !state_->idle_clients.empty();
        });
        if (!ready) {
            return acquireTimeoutStatus();
        }
        if (state_->closed) {
            return poolClosedStatus();
        }

        client = state_->idle_clients.front();
        state_->idle_clients.pop_front();
    }

    const auto status = ensureClientReady(state_, *client);
    if (!status.isOk()) {
        releaseClient(state_, client);
        return status;
    }

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->closed) {
            client->close();
            return poolClosedStatus();
        }
    }

    guard.reset(state_, client);
    return Status::ok();
}

void RedisPool::release(RedisConnectionGuard& guard) noexcept {
    guard.reset();
}

void RedisPool::close() noexcept {
    closeIdleClients(state_);
}

bool RedisPool::started() const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->started;
}

bool RedisPool::closed() const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->closed;
}

std::size_t RedisPool::size() const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->clients.size();
}

} // namespace liteim
