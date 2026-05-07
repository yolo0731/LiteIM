#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <netinet/in.h>
#include <string>

#include "liteim/net/SocketUtil.hpp"
#include "liteim/net/UniqueFd.hpp"

namespace liteim {

class Channel;
class EventLoop;

class Acceptor {
public:
    using NewConnectionCallback = std::function<void(int, const sockaddr_in&)>;

    Acceptor(EventLoop* loop, const std::string& listen_ip, std::uint16_t port);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    void setNewConnectionCallback(NewConnectionCallback callback);

    int listenFd() const noexcept;
    std::uint16_t port() const noexcept;
    bool listening() const noexcept;
    void close() noexcept;

private:
    void closeInLoop() noexcept;
    void handleRead();
    void handleAcceptError(int error_number) noexcept;
    void rejectOneConnectionAfterFdExhaustion() noexcept;

    EventLoop* loop_;
    UniqueFd listen_fd_;
    UniqueFd idle_fd_;
    std::unique_ptr<Channel> listen_channel_;
    std::uint16_t port_{0};
    bool listening_{false};
    NewConnectionCallback new_connection_callback_;
};

}  // namespace liteim
