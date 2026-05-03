#include "liteim/net/Acceptor.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace liteim::net {
namespace {

std::system_error makeSystemError(const char* operation, int error = errno) {
    return std::system_error(error, std::generic_category(), operation);
}

EventLoop* requireLoop(EventLoop* loop) {
    if (loop == nullptr) {
        throw std::invalid_argument("acceptor loop is null");
    }
    return loop;
}

class FdGuard {
public:
    explicit FdGuard(int fd) : fd_(fd) {}

    ~FdGuard() {
        closeFd(fd_);
    }

    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    int get() const {
        return fd_;
    }

    int release() {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_ = -1;
};

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
        throw std::invalid_argument("invalid IPv4 listen address: " + listen_ip);
    }

    return address;
}

sockaddr_in getLocalAddress(int fd) {
    sockaddr_in address{};
    socklen_t len = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &len) < 0) {
        throw makeSystemError("getsockname");
    }
    return address;
}

int createBoundListenSocket(const std::string& listen_ip, std::uint16_t port) {
    FdGuard fd(createNonBlockingSocket());
    if (fd.get() < 0) {
        throw makeSystemError("socket");
    }

    if (!setReuseAddr(fd.get())) {
        throw makeSystemError("setsockopt(SO_REUSEADDR)");
    }
    if (!setReusePort(fd.get())) {
        throw makeSystemError("setsockopt(SO_REUSEPORT)");
    }

    const sockaddr_in address = makeListenAddress(listen_ip, port);
    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        throw makeSystemError("bind");
    }

    return fd.release();
}

}  // namespace

Acceptor::Acceptor(EventLoop* loop, const std::string& listen_ip, std::uint16_t port)
    : loop_(requireLoop(loop)),
      listen_fd_(createBoundListenSocket(listen_ip, port)),
      accept_channel_(loop_, listen_fd_) {
    accept_channel_.setReadCallback([this]() {
        handleRead();
    });
}

Acceptor::~Acceptor() {
    close();
}

void Acceptor::listen() {
    if (listening_) {
        return;
    }
    if (listen_fd_ < 0) {
        throw std::runtime_error("acceptor is closed");
    }

    if (::listen(listen_fd_, SOMAXCONN) < 0) {
        throw makeSystemError("listen");
    }

    accept_channel_.enableReading();
    listening_ = true;
}

void Acceptor::close() {
    if (listen_fd_ < 0) {
        return;
    }

    try {
        accept_channel_.disableAll();
    } catch (...) {
    }
    closeFd(listen_fd_);
    listen_fd_ = -1;
    listening_ = false;
}

bool Acceptor::listening() const {
    return listening_;
}

int Acceptor::listenFd() const {
    return listen_fd_;
}

std::uint16_t Acceptor::port() const {
    if (listen_fd_ < 0) {
        return 0;
    }

    const sockaddr_in address = getLocalAddress(listen_fd_);
    return ntohs(address.sin_port);
}

void Acceptor::setNewConnectionCallback(NewConnectionCallback callback) {
    new_connection_callback_ = std::move(callback);
}

void Acceptor::handleRead() {
    while (true) {
        sockaddr_in peer_address{};
        socklen_t address_len = sizeof(peer_address);
        const int conn_fd = ::accept4(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&peer_address),
            &address_len,
            SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (conn_fd >= 0) {
            if (!new_connection_callback_) {
                closeFd(conn_fd);
                continue;
            }

            try {
                new_connection_callback_(conn_fd, peer_address);
            } catch (...) {
                closeFd(conn_fd);
                throw;
            }
            continue;
        }

        const int saved_errno = errno;
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
            return;
        }
        if (saved_errno == EINTR || saved_errno == ECONNABORTED) {
            continue;
        }

        throw makeSystemError("accept4", saved_errno);
    }
}

}  // namespace liteim::net
