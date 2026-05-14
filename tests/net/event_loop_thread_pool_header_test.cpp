#include "liteim/net/EventLoopThreadPool.hpp"

#include <cstddef>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

TEST(ReactorInterfaceTest, EventLoopThreadPoolHeaderIsSelfContained) {
    static_assert(!std::is_copy_constructible_v<liteim::EventLoopThreadPool>);
    static_assert(!std::is_copy_assignable_v<liteim::EventLoopThreadPool>);

    using Pool = liteim::EventLoopThreadPool;
    using LoopList = std::vector<liteim::EventLoop*>;

    static_assert(std::is_same_v<decltype(&Pool::start), void (Pool::*)()>);
    static_assert(std::is_same_v<decltype(&Pool::stop), void (Pool::*)() noexcept>);
    static_assert(std::is_same_v<decltype(&Pool::getNextLoop), liteim::EventLoop* (Pool::*)()>);
    static_assert(
        std::is_same_v<decltype(&Pool::loops), const LoopList& (Pool::*)() const noexcept>);
    static_assert(
        std::is_same_v<decltype(&Pool::threadCount), std::size_t (Pool::*)() const noexcept>);
    static_assert(std::is_same_v<decltype(&Pool::started), bool (Pool::*)() const noexcept>);

    SUCCEED();
}
