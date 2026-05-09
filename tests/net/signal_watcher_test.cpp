#include "liteim/net/SignalWatcher.hpp"

#include "liteim/net/EventLoop.hpp"

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(SignalWatcherTest, SignalfdDispatchesSignalInOwnerLoop) {
    liteim::EventLoop loop;
    std::atomic_int observed_signal{0};
    std::atomic_bool loop_done{false};
    liteim::SignalWatcher watcher(&loop, std::vector<int>{SIGUSR1}, [&](int signo) {
        observed_signal = signo;
        loop.quit();
    });

    const auto status = watcher.start();
    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_TRUE(watcher.started());
    EXPECT_GE(watcher.signalFd(), 0);

    std::thread sender([]() {
        std::this_thread::sleep_for(20ms);
        (void)::kill(::getpid(), SIGUSR1);
    });
    std::thread watchdog([&]() {
        std::this_thread::sleep_for(1s);
        if (!loop_done.load()) {
            loop.queueInLoop([&loop]() { loop.quit(); });
        }
    });

    loop.loop();
    loop_done = true;
    sender.join();
    watchdog.join();
    watcher.stop();

    EXPECT_EQ(observed_signal.load(), SIGUSR1);
    EXPECT_FALSE(watcher.started());
}

TEST(SignalWatcherTest, StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis) {
    EXPECT_DEATH(
        {
            liteim::EventLoop loop;
            liteim::SignalWatcher watcher(&loop, std::vector<int>{SIGUSR2}, [](int) {});
            std::thread caller([&watcher]() { watcher.stop(); });
            caller.join();
        },
        ".*");
}
