#pragma once

#include "liteim/net/Channel.hpp"

#include <netinet/in.h>

#include <cstdint>
#include <functional>
#include <string>

namespace liteim::net {

class EventLoop;

class Acceptor {
public:
    using NewConnectionCallback = std::function<void(int, const sockaddr_in&)>;

    Acceptor(EventLoop* loop, const std::string& listen_ip, std::uint16_t port);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    void listen();
    void close();
    bool listening() const;
    int listenFd() const;
    std::uint16_t port() const;

    void setNewConnectionCallback(NewConnectionCallback callback);

private:
    void handleRead();

    EventLoop* loop_ = nullptr;
    int listen_fd_ = -1;
    Channel accept_channel_;
    NewConnectionCallback new_connection_callback_;
    bool listening_ = false;
};

}  // namespace liteim::net
