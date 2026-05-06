#include "liteim/net/EventLoop.hpp"

#include <type_traits>

#include <gtest/gtest.h>

TEST(ReactorInterfaceTest, EventLoopHeaderIsSelfContained) {
    static_assert(!std::is_copy_constructible_v<liteim::EventLoop>);
    static_assert(!std::is_copy_assignable_v<liteim::EventLoop>);

    using Loop = liteim::EventLoop;
    using ChannelFn = void (Loop::*)(liteim::Channel*);
    static_assert(std::is_same_v<decltype(&Loop::updateChannel), ChannelFn>);
    static_assert(std::is_same_v<decltype(&Loop::removeChannel), ChannelFn>);
    static_assert(std::is_same_v<decltype(&Loop::loop), void (Loop::*)()>);
    static_assert(std::is_same_v<decltype(&Loop::quit), void (Loop::*)() noexcept>);
    static_assert(std::is_same_v<decltype(&Loop::isInLoopThread), bool (Loop::*)() const noexcept>);

    SUCCEED();
}
