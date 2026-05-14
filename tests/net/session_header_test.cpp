#include "liteim/net/Session.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

template <typename T, typename = void> struct HasFdMethod : std::false_type {};

template <typename T> struct HasFdMethod<T, std::void_t<decltype(&T::fd)>> : std::true_type {};

}  // namespace

TEST(ReactorInterfaceTest, SessionHeaderIsSelfContained) {
    using Session = liteim::Session;

    static_assert(
        std::is_constructible_v<Session, liteim::EventLoop*, liteim::UniqueFd, std::uint64_t>);
    static_assert(!std::is_constructible_v<Session, liteim::EventLoop*, int, std::uint64_t>);
    static_assert(std::is_enum_v<liteim::SessionState>);
    static_assert(!HasFdMethod<Session>::value);
    static_assert(
        std::is_same_v<decltype(&Session::setOutputHighWaterMark), void (Session::*)(std::size_t)>);
    static_assert(std::is_same_v<decltype(&Session::outputHighWaterMark),
                                 std::size_t (Session::*)() const noexcept>);
    static_assert(
        std::is_same_v<decltype(&Session::pendingOutputBytes), std::size_t (Session::*)() const>);

    SUCCEED();
}
