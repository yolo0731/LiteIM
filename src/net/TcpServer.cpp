#include "liteim/net/TcpServer.hpp"

#include "liteim/net/Acceptor.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace liteim {

TcpServer::TcpServer(EventLoop* base_loop, std::string listen_ip, std::uint16_t port, std::size_t io_thread_count)
    : base_loop_(base_loop), listen_ip_(std::move(listen_ip)), requested_port_(port),
      io_threads_(base_loop, io_thread_count) {
    if (base_loop_ == nullptr) {
        throw std::invalid_argument("TcpServer requires a valid base EventLoop");
    }
}

TcpServer::~TcpServer() {
    stop();
}

// 用来设置服务器收到消息后的处理回调函数
void TcpServer::setMessageCallback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    message_callback_ = std::move(callback);
}

void TcpServer::start() {
    base_loop_->assertInLoopThread();
    if (started_.load()) {
        return;
    }

    io_threads_.start();
    try {
        auto acceptor = std::make_unique<Acceptor>(base_loop_, listen_ip_, requested_port_);
        acceptor->setNewConnectionCallback(
            [this](UniqueFd accepted_fd, const sockaddr_in&) { handleNewConnection(std::move(accepted_fd)); });

        port_ = acceptor->port();
        acceptor_ = std::move(acceptor);
        started_ = true;
    } catch (...) {
        io_threads_.stop();
        port_ = 0;
        throw;
    }
}

void TcpServer::stop() noexcept {
    if (base_loop_ == nullptr) {
        return;
    }

    if (!base_loop_->isInLoopThread()) {
        try {
            base_loop_->queueInLoop([this]() { stopInLoop(); });
        } catch (...) {
        }
        return;
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

    return session->sendPacket(packet);
}

Status TcpServer::sendToUser(std::uint64_t, const Packet&) {
    return Status::error(ErrorCode::NotFound, "user is not bound to a session");
}

void TcpServer::stopInLoop() noexcept {
    if (!started_.exchange(false) && acceptor_ == nullptr) {
        return;
    }

    if (acceptor_ != nullptr) {
        acceptor_->close();
        acceptor_.reset();
    }
    port_ = 0;

    std::vector<Session::Ptr> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions.reserve(sessions_.size());
        for (const auto& [_, session] : sessions_) {
            sessions.push_back(session);
        }
        sessions_.clear(); // 把整个 unordered_map 清空
    }

    for (const auto& session : sessions) {
        if (session != nullptr) {
            session->close();
        }
    }

    io_threads_.stop();
}

void TcpServer::handleNewConnection(UniqueFd accepted_fd) {
    base_loop_->assertInLoopThread();
    if (!started_.load() || !accepted_fd) {
        return;
    }

    EventLoop* io_loop = io_threads_.getNextLoop();
    auto accepted_holder = std::make_shared<UniqueFd>(std::move(accepted_fd));
    io_loop->queueInLoop([this, io_loop, accepted_holder]() { createSessionInLoop(io_loop, accepted_holder); });
}

void TcpServer::createSessionInLoop(EventLoop* io_loop, std::shared_ptr<UniqueFd> accepted_fd) {
    io_loop->assertInLoopThread();
    if (!started_.load() || accepted_fd == nullptr || !*accepted_fd) {
        return;
    }

    UniqueFd fd_guard(accepted_fd->release());
    const auto session_id = next_session_id_.fetch_add(1);
    auto session = std::make_shared<Session>(io_loop, fd_guard.release(), session_id);
    session->setMessageCallback(
        [this](const Session::Ptr& observed, const Packet& packet) { handleMessage(observed, packet); });
    session->setCloseCallback([this, session_id](const Session::Ptr&) { removeSession(session_id); });

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

} // namespace liteim
