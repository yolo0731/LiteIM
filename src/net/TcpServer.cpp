#include "liteim/net/TcpServer.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <sys/signalfd.h>
#include <unistd.h>

#include <cerrno>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace liteim::net {
namespace {

EventLoop* requireLoop(EventLoop* loop) {
    if (loop == nullptr) {
        throw std::invalid_argument("tcp server loop is null");
    }
    return loop;
}

std::system_error makeSystemError(const char* operation, int error_code = errno) {
    return std::system_error(error_code, std::generic_category(), operation);
}

}  // namespace

TcpServer::TcpServer(EventLoop* loop, const std::string& listen_ip, std::uint16_t port)
    : loop_(requireLoop(loop)),
      acceptor_(loop_, listen_ip, port),
      signal_fd_(createSignalFd()),
      signal_channel_(loop_, signal_fd_) {
    acceptor_.setNewConnectionCallback([this](int conn_fd, const sockaddr_in& peer_address) {
        handleNewConnection(conn_fd, peer_address);
    });
    signal_channel_.setReadCallback([this]() {
        handleSignal();
    });
}

TcpServer::~TcpServer() {
    try {
        stop();
    } catch (...) {
    }
}

void TcpServer::start() {
    if (stopped_) {
        throw std::runtime_error("tcp server is stopped");
    }
    if (started_) {
        return;
    }

    try {
        acceptor_.listen();
        signal_channel_.enableReading();
    } catch (...) {
        acceptor_.close();
        throw;
    }
    started_ = true;
}

void TcpServer::stop() {
    if (stopped_) {
        return;
    }

    stopped_ = true;
    closeAllSessions();
    acceptor_.close();

    if (started_ && signal_fd_ >= 0) {
        try {
            signal_channel_.disableAll();
        } catch (...) {
        }
    }
    closeSignalFd();
    restoreSignalMask();
    started_ = false;
    loop_->quit();
}

bool TcpServer::started() const {
    return started_;
}

bool TcpServer::stopped() const {
    return stopped_;
}

bool TcpServer::listening() const {
    return acceptor_.listening();
}

std::uint16_t TcpServer::port() const {
    return acceptor_.port();
}

std::size_t TcpServer::sessionCount() const {
    return sessions_.size();
}

bool TcpServer::sendToSession(int session_fd, const protocol::Packet& packet) {
    const auto it = sessions_.find(session_fd);
    if (it == sessions_.end() || it->second->closed()) {
        return false;
    }

    it->second->sendPacket(packet);
    return true;
}

bool TcpServer::bindUserToSession(UserId user_id, int session_fd) {
    const auto it = sessions_.find(session_fd);
    if (it == sessions_.end() || it->second->closed()) {
        return false;
    }

    user_session_fds_[user_id] = session_fd;
    return true;
}

void TcpServer::unbindUser(UserId user_id) {
    user_session_fds_.erase(user_id);
}

bool TcpServer::sendToUser(UserId user_id, const protocol::Packet& packet) {
    const auto it = user_session_fds_.find(user_id);
    if (it == user_session_fds_.end()) {
        return false;
    }

    return sendToSession(it->second, packet);
}

void TcpServer::setMessageCallback(MessageCallback callback) {
    message_callback_ = std::move(callback);
}

EventLoop* TcpServer::loop() const {
    return loop_;
}

int TcpServer::createSignalFd() {
    ::sigemptyset(&signal_mask_);
    ::sigaddset(&signal_mask_, SIGINT);
    ::sigaddset(&signal_mask_, SIGTERM);

    if (::sigprocmask(SIG_BLOCK, &signal_mask_, &previous_signal_mask_) < 0) {
        throw makeSystemError("sigprocmask(SIG_BLOCK)");
    }
    signal_mask_installed_ = true;

    const int fd = ::signalfd(-1, &signal_mask_, SFD_NONBLOCK | SFD_CLOEXEC);
    if (fd < 0) {
        const int saved_errno = errno;
        restoreSignalMask();
        throw makeSystemError("signalfd", saved_errno);
    }

    return fd;
}

void TcpServer::restoreSignalMask() {
    if (!signal_mask_installed_) {
        return;
    }

    (void)::sigprocmask(SIG_SETMASK, &previous_signal_mask_, nullptr);
    signal_mask_installed_ = false;
}

void TcpServer::closeSignalFd() {
    closeFd(signal_fd_);
    signal_fd_ = -1;
}

void TcpServer::handleNewConnection(int conn_fd, const sockaddr_in& peer_address) {
    (void)peer_address;
    cleanupRetiredSessions();

    std::shared_ptr<Session> session;
    try {
        session = std::make_shared<Session>(loop_, conn_fd);
    } catch (...) {
        closeFd(conn_fd);
        throw;
    }

    const int session_fd = session->fd();
    session->setMessageCallback([this](Session& active_session, const protocol::Packet& packet) {
        handleSessionMessage(active_session, packet);
    });
    session->setCloseCallback([this](int closed_fd) {
        handleSessionClosed(closed_fd);
    });

    try {
        sessions_.emplace(session_fd, session);
        session->start();
    } catch (...) {
        sessions_.erase(session_fd);
        if (!session->closed()) {
            session->close();
        }
        throw;
    }
}

void TcpServer::handleSessionMessage(Session& session, const protocol::Packet& packet) {
    if (message_callback_) {
        message_callback_(session, packet);
    }
}

void TcpServer::handleSessionClosed(int session_fd) {
    cleanupRetiredSessions();

    for (auto it = user_session_fds_.begin(); it != user_session_fds_.end();) {
        if (it->second == session_fd) {
            it = user_session_fds_.erase(it);
        } else {
            ++it;
        }
    }

    auto it = sessions_.find(session_fd);
    if (it == sessions_.end()) {
        return;
    }

    retired_sessions_.push_back(std::move(it->second));
    sessions_.erase(it);
}

void TcpServer::handleSignal() {
    while (signal_fd_ >= 0) {
        signalfd_siginfo signal_info{};
        const ssize_t n = ::read(signal_fd_, &signal_info, sizeof(signal_info));
        if (n == static_cast<ssize_t>(sizeof(signal_info))) {
            if (signal_info.ssi_signo == SIGINT || signal_info.ssi_signo == SIGTERM) {
                stop();
                return;
            }
            continue;
        }

        if (n == 0) {
            return;
        }

        const int saved_errno = errno;
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
            return;
        }
        if (saved_errno == EINTR) {
            continue;
        }

        stop();
        return;
    }
}

void TcpServer::closeAllSessions() {
    std::vector<std::shared_ptr<Session>> closing_sessions;
    closing_sessions.reserve(sessions_.size());

    for (auto& entry : sessions_) {
        closing_sessions.push_back(std::move(entry.second));
    }
    sessions_.clear();
    user_session_fds_.clear();

    for (const auto& session : closing_sessions) {
        if (session && !session->closed()) {
            session->close();
        }
    }

    retired_sessions_.insert(
        retired_sessions_.end(),
        std::make_move_iterator(closing_sessions.begin()),
        std::make_move_iterator(closing_sessions.end()));
}

void TcpServer::cleanupRetiredSessions() {
    retired_sessions_.clear();
}

}  // namespace liteim::net
