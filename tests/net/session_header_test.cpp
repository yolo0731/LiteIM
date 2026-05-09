#include "liteim/net/Session.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

TEST(ReactorInterfaceTest, SessionHeaderIsSelfContained) {
    using Session = liteim::Session;

    static_assert(std::is_constructible_v<Session, liteim::EventLoop*, liteim::UniqueFd, std::uint64_t>);
    static_assert(!std::is_constructible_v<Session, liteim::EventLoop*, int, std::uint64_t>);
    static_assert(std::is_same_v<decltype(&Session::pendingOutputBytes), std::size_t (Session::*)() const>);

    SUCCEED();
}
