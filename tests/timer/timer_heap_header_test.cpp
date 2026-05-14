#include "liteim/timer/TimerHeap.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

#include <gtest/gtest.h>

TEST(TimerInterfaceTest, TimerHeapHeaderIsSelfContained) {
    static_assert(!std::is_copy_constructible_v<liteim::TimerHeap>);
    static_assert(!std::is_copy_assignable_v<liteim::TimerHeap>);

    using TimerHeap = liteim::TimerHeap;
    using TimerId = std::uint64_t;
    using TimerCallback = std::function<void()>;

    static_assert(std::is_same_v<TimerHeap::TimerId, TimerId>);
    static_assert(std::is_same_v<TimerHeap::TimerCallback, TimerCallback>);
    static_assert(std::is_same_v<decltype(&TimerHeap::add),
                                 TimerId (TimerHeap::*)(std::int64_t, TimerCallback)>);
    static_assert(std::is_same_v<decltype(&TimerHeap::cancel), void (TimerHeap::*)(TimerId)>);
    static_assert(
        std::is_same_v<decltype(&TimerHeap::popExpired), std::size_t (TimerHeap::*)(std::int64_t)>);
    static_assert(std::is_same_v<decltype(&TimerHeap::nextExpirationMilliseconds),
                                 std::int64_t (TimerHeap::*)()>);
    static_assert(
        std::is_same_v<decltype(&TimerHeap::activeTimerCount), std::size_t (TimerHeap::*)() const>);
    static_assert(std::is_same_v<decltype(&TimerHeap::empty), bool (TimerHeap::*)() const>);

    SUCCEED();
}
