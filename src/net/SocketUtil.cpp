#include "liteim/net/SocketUtil.hpp"

#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace liteim {
namespace {

Status invalidFdStatus() {
    return Status::error(ErrorCode::InvalidArgument, "socket fd is invalid");
}

Status errnoStatus(const char* action, int error_number) {
    return Status::error(ErrorCode::IoError,
                         std::string(action) + " failed with errno " + std::to_string(error_number));
}

Status setSocketOption(int fd, int level, int option, bool enabled, const char* option_name) {
    if (fd < 0) {
        return invalidFdStatus();
    }

    const int value = enabled ? 1 : 0;
    if (::setsockopt(fd, level, option, &value, static_cast<socklen_t>(sizeof(value))) < 0) {
        return errnoStatus(option_name, errno);
    }

    return Status::ok();
}

}  // namespace

Status createNonBlockingSocket(int& fd) {
    fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        fd = kInvalidFd;
        return errnoStatus("socket", errno);
    }

    return Status::ok();
}

Status setNonBlocking(int fd) {
    if (fd < 0) {
        return invalidFdStatus();
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return errnoStatus("fcntl(F_GETFL)", errno);
    }

    if ((flags & O_NONBLOCK) != 0) {
        return Status::ok();
    }

    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return errnoStatus("fcntl(F_SETFL)", errno);
    }

    return Status::ok();
}

Status setReuseAddr(int fd, bool enabled) {
    return setSocketOption(fd, SOL_SOCKET, SO_REUSEADDR, enabled, "setsockopt(SO_REUSEADDR)");
}

Status setReusePort(int fd, bool enabled) {
#ifdef SO_REUSEPORT
    return setSocketOption(fd, SOL_SOCKET, SO_REUSEPORT, enabled, "setsockopt(SO_REUSEPORT)");
#else
    (void)fd;
    (void)enabled;
    return Status::error(ErrorCode::InternalError, "SO_REUSEPORT is not supported on this platform");
#endif
}

Status setTcpNoDelay(int fd, bool enabled) {
    return setSocketOption(fd, IPPROTO_TCP, TCP_NODELAY, enabled, "setsockopt(TCP_NODELAY)");
}

Status setKeepAlive(int fd, bool enabled) {
    return setSocketOption(fd, SOL_SOCKET, SO_KEEPALIVE, enabled, "setsockopt(SO_KEEPALIVE)");
}

Status closeFd(int& fd) {
    if (fd < 0) {
        fd = kInvalidFd;
        return Status::ok();
    }

    const int current_fd = fd;
    fd = kInvalidFd;
    if (::close(current_fd) < 0) {
        return errnoStatus("close", errno);
    }

    return Status::ok();
}

Status getSocketError(int fd, int& error_code) {
    if (fd < 0) {
        return invalidFdStatus();
    }

    socklen_t len = static_cast<socklen_t>(sizeof(error_code));
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error_code, &len) < 0) {
        return errnoStatus("getsockopt(SO_ERROR)", errno);
    }

    return Status::ok();
}

}  // namespace liteim
