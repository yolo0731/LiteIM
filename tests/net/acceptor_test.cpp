#include "liteim/net/Acceptor.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"
#include "liteim/net/UniqueFd.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <stdexcept>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

class SoftFdLimitGuard {
public:
    SoftFdLimitGuard() = default;

    SoftFdLimitGuard(const SoftFdLimitGuard&) = delete;
    SoftFdLimitGuard& operator=(const SoftFdLimitGuard&) = delete;

    ~SoftFdLimitGuard() {
        if (active_) {
            const int rc = ::setrlimit(RLIMIT_NOFILE, &old_limit_);
            (void)rc;
        }
    }

    bool setSoftLimit(rlim_t soft_limit) {
        if (::getrlimit(RLIMIT_NOFILE, &old_limit_) != 0) {
            return false;
        }
        if (old_limit_.rlim_max < soft_limit) {
            return false;
        }

        rlimit new_limit = old_limit_;
        new_limit.rlim_cur = soft_limit;
        if (::setrlimit(RLIMIT_NOFILE, &new_limit) != 0) {
            return false;
        }

        active_ = true;
        return true;
    }

private:
    rlimit old_limit_{};
    bool active_{false};
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

liteim::UniqueFd connectTo(std::uint16_t port) {
    liteim::UniqueFd client(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
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

bool isClosed(int fd) {
    errno = 0;
    const int rc = ::fcntl(fd, F_GETFL, 0);
    return rc == -1 && errno == EBADF;
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
            [&loop, &accepted, &accepted_fd_nonblocking, &accepted_fd_cloexec](liteim::UniqueFd accepted_fd,
                                                                               const sockaddr_in& peer) {
                const int fd = accepted_fd.fd();
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
        acceptor.setNewConnectionCallback([&loop, &accepted_count](liteim::UniqueFd accepted_fd, const sockaddr_in&) {
            EXPECT_GE(accepted_fd.fd(), 0);
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
    std::vector<liteim::UniqueFd> clients;
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

    liteim::UniqueFd client(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    ASSERT_GE(client.fd(), 0);
    const auto address = loopbackAddress(port);
    errno = 0;
    const int rc = ::connect(client.fd(),
                             reinterpret_cast<const sockaddr*>(&address),
                             static_cast<socklen_t>(sizeof(address)));

    EXPECT_EQ(rc, -1);
    EXPECT_EQ(errno, ECONNREFUSED);
}

TEST(AcceptorTest, AcceptedFdIsClosedWhenCallbackThrowsBeforeTakingOwnership) {
    LoopThreadHandle handle;
    std::atomic_int callback_fd{liteim::kInvalidFd};
    std::atomic_bool callback_exception_escaped{false};

    std::thread loop_thread([&handle, &callback_fd, &callback_exception_escaped]() {
        liteim::EventLoop loop;
        liteim::Acceptor acceptor(&loop, "127.0.0.1", 0);
        acceptor.setNewConnectionCallback([&callback_fd](liteim::UniqueFd accepted_fd, const sockaddr_in&) {
            callback_fd = accepted_fd.fd();
            throw std::runtime_error("callback failed before taking fd ownership");
        });

        {
            std::lock_guard<std::mutex> lock(handle.mutex);
            handle.loop = &loop;
            handle.port = acceptor.port();
        }
        handle.ready.notify_one();

        try {
            loop.loop();
        } catch (...) {
            callback_exception_escaped = true;
        }
    });

    auto client = connectTo(waitForPort(handle));
    ASSERT_GE(client.fd(), 0);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (callback_fd.load() < 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!callback_exception_escaped.load()) {
        std::lock_guard<std::mutex> lock(handle.mutex);
        ASSERT_NE(handle.loop, nullptr);
        handle.loop->queueInLoop([loop = handle.loop]() { loop->quit(); });
    }

    loop_thread.join();

    EXPECT_FALSE(callback_exception_escaped.load());
    ASSERT_GE(callback_fd.load(), 0);
    EXPECT_TRUE(isClosed(callback_fd.load()));
    if (!isClosed(callback_fd.load())) {
        int leaked_fd = callback_fd.load();
        const auto status = liteim::closeFd(leaked_fd);
        (void)status;
    }
}

TEST(AcceptorTest, CloseFromNonOwnerThreadTerminates) {
    EXPECT_DEATH(
        {
            liteim::EventLoop loop;
            liteim::Acceptor acceptor(&loop, "127.0.0.1", 0);
            std::thread caller([&acceptor]() { acceptor.close(); });
            caller.join();
        },
        ".*");
}

TEST(AcceptorTest, FdExhaustionRejectsPendingConnectionWithoutLaterCallback) {
    constexpr rlim_t kSoftFdLimit = 64;
    LoopThreadHandle handle;
    std::atomic_int accepted_count{0};
    bool start_loop{false};
    std::mutex start_mutex;
    std::condition_variable start_ready;

    std::thread loop_thread([&handle, &accepted_count, &start_loop, &start_mutex, &start_ready]() {
        liteim::EventLoop loop;
        liteim::Acceptor acceptor(&loop, "127.0.0.1", 0);
        acceptor.setNewConnectionCallback([&accepted_count](liteim::UniqueFd accepted_fd, const sockaddr_in&) {
            EXPECT_GE(accepted_fd.fd(), 0);
            ++accepted_count;
        });

        {
            std::lock_guard<std::mutex> lock(handle.mutex);
            handle.loop = &loop;
            handle.port = acceptor.port();
        }
        handle.ready.notify_one();

        {
            std::unique_lock<std::mutex> lock(start_mutex);
            start_ready.wait(lock, [&start_loop]() { return start_loop; });
        }

        loop.loop();
    });

    const auto port = waitForPort(handle);
    auto client = connectTo(port);
    ASSERT_GE(client.fd(), 0);

    SoftFdLimitGuard limit_guard;
    if (!limit_guard.setSoftLimit(kSoftFdLimit)) {
        {
            std::lock_guard<std::mutex> lock(start_mutex);
            start_loop = true;
        }
        start_ready.notify_one();
        {
            std::lock_guard<std::mutex> lock(handle.mutex);
            ASSERT_NE(handle.loop, nullptr);
            handle.loop->queueInLoop([loop = handle.loop]() { loop->quit(); });
        }
        loop_thread.join();
        GTEST_SKIP() << "cannot lower RLIMIT_NOFILE for fd-exhaustion regression";
    }

    std::vector<liteim::UniqueFd> filler_fds;
    while (true) {
        errno = 0;
        const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            ASSERT_EQ(errno, EMFILE);
            break;
        }
        filler_fds.emplace_back(fd);
    }

    {
        std::lock_guard<std::mutex> lock(start_mutex);
        start_loop = true;
    }
    start_ready.notify_one();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    filler_fds.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::lock_guard<std::mutex> lock(handle.mutex);
        ASSERT_NE(handle.loop, nullptr);
        handle.loop->queueInLoop([loop = handle.loop]() { loop->quit(); });
    }

    loop_thread.join();

    EXPECT_EQ(accepted_count.load(), 0);
}
