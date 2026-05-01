#include "liteim/net/SocketUtil.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace liteim::net {
namespace {

int printSyscallError(const char* operation) {
    const int saved_errno = errno;
    std::cerr << operation << " failed: errno=" << saved_errno << " ("
              << std::strerror(saved_errno) << ")\n";
    return saved_errno;
}

}  // namespace

int createNonBlockingSocket() {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        printSyscallError("socket");
    }
    return fd;
}

bool setNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        printSyscallError("fcntl(F_GETFL)");
        return false;
    }

    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        printSyscallError("fcntl(F_SETFL)");
        return false;
    }

    return true;
}

bool setReuseAddr(int fd) {
    int option = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
        printSyscallError("setsockopt(SO_REUSEADDR)");
        return false;
    }

    return true;
}

bool setReusePort(int fd) {
    int option = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &option, sizeof(option)) < 0) {
        printSyscallError("setsockopt(SO_REUSEPORT)");
        return false;
    }

    return true;
}

void closeFd(int fd) {
    if (fd < 0) {
        return;
    }

    if (::close(fd) < 0) {
        printSyscallError("close");
    }
}

int getSocketError(int fd) {
    int socket_error = 0;
    socklen_t len = sizeof(socket_error);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &len) < 0) {
        return printSyscallError("getsockopt(SO_ERROR)");
    }

    return socket_error;
}

}  // namespace liteim::net
