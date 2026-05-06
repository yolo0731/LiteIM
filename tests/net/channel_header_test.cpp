#include "liteim/net/Channel.hpp"

#include <memory>
#include <type_traits>

#include <gtest/gtest.h>

TEST(ReactorInterfaceTest, ChannelHeaderIsSelfContained) {
    static_assert(!std::is_default_constructible_v<liteim::Channel>);
    static_assert(!std::is_copy_constructible_v<liteim::Channel>);
    static_assert(!std::is_copy_assignable_v<liteim::Channel>);

    using Callback = liteim::Channel::EventCallback;
    using CallbackSetter = void (liteim::Channel::*)(Callback);
    using TieSetter = void (liteim::Channel::*)(const std::shared_ptr<void>&);
    static_assert(std::is_same_v<decltype(&liteim::Channel::setReadCallback), CallbackSetter>);
    static_assert(std::is_same_v<decltype(&liteim::Channel::setWriteCallback), CallbackSetter>);
    static_assert(std::is_same_v<decltype(&liteim::Channel::setCloseCallback), CallbackSetter>);
    static_assert(std::is_same_v<decltype(&liteim::Channel::setErrorCallback), CallbackSetter>);
    static_assert(std::is_same_v<decltype(&liteim::Channel::tie), TieSetter>);

    EXPECT_EQ(liteim::Channel::kNoneEvent, 0U);
    EXPECT_NE(liteim::Channel::kReadEvent, liteim::Channel::kNoneEvent);
    EXPECT_NE(liteim::Channel::kWriteEvent, liteim::Channel::kNoneEvent);
}
