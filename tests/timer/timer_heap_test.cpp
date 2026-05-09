#include "liteim/timer/TimerHeap.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

TEST(TimerHeapTest, PopExpiredRunsCallbacksInDeadlineOrder) {
    liteim::TimerHeap timers;
    std::vector<int> fired;

    const auto late = timers.add(30, [&fired]() { fired.push_back(3); });
    const auto early = timers.add(10, [&fired]() { fired.push_back(1); });
    const auto middle = timers.add(20, [&fired]() { fired.push_back(2); });

    EXPECT_NE(late, early);
    EXPECT_NE(early, middle);
    EXPECT_EQ(timers.activeTimerCount(), 3U);
    EXPECT_EQ(timers.nextExpirationMilliseconds(), 10);

    const auto fired_count = timers.popExpired(20);

    EXPECT_EQ(fired_count, 2U);
    EXPECT_EQ(fired, (std::vector<int>{1, 2}));
    EXPECT_EQ(timers.activeTimerCount(), 1U);
    EXPECT_EQ(timers.nextExpirationMilliseconds(), 30);

    EXPECT_EQ(timers.popExpired(30), 1U);
    EXPECT_EQ(fired, (std::vector<int>{1, 2, 3}));
    EXPECT_TRUE(timers.empty());
}

TEST(TimerHeapTest, CancelUsesLazyDeletionWithoutRemovingNewTimer) {
    liteim::TimerHeap timers;
    std::vector<int> fired;

    const auto cancelled = timers.add(10, [&fired]() { fired.push_back(1); });
    timers.cancel(cancelled);
    const auto replacement = timers.add(10, [&fired]() { fired.push_back(2); });

    EXPECT_NE(cancelled, replacement);
    EXPECT_EQ(timers.activeTimerCount(), 1U);
    EXPECT_EQ(timers.nextExpirationMilliseconds(), 10);

    const auto fired_count = timers.popExpired(10);

    EXPECT_EQ(fired_count, 1U);
    EXPECT_EQ(fired, (std::vector<int>{2}));
    EXPECT_TRUE(timers.empty());
}

TEST(TimerHeapTest, PopExpiredIgnoresFutureTimers) {
    liteim::TimerHeap timers;
    int fired_count = 0;

    timers.add(100, [&fired_count]() { ++fired_count; });

    EXPECT_EQ(timers.popExpired(99), 0U);
    EXPECT_EQ(fired_count, 0);
    EXPECT_FALSE(timers.empty());
    EXPECT_EQ(timers.nextExpirationMilliseconds(), 100);
}
