#include "liteim/net/SocketUtil.hpp"

#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

namespace {

class FdGuard {
public:
    explicit FdGuard(int fd) : fd_(fd) {}

    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    ~FdGuard() {
        if (fd_ >= 0) {
            const auto status = liteim::closeFd(fd_);
            (void)status;
        }
    }

private:
    int fd_;
};

int getSocketFlag(int fd) {
    return ::fcntl(fd, F_GETFL, 0);
}

int getFdFlag(int fd) {
    return ::fcntl(fd, F_GETFD, 0);
}

int getIntSocketOption(int fd, int level, int option) {
    int value = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(value));
    const int rc = ::getsockopt(fd, level, option, &value, &len);
    EXPECT_EQ(rc, 0);
    return value;
}

}  // namespace

TEST(SocketUtilTest, CreateNonBlockingSocketReturnsNonblockingFd) {
    int fd = liteim::kInvalidFd;

    const auto status = liteim::createNonBlockingSocket(fd);
    FdGuard guard(fd);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_GE(fd, 0);
    EXPECT_NE(getSocketFlag(fd) & O_NONBLOCK, 0);
    EXPECT_NE(getFdFlag(fd) & FD_CLOEXEC, 0);
}

TEST(SocketUtilTest, SetNonBlockingMarksPlainSocketNonblocking) {
    const int raw_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_GE(raw_fd, 0);
    FdGuard guard(raw_fd);

    ASSERT_EQ(getSocketFlag(raw_fd) & O_NONBLOCK, 0);

    const auto status = liteim::setNonBlocking(raw_fd);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_NE(getSocketFlag(raw_fd) & O_NONBLOCK, 0);
}

TEST(SocketUtilTest, SocketOptionsCanBeEnabled) {
    int fd = liteim::kInvalidFd;
    ASSERT_TRUE(liteim::createNonBlockingSocket(fd).isOk());
    FdGuard guard(fd);

    ASSERT_TRUE(liteim::setReuseAddr(fd).isOk());
    ASSERT_TRUE(liteim::setReusePort(fd).isOk());
    ASSERT_TRUE(liteim::setTcpNoDelay(fd).isOk());
    ASSERT_TRUE(liteim::setKeepAlive(fd).isOk());

    EXPECT_EQ(getIntSocketOption(fd, SOL_SOCKET, SO_REUSEADDR), 1);
    EXPECT_EQ(getIntSocketOption(fd, SOL_SOCKET, SO_REUSEPORT), 1);
    EXPECT_EQ(getIntSocketOption(fd, IPPROTO_TCP, TCP_NODELAY), 1);
    EXPECT_EQ(getIntSocketOption(fd, SOL_SOCKET, SO_KEEPALIVE), 1);
}

TEST(SocketUtilTest, InvalidFdReturnsError) {
    int socket_error = 0;

    EXPECT_FALSE(liteim::setNonBlocking(liteim::kInvalidFd).isOk());
    EXPECT_FALSE(liteim::setReuseAddr(liteim::kInvalidFd).isOk());
    EXPECT_FALSE(liteim::setReusePort(liteim::kInvalidFd).isOk());
    EXPECT_FALSE(liteim::setTcpNoDelay(liteim::kInvalidFd).isOk());
    EXPECT_FALSE(liteim::setKeepAlive(liteim::kInvalidFd).isOk());
    EXPECT_FALSE(liteim::getSocketError(liteim::kInvalidFd, socket_error).isOk());
}

TEST(SocketUtilTest, CloseFdInvalidatesDescriptorAndCanBeRepeated) {
    int fd = liteim::kInvalidFd;
    ASSERT_TRUE(liteim::createNonBlockingSocket(fd).isOk());
    const int closed_fd = fd;

    const auto first_close = liteim::closeFd(fd);

    ASSERT_TRUE(first_close.isOk()) << first_close.message();
    EXPECT_EQ(fd, liteim::kInvalidFd);

    errno = 0;
    EXPECT_EQ(::fcntl(closed_fd, F_GETFL, 0), -1);
    EXPECT_EQ(errno, EBADF);

    const auto second_close = liteim::closeFd(fd);

    EXPECT_TRUE(second_close.isOk()) << second_close.message();
    EXPECT_EQ(fd, liteim::kInvalidFd);
}

TEST(SocketUtilTest, GetSocketErrorReturnsCurrentSoError) {
    int fd = liteim::kInvalidFd;
    ASSERT_TRUE(liteim::createNonBlockingSocket(fd).isOk());
    FdGuard guard(fd);

    int socket_error = -1;

    const auto status = liteim::getSocketError(fd, socket_error);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(socket_error, 0);
}
