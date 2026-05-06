#include "liteim/net/EventLoop.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

namespace {

class PipePair {
public:
    PipePair() {
        int fds[2] = {-1, -1};
        const int rc = ::pipe(fds);
        EXPECT_EQ(rc, 0);
        read_fd_ = fds[0];
        write_fd_ = fds[1];
    }

    PipePair(const PipePair&) = delete;
    PipePair& operator=(const PipePair&) = delete;

    ~PipePair() {
        auto read_fd = read_fd_;
        auto write_fd = write_fd_;
        const auto read_close = liteim::closeFd(read_fd);
        const auto write_close = liteim::closeFd(write_fd);
        (void)read_close;
        (void)write_close;
    }

    int readFd() const noexcept {
        return read_fd_;
    }

    int writeFd() const noexcept {
        return write_fd_;
    }

private:
    int read_fd_{liteim::kInvalidFd};
    int write_fd_{liteim::kInvalidFd};
};

struct LoopThreadHandle {
    liteim::EventLoop* loop{nullptr};
    std::mutex mutex;
    std::condition_variable ready;
};

liteim::EventLoop* waitForLoop(LoopThreadHandle& handle) {
    std::unique_lock<std::mutex> lock(handle.mutex);
    handle.ready.wait(lock, [&handle]() { return handle.loop != nullptr; });
    return handle.loop;
}

}  // namespace

TEST(EventLoopTest, RunInLoopExecutesImmediatelyOnOwnerThread) {
    liteim::EventLoop loop;
    int count = 0;

    loop.runInLoop([&count]() { ++count; });

    EXPECT_EQ(count, 1);
}

TEST(EventLoopTest, QueueInLoopFromOtherThreadWakesAndExecutesTask) {
    LoopThreadHandle handle;
    std::atomic_bool ran{false};

    std::thread loop_thread([&handle]() {
        liteim::EventLoop loop;
        {
            std::lock_guard<std::mutex> lock(handle.mutex);
            handle.loop = &loop;
        }
        handle.ready.notify_one();
        loop.loop();
    });

    auto* loop = waitForLoop(handle);
    loop->queueInLoop([loop, &ran]() {
        ran = true;
        loop->quit();
    });

    loop_thread.join();
    EXPECT_TRUE(ran.load());
}

TEST(EventLoopTest, LoopHandlesRegisteredFdEvent) {
    PipePair pipe;
    LoopThreadHandle handle;
    std::atomic_bool handled{false};

    std::thread loop_thread([&handle, &pipe, &handled]() {
        liteim::EventLoop loop;
        liteim::Channel channel(&loop, pipe.readFd());
        channel.setReadCallback([&loop, &pipe, &handled]() {
            char value = '\0';
            const auto n = ::read(pipe.readFd(), &value, sizeof(value));
            EXPECT_EQ(n, 1);
            EXPECT_EQ(value, 'x');
            handled = true;
            loop.quit();
        });
        channel.enableReading();

        {
            std::lock_guard<std::mutex> lock(handle.mutex);
            handle.loop = &loop;
        }
        handle.ready.notify_one();
        loop.loop();
    });

    (void)waitForLoop(handle);
    ASSERT_EQ(::write(pipe.writeFd(), "x", 1), 1);

    loop_thread.join();
    EXPECT_TRUE(handled.load());
}

TEST(EventLoopTest, QueueInLoopRunsMultipleTasksAfterWakeup) {
    LoopThreadHandle handle;
    std::atomic_int count{0};

    std::thread loop_thread([&handle]() {
        liteim::EventLoop loop;
        {
            std::lock_guard<std::mutex> lock(handle.mutex);
            handle.loop = &loop;
        }
        handle.ready.notify_one();
        loop.loop();
    });

    auto* loop = waitForLoop(handle);
    for (int i = 0; i < 5; ++i) {
        loop->queueInLoop([loop, &count]() {
            const int current = ++count;
            if (current == 5) {
                loop->quit();
            }
        });
    }

    loop_thread.join();
    EXPECT_EQ(count.load(), 5);
}
