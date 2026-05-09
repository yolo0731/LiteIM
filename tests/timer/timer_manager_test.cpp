#include "liteim/timer/TimerManager.hpp"

#include "liteim/net/EventLoop.hpp"

#include <chrono>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(TimerManagerTest, TimerFdTickRunsExpiredTimer) {
    liteim::EventLoop loop;
    liteim::TimerManager manager(&loop, 10ms);
    bool fired = false;

    const auto start_status = manager.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();
    ASSERT_TRUE(manager.started());
    EXPECT_GE(manager.timerFd(), 0);

    manager.runAfter(1ms, [&]() {
        fired = true;
        manager.stop();
        loop.quit();
    });

    loop.loop();

    EXPECT_TRUE(fired);
    EXPECT_FALSE(manager.started());
}

TEST(TimerManagerTest, CancelledTimerDoesNotRun) {
    liteim::EventLoop loop;
    liteim::TimerManager manager(&loop, 10ms);
    bool cancelled_fired = false;
    bool stop_fired = false;

    const auto start_status = manager.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    const auto cancelled = manager.runAfter(1ms, [&]() { cancelled_fired = true; });
    manager.cancel(cancelled);
    manager.runAfter(25ms, [&]() {
        stop_fired = true;
        manager.stop();
        loop.quit();
    });

    loop.loop();

    EXPECT_FALSE(cancelled_fired);
    EXPECT_TRUE(stop_fired);
    EXPECT_FALSE(manager.started());
}
