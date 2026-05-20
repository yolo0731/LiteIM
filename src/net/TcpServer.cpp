#include "liteim/net/TcpServer.hpp"

#include "liteim/base/Timestamp.hpp"
#include "liteim/net/Acceptor.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/timer/TimerManager.hpp"

#include <arpa/inet.h>

#include <array>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace liteim {
namespace {

std::string peerIpFrom(const sockaddr_in& peer_address) {
    std::array<char, INET_ADDRSTRLEN> buffer{};
    if (::inet_ntop(AF_INET, &peer_address.sin_addr, buffer.data(), buffer.size()) == nullptr) {
        return {};
    }
    return std::string(buffer.data());
}

}  // namespace

TcpServer::TcpServer(EventLoop* base_loop, std::string listen_ip, std::uint16_t port,
                     std::size_t io_thread_count)
    : base_loop_(base_loop), listen_ip_(std::move(listen_ip)), requested_port_(port),
      io_threads_(base_loop, io_thread_count) {
    if (base_loop_ == nullptr) {
        throw std::invalid_argument("TcpServer requires a valid base EventLoop");
    }
}

TcpServer::~TcpServer() {
    if (base_loop_ != nullptr && !base_loop_->isInLoopThread()) {
        std::terminate();
    }
    stopInLoop();
}

// 用来设置服务器收到消息后的处理回调函数
void TcpServer::setMessageCallback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    message_callback_ = std::move(callback);
}

void TcpServer::setSessionCloseCallback(SessionCloseCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_close_callback_ = std::move(callback);
}

void TcpServer::setHeartbeatOptions(std::chrono::milliseconds interval,
                                    std::chrono::milliseconds timeout) {
    base_loop_->assertInLoopThread();
    if (started_.load()) {
        throw std::logic_error("heartbeat options must be set before TcpServer starts");
    }
    if (interval.count() <= 0 || timeout.count() <= 0) {
        throw std::invalid_argument("heartbeat interval and timeout must be positive");
    }

    heartbeat_interval_ = interval;
    heartbeat_timeout_ = timeout;
}

void TcpServer::setSessionOutputHighWaterMark(std::size_t high_water_mark) {
    base_loop_->assertInLoopThread();
    if (started_.load()) {
        throw std::logic_error(
            "session output high water mark must be set before TcpServer starts");
    }
    if (high_water_mark == 0) {
        throw std::invalid_argument("session output high water mark must be positive");
    }

    session_output_high_water_mark_ = high_water_mark;
}

void TcpServer::start() {
    base_loop_->assertInLoopThread();
    if (started_.load()) {
        return;
    }

    io_threads_.start();
    try {
        auto acceptor = std::make_unique<Acceptor>(base_loop_, listen_ip_, requested_port_);
        acceptor->setNewConnectionCallback([this](UniqueFd accepted_fd,
                                                  const sockaddr_in& peer_address) {
            handleNewConnection(std::move(accepted_fd), peerIpFrom(peer_address));
        });

        port_ = acceptor->port();
        acceptor_ = std::move(acceptor);
        started_ = true;
        startHeartbeatTimer();
    } catch (...) {
        if (heartbeat_timer_ != nullptr) {
            heartbeat_timer_->stop();
            heartbeat_timer_.reset();
        }
        if (acceptor_ != nullptr) {
            acceptor_->close();
            acceptor_.reset();
        }
        io_threads_.stop();
        port_ = 0;
        started_ = false;
        throw;
    }
}

void TcpServer::stop() noexcept {
    if (base_loop_ == nullptr) {
        return;
    }

    if (!base_loop_->isInLoopThread()) {
        std::terminate();
    }

    stopInLoop();
}

std::uint16_t TcpServer::port() const noexcept {
    return port_.load();
}

std::size_t TcpServer::sessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

bool TcpServer::started() const noexcept {
    return started_.load();
}

Status TcpServer::sendToSession(std::uint64_t session_id, const Packet& packet) {
    Session::Ptr session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return Status::error(ErrorCode::NotFound, "session was not found");
        }
        session = it->second;
    }

    if (session == nullptr || session->closed()) {
        return Status::error(ErrorCode::NotFound, "session was not found");
    }

    return session->sendPacket(packet);
}

