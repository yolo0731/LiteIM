#include "liteim/net/Acceptor.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
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

}  // namespace

Acceptor::Acceptor(EventLoop* loop, const std::string& listen_ip, std::uint16_t port) : loop_(loop) {
    if (loop_ == nullptr) {
        throw std::invalid_argument("Acceptor requires a valid EventLoop");
    }

    loop_->assertInLoopThread();

    try {
        throwIfError(createNonBlockingSocket(listen_fd_));
        throwIfError(setReuseAddr(listen_fd_));
        throwIfError(setReusePort(listen_fd_));

        const auto listen_address = makeListenAddress(listen_ip, port);
        if (::bind(listen_fd_,
                   reinterpret_cast<const sockaddr*>(&listen_address),
                   static_cast<socklen_t>(sizeof(listen_address))) < 0) {
            throw std::runtime_error("bind failed with errno " + std::to_string(errno));
        }

        if (::listen(listen_fd_, SOMAXCONN) < 0) {
            throw std::runtime_error("listen failed with errno " + std::to_string(errno));
        }

        port_ = boundPort(listen_fd_);
        listening_ = true;
        listen_channel_ = std::make_unique<Channel>(loop_, listen_fd_);
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
    return listen_fd_;
}

std::uint16_t Acceptor::port() const noexcept {
    return port_;
}

bool Acceptor::listening() const noexcept {
    return listening_;
}

void Acceptor::close() noexcept {
    listening_ = false;

    if (listen_channel_ != nullptr) {
        if (loop_ != nullptr && loop_->isInLoopThread()) {
            try {
                loop_->removeChannel(listen_channel_.get());
            } catch (...) {
            }
        }
        listen_channel_.reset();
    }

    const auto status = closeFd(listen_fd_);
    (void)status;
}

void Acceptor::handleRead() {
    while (listen_fd_ >= 0) {
        sockaddr_in peer_address{};
        socklen_t len = static_cast<socklen_t>(sizeof(peer_address));
        const int fd = ::accept4(listen_fd_,
                                 reinterpret_cast<sockaddr*>(&peer_address),
                                 &len,
                                 SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd >= 0) {
            if (new_connection_callback_) {
                new_connection_callback_(fd, peer_address);
            } else {
                int accepted_fd = fd;
                const auto status = closeFd(accepted_fd);
                (void)status;
            }
            continue;
        }

        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        return;
    }
}

}  // namespace liteim
