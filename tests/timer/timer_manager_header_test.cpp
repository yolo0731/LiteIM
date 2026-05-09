#include "liteim/timer/TimerManager.hpp"

#include "liteim/base/Status.hpp"
#include "liteim/net/EventLoop.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <type_traits>

#include <gtest/gtest.h>

TEST(TimerInterfaceTest, TimerManagerHeaderIsSelfContained) {
    static_assert(!std::is_copy_constructible_v<liteim::TimerManager>);
    static_assert(!std::is_copy_assignable_v<liteim::TimerManager>);

    using TimerManager = liteim::TimerManager;
    using TimerId = std::uint64_t;
    using TimerCallback = std::function<void()>;

    static_assert(std::is_same_v<TimerManager::TimerId, TimerId>);
    static_assert(std::is_same_v<TimerManager::TimerCallback, TimerCallback>);
    static_assert(std::is_constructible_v<TimerManager, liteim::EventLoop*, std::chrono::milliseconds>);
    static_assert(std::is_same_v<decltype(&TimerManager::start), liteim::Status (TimerManager::*)()>);
    static_assert(std::is_same_v<decltype(&TimerManager::stop), void (TimerManager::*)() noexcept>);
    static_assert(std::is_same_v<decltype(&TimerManager::runAfter),
                                 TimerId (TimerManager::*)(std::chrono::milliseconds, TimerCallback)>);
    static_assert(std::is_same_v<decltype(&TimerManager::cancel), void (TimerManager::*)(TimerId)>);
    static_assert(std::is_same_v<decltype(&TimerManager::started), bool (TimerManager::*)() const noexcept>);
    static_assert(std::is_same_v<decltype(&TimerManager::timerFd), int (TimerManager::*)() const noexcept>);

    SUCCEED();
}
