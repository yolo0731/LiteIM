#include "liteim/net/TcpServer.hpp"

#include "liteim/base/Status.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/Session.hpp"
#include "liteim/protocol/Packet.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

TEST(ReactorInterfaceTest, TcpServerHeaderIsSelfContained) {
    static_assert(!std::is_copy_constructible_v<liteim::TcpServer>);
    static_assert(!std::is_copy_assignable_v<liteim::TcpServer>);

    using TcpServer = liteim::TcpServer;
    using MessageCallback = std::function<void(const liteim::Session::Ptr&, const liteim::Packet&)>;
    static_assert(std::is_same_v<TcpServer::MessageCallback, MessageCallback>);
    static_assert(std::is_same_v<decltype(&TcpServer::setMessageCallback), void (TcpServer::*)(MessageCallback)>);
    static_assert(std::is_same_v<decltype(&TcpServer::setHeartbeatOptions),
                                 void (TcpServer::*)(std::chrono::milliseconds, std::chrono::milliseconds)>);
    static_assert(std::is_same_v<decltype(&TcpServer::setSessionOutputHighWaterMark),
                                 void (TcpServer::*)(std::size_t)>);
    static_assert(std::is_same_v<decltype(&TcpServer::start), void (TcpServer::*)()>);
    static_assert(std::is_same_v<decltype(&TcpServer::stop), void (TcpServer::*)() noexcept>);
    static_assert(std::is_same_v<decltype(&TcpServer::port), std::uint16_t (TcpServer::*)() const noexcept>);
    static_assert(std::is_same_v<decltype(&TcpServer::sessionCount), std::size_t (TcpServer::*)() const>);
    static_assert(std::is_same_v<decltype(&TcpServer::started), bool (TcpServer::*)() const noexcept>);
    static_assert(std::is_same_v<decltype(&TcpServer::sendToSession),
                                 liteim::Status (TcpServer::*)(std::uint64_t, const liteim::Packet&)>);

    SUCCEED();
}
