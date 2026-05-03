#include "liteim/net/Session.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <stdexcept>
#include <utility>

namespace liteim::net {
namespace {

EventLoop* requireLoop(EventLoop* loop) {
    if (loop == nullptr) {
        throw std::invalid_argument("session loop is null");
    }
    return loop;
}

int prepareSessionFd(int fd) {
    if (fd < 0) {
        throw std::invalid_argument("session fd is invalid");
    }

    if (!setNonBlocking(fd)) {
        closeFd(fd);
        throw std::runtime_error("session set nonblocking failed");
    }

    return fd;
}

}  // namespace

Session::Session(EventLoop* loop, int fd)
    : loop_(requireLoop(loop)), fd_(prepareSessionFd(fd)), channel_(loop_, fd_) {
    channel_.setReadCallback([this]() {
        handleRead();
    });
    channel_.setWriteCallback([this]() {
        handleWrite();
    });
    channel_.setCloseCallback([this]() {
        handleClose();
    });
    channel_.setErrorCallback([this]() {
        handleError();
    });
}

Session::~Session() {
    try {
        closeConnection(false);
    } catch (...) {
    }
}

void Session::start() {
    if (closed_) {
        throw std::runtime_error("session is closed");
    }
    if (started_) {
        return;
    }

    channel_.enableReading();
    started_ = true;
}

void Session::sendPacket(const protocol::Packet& packet) {
    if (closed_) {
        throw std::runtime_error("session is closed");
    }

    const std::string encoded = protocol::encodePacket(packet);
    output_buffer_.append(encoded.data(), encoded.size());
    channel_.enableWriting();
}

void Session::close() {
    closeConnection(true);
}

int Session::fd() const {
    return fd_;
}

bool Session::started() const {
    return started_;
}

bool Session::closed() const {
    return closed_;
}

std::size_t Session::pendingOutputBytes() const {
    return output_buffer_.readableBytes();
}

void Session::setMessageCallback(MessageCallback callback) {
    message_callback_ = std::move(callback);
}

void Session::setCloseCallback(CloseCallback callback) {
    close_callback_ = std::move(callback);
}

void Session::handleRead() {
    std::array<char, 4096> buffer{};

    while (true) {
        const ssize_t n = ::read(fd_, buffer.data(), buffer.size());
        if (n > 0) {
            const auto packets = decoder_.feed(buffer.data(), static_cast<std::size_t>(n));
            if (decoder_.hasError()) {
                close();
                return;
            }

            for (const auto& packet : packets) {
                if (message_callback_) {
                    message_callback_(*this, packet);
                }
                if (closed_) {
                    return;
                }
            }
            continue;
        }

        if (n == 0) {
            close();
            return;
        }

        const int saved_errno = errno;
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
            return;
        }
        if (saved_errno == EINTR) {
            continue;
        }

        close();
        return;
    }
}

void Session::handleWrite() {
    while (output_buffer_.readableBytes() > 0) {
        const ssize_t n = ::send(
            fd_,
            output_buffer_.peek(),
            output_buffer_.readableBytes(),
            MSG_NOSIGNAL);

        if (n > 0) {
            output_buffer_.retrieve(static_cast<std::size_t>(n));
            continue;
        }

        if (n == 0) {
            break;
        }

        const int saved_errno = errno;
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
            break;
        }
        if (saved_errno == EINTR) {
            continue;
        }

        close();
        return;
    }

    if (!closed_ && output_buffer_.readableBytes() == 0) {
        channel_.disableWriting();
    }
}

void Session::handleClose() {
    close();
}

void Session::handleError() {
    close();
}

void Session::closeConnection(bool notify) {
    if (closed_) {
        return;
    }

    const int closed_fd = fd_;
    closed_ = true;
    channel_.disableAll();
    closeFd(closed_fd);
    fd_ = -1;

    if (notify && close_callback_) {
        close_callback_(closed_fd);
    }
}

}  // namespace liteim::net
