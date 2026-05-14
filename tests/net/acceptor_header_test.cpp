#include "liteim/net/Acceptor.hpp"

#include <cstdint>
#include <functional>
#include <netinet/in.h>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

TEST(ReactorInterfaceTest, AcceptorHeaderIsSelfContained) {
    static_assert(!std::is_copy_constructible_v<liteim::Acceptor>);
    static_assert(!std::is_copy_assignable_v<liteim::Acceptor>);

    using Acceptor = liteim::Acceptor;
    using Callback = std::function<void(liteim::UniqueFd, const sockaddr_in&)>;
    static_assert(std::is_same_v<Acceptor::NewConnectionCallback, Callback>);
    static_assert(std::is_same_v<decltype(&Acceptor::setNewConnectionCallback),
                                 void (Acceptor::*)(Callback)>);
    static_assert(
        std::is_same_v<decltype(&Acceptor::listenFd), int (Acceptor::*)() const noexcept>);
    static_assert(
        std::is_same_v<decltype(&Acceptor::port), std::uint16_t (Acceptor::*)() const noexcept>);
    static_assert(
        std::is_same_v<decltype(&Acceptor::listening), bool (Acceptor::*)() const noexcept>);
    static_assert(std::is_same_v<decltype(&Acceptor::close), void (Acceptor::*)() noexcept>);

    SUCCEED();
}