void TcpServer::stopInLoop() noexcept {
    if (!started_.exchange(false) && acceptor_ == nullptr) {
        return;
    }

    if (acceptor_ != nullptr) {
        acceptor_->close();
        acceptor_.reset();
    }
    if (heartbeat_timer_ != nullptr) {
        heartbeat_timer_->stop();
        heartbeat_timer_.reset();
    }
    port_ = 0;

    std::vector<Session::Ptr> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions.reserve(sessions_.size());
        for (const auto& [_, session] : sessions_) {
            sessions.push_back(session);
        }
        sessions_.clear();  // 把整个 unordered_map 清空
    }

    for (const auto& session : sessions) {
        if (session != nullptr) {
            session->close();
        }
    }

    io_threads_.stop();
}

void TcpServer::handleNewConnection(UniqueFd accepted_fd, std::string peer_ip) {
    base_loop_->assertInLoopThread();
    if (!started_.load() || !accepted_fd) {
        return;
    }

    EventLoop* io_loop = io_threads_.getNextLoop();
    auto accepted_holder = std::make_shared<UniqueFd>(std::move(accepted_fd));
    io_loop->queueInLoop([this, io_loop, accepted_holder, peer_ip = std::move(peer_ip)]() mutable {
        createSessionInLoop(io_loop, accepted_holder, std::move(peer_ip));
    });
}

void TcpServer::createSessionInLoop(EventLoop* io_loop, std::shared_ptr<UniqueFd> accepted_fd,
                                    std::string peer_ip) {
    io_loop->assertInLoopThread();
    if (!started_.load() || accepted_fd == nullptr || !*accepted_fd) {
        return;
    }

    const auto session_id = next_session_id_.fetch_add(1);
    auto session =
        std::make_shared<Session>(io_loop, std::move(*accepted_fd), session_id, std::move(peer_ip));
    session->setOutputHighWaterMark(session_output_high_water_mark_);
    session->setMessageCallback([this](const Session::Ptr& observed, const Packet& packet) {
        handleMessage(observed, packet);
    });
    session->setCloseCallback([this, session_id](const Session::Ptr&) {
        removeSession(session_id);
        handleSessionClose(session_id);
    });

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[session_id] = session;
    }

    session->start();
}

void TcpServer::handleMessage(const Session::Ptr& session, const Packet& packet) {
    MessageCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = message_callback_;
    }

    if (callback) {
        callback(session, packet);
        return;
    }

    const auto status = session->sendPacket(packet);
    (void)status;
}

void TcpServer::removeSession(std::uint64_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session_id);
}

void TcpServer::handleSessionClose(std::uint64_t session_id) {
    SessionCloseCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = session_close_callback_;
    }

    if (callback) {
        callback(session_id);
    }
}

void TcpServer::startHeartbeatTimer() {
    base_loop_->assertInLoopThread();
    heartbeat_timer_ = std::make_unique<TimerManager>(base_loop_, heartbeat_interval_);
    const auto status = heartbeat_timer_->start();
    if (!status.isOk()) {
        heartbeat_timer_.reset();
        throw std::runtime_error(status.message());
    }
    scheduleHeartbeatCheck();
}

void TcpServer::scheduleHeartbeatCheck() {
    base_loop_->assertInLoopThread();
    if (!started_.load() || heartbeat_timer_ == nullptr || !heartbeat_timer_->started()) {
        return;
    }

    heartbeat_timer_->runAfter(heartbeat_interval_, [this]() {
        closeIdleSessions();
        scheduleHeartbeatCheck();
    });
}

void TcpServer::closeIdleSessions() {
    base_loop_->assertInLoopThread();
    if (!started_.load()) {
        return;
    }

    const auto now_ms = Timestamp::now().millisecondsSinceEpoch();
    const auto timeout_ms = heartbeat_timeout_.count();
    std::vector<Session::Ptr> expired_sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [_, session] : sessions_) {
            if (session == nullptr || session->closed()) {
                continue;
            }
            if (now_ms - session->lastActiveTimeMilliseconds() >= timeout_ms) {
                expired_sessions.push_back(session);
            }
        }
    }

    for (const auto& session : expired_sessions) {
        session->close();
    }
}

}  // namespace liteim
