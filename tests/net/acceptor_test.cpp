#include "liteim/net/Acceptor.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

class FdGuard {
public:
    explicit FdGuard(int fd = liteim::kInvalidFd) : fd_(fd) {}

    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = liteim::kInvalidFd;
    }

    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = liteim::kInvalidFd;
        }
        return *this;
    }

    ~FdGuard() {
        reset();
    }

    int fd() const noexcept {
        return fd_;
    }

    void reset(int fd = liteim::kInvalidFd) noexcept {
        if (fd_ >= 0) {
            const auto status = liteim::closeFd(fd_);
            (void)status;
        }
        fd_ = fd;
    }

    int release() noexcept {
        const int fd = fd_;
        fd_ = liteim::kInvalidFd;
        return fd;
    }

private:
    int fd_;
};

struct LoopThreadHandle {
    liteim::EventLoop* loop{nullptr};
    std::uint16_t port{0};
    std::mutex mutex;
    std::condition_variable ready;
};

std::uint16_t waitForPort(LoopThreadHandle& handle) {
    std::unique_lock<std::mutex> lock(handle.mutex);
    handle.ready.wait(lock, [&handle]() { return handle.port != 0; });
    return handle.port;
}

sockaddr_in loopbackAddress(std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    const int rc = ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    EXPECT_EQ(rc, 1);
    return address;
}

FdGuard connectTo(std::uint16_t port) {
    FdGuard client(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    EXPECT_GE(client.fd(), 0);
    if (client.fd() < 0) {
        return client;
    }

    const auto address = loopbackAddress(port);
    const int rc = ::connect(client.fd(),
                             reinterpret_cast<const sockaddr*>(&address),
                             static_cast<socklen_t>(sizeof(address)));
    EXPECT_EQ(rc, 0) << "connect errno=" << errno;
    return client;
}

int getSocketFlag(int fd) {
    return ::fcntl(fd, F_GETFL, 0);
}

int getFdFlag(int fd) {
    return ::fcntl(fd, F_GETFD, 0);
}

}  // namespace

TEST(AcceptorTest, ServerCanListenOnEphemeralPort) {
    liteim::EventLoop loop;
    liteim::Acceptor acceptor(&loop, "127.0.0.1", 0);

    int accept_conn = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(accept_conn));
    const int rc = ::getsockopt(acceptor.listenFd(), SOL_SOCKET, SO_ACCEPTCONN, &accept_conn, &len);

    ASSERT_EQ(rc, 0);
    EXPECT_TRUE(acceptor.listening());
    EXPECT_GE(acceptor.listenFd(), 0);
    EXPECT_GT(acceptor.port(), 0);
    EXPECT_EQ(accept_conn, 1);
}

TEST(AcceptorTest, ClientConnectionTriggersNewConnectionCallback) {
    LoopThreadHandle handle;
    std::atomic_bool accepted{false};
    std::atomic_bool accepted_fd_nonblocking{false};
    std::atomic_bool accepted_fd_cloexec{false};

    std::thread loop_thread([&handle, &accepted, &accepted_fd_nonblocking, &accepted_fd_cloexec]() {
        liteim::EventLoop loop;
        liteim::Acceptor acceptor(&loop, "127.0.0.1", 0);
        acceptor.setNewConnectionCallback(
            [&loop, &accepted, &accepted_fd_nonblocking, &accepted_fd_cloexec](int fd,
                                                                               const sockaddr_in& peer) {
                FdGuard accepted_fd(fd);
                EXPECT_EQ(peer.sin_family, AF_INET);
                accepted_fd_nonblocking = (getSocketFlag(fd) & O_NONBLOCK) != 0;
                accepted_fd_cloexec = (getFdFlag(fd) & FD_CLOEXEC) != 0;
                accepted = true;
                loop.quit();
            });

        {
            std::lock_guard<std::mutex> lock(handle.mutex);
            handle.loop = &loop;
            handle.port = acceptor.port();
        }
        handle.ready.notify_one();
        loop.loop();
    });

    auto client = connectTo(waitForPort(handle));
    ASSERT_GE(client.fd(), 0);

    loop_thread.join();
    EXPECT_TRUE(accepted.load());
    EXPECT_TRUE(accepted_fd_nonblocking.load());
    EXPECT_TRUE(accepted_fd_cloexec.load());
}

TEST(AcceptorTest, MultiplePendingConnectionsAreAccepted) {
    constexpr int kConnectionCount = 3;
    LoopThreadHandle handle;
    std::atomic_int accepted_count{0};

    std::thread loop_thread([&handle, &accepted_count]() {
        liteim::EventLoop loop;
        liteim::Acceptor acceptor(&loop, "127.0.0.1", 0);
        acceptor.setNewConnectionCallback([&loop, &accepted_count](int fd, const sockaddr_in&) {
            FdGuard accepted_fd(fd);
            const int count = ++accepted_count;
            if (count == kConnectionCount) {
                loop.quit();
            }
        });

        {
            std::lock_guard<std::mutex> lock(handle.mutex);
            handle.loop = &loop;
            handle.port = acceptor.port();
        }
        handle.ready.notify_one();
        loop.loop();
    });

    const auto port = waitForPort(handle);
    std::vector<FdGuard> clients;
    for (int index = 0; index < kConnectionCount; ++index) {
        clients.push_back(connectTo(port));
        ASSERT_GE(clients.back().fd(), 0);
    }

    loop_thread.join();
    EXPECT_EQ(accepted_count.load(), kConnectionCount);
}

TEST(AcceptorTest, ClosedListenSocketRejectsNewConnections) {
    liteim::EventLoop loop;
    liteim::Acceptor acceptor(&loop, "127.0.0.1", 0);
    const auto port = acceptor.port();

    acceptor.close();

    EXPECT_FALSE(acceptor.listening());
    EXPECT_EQ(acceptor.listenFd(), liteim::kInvalidFd);

    FdGuard client(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    ASSERT_GE(client.fd(), 0);
    const auto address = loopbackAddress(port);
    errno = 0;
    const int rc = ::connect(client.fd(),
                             reinterpret_cast<const sockaddr*>(&address),
                             static_cast<socklen_t>(sizeof(address)));

    EXPECT_EQ(rc, -1);
    EXPECT_EQ(errno, ECONNREFUSED);
}
