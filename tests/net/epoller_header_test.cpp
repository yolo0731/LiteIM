#include "liteim/net/Epoller.hpp"

#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

TEST(ReactorInterfaceTest, EpollerHeaderIsSelfContained) {
    static_assert(!std::is_default_constructible_v<liteim::Epoller>);
    static_assert(!std::is_copy_constructible_v<liteim::Epoller>);
    static_assert(!std::is_copy_assignable_v<liteim::Epoller>);
    static_assert(std::is_same_v<liteim::Epoller::ChannelList, std::vector<liteim::Channel*>>);

    using Poller = liteim::Epoller;
    using PollFn = Poller::ChannelList (Poller::*)(int);
    using ChannelFn = void (Poller::*)(liteim::Channel*);
    static_assert(std::is_same_v<decltype(&Poller::poll), PollFn>);
    static_assert(std::is_same_v<decltype(&Poller::updateChannel), ChannelFn>);
    static_assert(std::is_same_v<decltype(&Poller::removeChannel), ChannelFn>);

    SUCCEED();
}
