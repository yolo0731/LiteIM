#pragma once

#include <cstdint>
#include <functional>
#include <sys/epoll.h>

namespace liteim::net {

class EventLoop;

class Channel {
public:
    using EventCallback = std::function<void()>;

    static constexpr std::uint32_t kNoneEvent = 0;
    static constexpr std::uint32_t kReadEvent = EPOLLIN | EPOLLPRI;
    static constexpr std::uint32_t kWriteEvent = EPOLLOUT;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    int fd() const;
    std::uint32_t events() const;
    std::uint32_t revents() const;
    void setRevents(std::uint32_t revents);

    bool isNoneEvent() const;
    bool isWriting() const;

    void enableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();

    void setReadCallback(EventCallback callback);
    void setWriteCallback(EventCallback callback);
    void setCloseCallback(EventCallback callback);
    void setErrorCallback(EventCallback callback);

    void handleEvent();

private:
    void update();

    EventLoop* loop_ = nullptr;
    int fd_ = -1;
    std::uint32_t events_ = kNoneEvent;
    std::uint32_t revents_ = kNoneEvent;

    EventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback close_callback_;
    EventCallback error_callback_;
};

}  // namespace liteim::net
