#pragma once

#include "liteim/net/Buffer.hpp"
#include "liteim/net/Channel.hpp"
#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/protocol/Packet.hpp"

#include <cstddef>
#include <functional>

namespace liteim::net {

class EventLoop;

class Session {
public:
    using MessageCallback = std::function<void(Session&, const protocol::Packet&)>;
    using CloseCallback = std::function<void(int)>;

    Session(EventLoop* loop, int fd);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void start();
    void sendPacket(const protocol::Packet& packet);
    void close();

    int fd() const;
    bool started() const;
    bool closed() const;
    std::size_t pendingOutputBytes() const;

    void setMessageCallback(MessageCallback callback);
    void setCloseCallback(CloseCallback callback);

private:
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();
    void closeConnection(bool notify);

    EventLoop* loop_ = nullptr;
    int fd_ = -1;
    Channel channel_;
    protocol::FrameDecoder decoder_;
    Buffer output_buffer_;
    MessageCallback message_callback_;
    CloseCallback close_callback_;
    bool started_ = false;
    bool closed_ = false;
};

}  // namespace liteim::net
