#include "liteim/net/Acceptor.hpp"

#include "liteim/base/Logger.hpp"
#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace liteim {
namespace {

void throwIfError(const Status& status) {
    if (!status.isOk()) {
        throw std::runtime_error(status.message());
    }
}

sockaddr_in makeListenAddress(const std::string& listen_ip, std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (listen_ip.empty() || listen_ip == "0.0.0.0") {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        return address;
    }

    const int rc = ::inet_pton(AF_INET, listen_ip.c_str(), &address.sin_addr);
    if (rc != 1) {
        throw std::invalid_argument("listen ip is not a valid IPv4 address: " + listen_ip);
    }

    return address;
}

std::uint16_t boundPort(int fd) {
    sockaddr_in address{};
    socklen_t len = static_cast<socklen_t>(sizeof(address));
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &len) < 0) {
        throw std::runtime_error("getsockname failed with errno " + std::to_string(errno));
    }

    return ntohs(address.sin_port);
}

int openIdleFd() {
    const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("open(/dev/null) failed with errno " + std::to_string(errno));
    }
    return fd;
}

void logWarnNoexcept(const char* format, int error_number) noexcept {
    try {
        Logger::get()->warn(format, error_number);
    } catch (...) {
    }
}

} // namespace

Acceptor::Acceptor(EventLoop* loop, const std::string& listen_ip, std::uint16_t port) : loop_(loop) {
    if (loop_ == nullptr) {
        throw std::invalid_argument("Acceptor requires a valid EventLoop");
    }

    loop_->assertInLoopThread();

    try {
        int fd = kInvalidFd;
        throwIfError(createNonBlockingSocket(fd));
        listen_fd_.reset(fd);
        idle_fd_.reset(openIdleFd());
        throwIfError(setReuseAddr(listen_fd_.fd()));
        throwIfError(setReusePort(listen_fd_.fd()));

        const auto listen_address = makeListenAddress(listen_ip, port);
        if (::bind(listen_fd_.fd(), reinterpret_cast<const sockaddr*>(&listen_address),
                   static_cast<socklen_t>(sizeof(listen_address))) < 0) {
            throw std::runtime_error("bind failed with errno " + std::to_string(errno));
        }

        if (::listen(listen_fd_.fd(), SOMAXCONN) < 0) {
            throw std::runtime_error("listen failed with errno " + std::to_string(errno));
        }

        port_ = boundPort(listen_fd_.fd());
        listen_channel_ = std::make_unique<Channel>(loop_, listen_fd_.fd());
        listen_channel_->setReadCallback([this]() { handleRead(); });
        listen_channel_->enableReading();
    } catch (...) {
        close();
        throw;
    }
}

Acceptor::~Acceptor() {
    close();
}

void Acceptor::setNewConnectionCallback(NewConnectionCallback callback) {
    new_connection_callback_ = std::move(callback);
}

int Acceptor::listenFd() const noexcept {
    return listen_fd_.fd();
}

std::uint16_t Acceptor::port() const noexcept {
    return port_;
}

bool Acceptor::listening() const noexcept {
    return static_cast<bool>(listen_fd_);
}

void Acceptor::close() noexcept {
    if (loop_ != nullptr && !loop_->isInLoopThread()) {
        if (loop_->isStopped()) {
            closeInLoop();
            return;
        }

        try {
            auto done = std::make_shared<std::promise<void>>();
            auto future = done->get_future();
            loop_->queueInLoop([this, done]() noexcept {
                closeInLoop();
                done->set_value();
            });
            while (future.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
                if (loop_->isStopped()) {
                    closeInLoop();
                    return;
                }
            }
        } catch (...) {
        }
        return;
    }

    closeInLoop();
}

void Acceptor::closeInLoop() noexcept {
    if (listen_channel_ != nullptr) {
        if (loop_ != nullptr && loop_->isInLoopThread()) {
            try {
                loop_->removeChannel(listen_channel_.get());
            } catch (...) {
            }
        }
        listen_channel_.reset();
    }

    listen_fd_.reset();
    idle_fd_.reset();
}

void Acceptor::handleRead() {
    while (listen_fd_) {
        sockaddr_in peer_address{};
        socklen_t len = static_cast<socklen_t>(sizeof(peer_address));
        const int fd =
            ::accept4(listen_fd_.fd(), reinterpret_cast<sockaddr*>(&peer_address), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd >= 0) {
            UniqueFd accepted_fd(fd);
            if (new_connection_callback_) {
                new_connection_callback_(std::move(accepted_fd), peer_address);
            }
            continue;
        }

        const int error_number = errno;
        if (error_number == EINTR) {
            continue;
        }
        if (error_number == EAGAIN || error_number == EWOULDBLOCK) {
            return;
        }
        handleAcceptError(error_number);
        if (error_number == ECONNABORTED) {
            continue;
        }
        return;
    }
}

void Acceptor::handleAcceptError(int error_number) noexcept {
    if (error_number == ECONNABORTED) {
        logWarnNoexcept("accept4 aborted a pending connection with errno {}", error_number);
        return;
    }

    if (error_number == EMFILE || error_number == ENFILE) {
        logWarnNoexcept("accept4 hit fd exhaustion with errno {}", error_number);
        rejectOneConnectionAfterFdExhaustion();
        return;
    }

    logWarnNoexcept("accept4 failed with errno {}", error_number);
}

void Acceptor::rejectOneConnectionAfterFdExhaustion() noexcept {
    idle_fd_.reset();

    sockaddr_in peer_address{};
    socklen_t len = static_cast<socklen_t>(sizeof(peer_address));
    const int fd =
        ::accept4(listen_fd_.fd(), reinterpret_cast<sockaddr*>(&peer_address), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd >= 0) {
        UniqueFd rejected_fd(fd);
    } else {
        logWarnNoexcept("accept4 retry after fd exhaustion failed with errno {}", errno);
    }

    const int idle_fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (idle_fd < 0) {
        logWarnNoexcept("failed to refill Acceptor idle fd with errno {}", errno);
        return;
    }
    idle_fd_.reset(idle_fd);
}

} // namespace liteim
