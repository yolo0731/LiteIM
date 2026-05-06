#include "liteim/net/UniqueFd.hpp"

#include "liteim/net/SocketUtil.hpp"

#include <cerrno>
#include <fcntl.h>
#include <utility>

#include <gtest/gtest.h>

namespace {

int openSocket() {
    int fd = liteim::kInvalidFd;
    EXPECT_TRUE(liteim::createNonBlockingSocket(fd).isOk());
    return fd;
}

bool isClosed(int fd) {
    errno = 0;
    const int rc = ::fcntl(fd, F_GETFL, 0);
    return rc == -1 && errno == EBADF;
}

}  // namespace

TEST(UniqueFdTest, DestructorClosesOwnedFd) {
    const int fd = openSocket();
    ASSERT_GE(fd, 0);

    {
        liteim::UniqueFd guard(fd);
        EXPECT_EQ(guard.fd(), fd);
    }

    EXPECT_TRUE(isClosed(fd));
}

TEST(UniqueFdTest, ReleaseReturnsFdWithoutClosing) {
    const int fd = openSocket();
    ASSERT_GE(fd, 0);

    liteim::UniqueFd guard(fd);
    const int released_fd = guard.release();

    EXPECT_EQ(released_fd, fd);
    EXPECT_EQ(guard.fd(), liteim::kInvalidFd);
    EXPECT_FALSE(isClosed(fd));

    int close_fd = released_fd;
    EXPECT_TRUE(liteim::closeFd(close_fd).isOk());
}

TEST(UniqueFdTest, MoveTransfersOwnership) {
    const int fd = openSocket();
    ASSERT_GE(fd, 0);

    liteim::UniqueFd first(fd);
    liteim::UniqueFd second(std::move(first));

    EXPECT_EQ(first.fd(), liteim::kInvalidFd);
    EXPECT_EQ(second.fd(), fd);
}

TEST(UniqueFdTest, ResetClosesPreviousFd) {
    const int first_fd = openSocket();
    const int second_fd = openSocket();
    ASSERT_GE(first_fd, 0);
    ASSERT_GE(second_fd, 0);

    liteim::UniqueFd guard(first_fd);
    guard.reset(second_fd);

    EXPECT_TRUE(isClosed(first_fd));
    EXPECT_EQ(guard.fd(), second_fd);
}
