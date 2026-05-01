#include "TestUtil.hpp"

#include "liteim/net/SocketUtil.hpp"

#include <fcntl.h>
#include <sys/socket.h>

#include <cerrno>
#include <vector>

namespace {

using liteim::net::closeFd;
using liteim::net::createNonBlockingSocket;
using liteim::net::getSocketError;
using liteim::net::setNonBlocking;
using liteim::net::setReuseAddr;
using liteim::net::setReusePort;
using liteim::tests::TestCase;
using liteim::tests::expect;

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
        const int old_fd = fd_;
        fd_ = -1;
        return old_fd;
    }

private:
    int fd_ = -1;
};

bool isNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && (flags & O_NONBLOCK) != 0;
}

int createBlockingSocketForTest() {
    return ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
}

void testCreateNonBlockingSocket() {
    FdGuard fd(createNonBlockingSocket());

    expect(fd.get() >= 0, "createNonBlockingSocket should return valid fd");
    expect(isNonBlocking(fd.get()), "created socket should be nonblocking");
}

void testSetNonBlocking() {
    FdGuard fd(createBlockingSocketForTest());

    expect(fd.get() >= 0, "test blocking socket should be valid");
    expect(!isNonBlocking(fd.get()), "test socket should initially be blocking");
    expect(setNonBlocking(fd.get()), "setNonBlocking should succeed");
    expect(isNonBlocking(fd.get()), "socket should become nonblocking");
}

void testSetReuseAddr() {
    FdGuard fd(createNonBlockingSocket());

    expect(fd.get() >= 0, "socket should be valid");
    expect(setReuseAddr(fd.get()), "setReuseAddr should succeed");

    int option = 0;
    socklen_t len = sizeof(option);
    const int rc = ::getsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &option, &len);
    expect(rc == 0, "getsockopt(SO_REUSEADDR) should succeed");
    expect(option == 1, "SO_REUSEADDR should be enabled");
}

void testSetReusePort() {
    FdGuard fd(createNonBlockingSocket());

    expect(fd.get() >= 0, "socket should be valid");
    expect(setReusePort(fd.get()), "setReusePort should succeed");

    int option = 0;
    socklen_t len = sizeof(option);
    const int rc = ::getsockopt(fd.get(), SOL_SOCKET, SO_REUSEPORT, &option, &len);
    expect(rc == 0, "getsockopt(SO_REUSEPORT) should succeed");
    expect(option == 1, "SO_REUSEPORT should be enabled");
}

void testGetSocketError() {
    FdGuard fd(createNonBlockingSocket());

    expect(fd.get() >= 0, "socket should be valid");
    expect(getSocketError(fd.get()) == 0, "new socket should have no pending error");
}

void testCloseFd() {
    FdGuard fd(createNonBlockingSocket());
    const int raw_fd = fd.release();

    expect(raw_fd >= 0, "socket should be valid");
    closeFd(raw_fd);

    errno = 0;
    const int rc = ::fcntl(raw_fd, F_GETFL, 0);
    expect(rc < 0, "closed fd should not be usable");
    expect(errno == EBADF, "closed fd should report EBADF");
}

void testInvalidFdOperationsFail() {
    constexpr int invalid_fd = -1;

    expect(!setNonBlocking(invalid_fd), "setNonBlocking should fail on invalid fd");
    expect(!setReuseAddr(invalid_fd), "setReuseAddr should fail on invalid fd");
    expect(!setReusePort(invalid_fd), "setReusePort should fail on invalid fd");
    expect(getSocketError(invalid_fd) != 0, "getSocketError should return non-zero on invalid fd");

    closeFd(invalid_fd);
}

}  // namespace

std::vector<TestCase> socketUtilTests() {
    return {
        {"socket util create nonblocking socket", testCreateNonBlockingSocket},
        {"socket util set nonblocking", testSetNonBlocking},
        {"socket util set reuse addr", testSetReuseAddr},
        {"socket util set reuse port", testSetReusePort},
        {"socket util get socket error", testGetSocketError},
        {"socket util close fd", testCloseFd},
        {"socket util invalid fd operations fail", testInvalidFdOperationsFail},
    };
}

