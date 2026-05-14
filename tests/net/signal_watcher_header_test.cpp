#include "liteim/net/SignalWatcher.hpp"

#include "liteim/base/Status.hpp"
#include "liteim/net/EventLoop.hpp"

#include <functional>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

TEST(ReactorInterfaceTest, SignalWatcherHeaderIsSelfContained) {
    using SignalCallback = std::function<void(int)>;

    static_assert(std::is_constructible_v<liteim::SignalWatcher, liteim::EventLoop*,
                                          std::vector<int>, SignalCallback>);
    static_assert(std::is_same_v<decltype(&liteim::SignalWatcher::start),
                                 liteim::Status (liteim::SignalWatcher::*)()>);
    static_assert(std::is_same_v<decltype(&liteim::SignalWatcher::stop),
                                 void (liteim::SignalWatcher::*)() noexcept>);

    SUCCEED();
}
