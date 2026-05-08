#include "liteim/net/EventLoopThread.hpp"

#include <type_traits>

#include <gtest/gtest.h>

TEST(ReactorInterfaceTest, EventLoopThreadHeaderIsSelfContained) {
    static_assert(!std::is_copy_constructible_v<liteim::EventLoopThread>);
    static_assert(!std::is_copy_assignable_v<liteim::EventLoopThread>);

    using Thread = liteim::EventLoopThread;
    static_assert(std::is_same_v<decltype(&Thread::startLoop), liteim::EventLoop* (Thread::*)()>);
    static_assert(std::is_same_v<decltype(&Thread::stop), void (Thread::*)() noexcept>);
    static_assert(std::is_same_v<decltype(&Thread::running), bool (Thread::*)() const noexcept>);

    SUCCEED();
}
