#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <sys/epoll.h>

namespace liteim {

class EventLoop;

class Channel {
public:
    using EventCallback = std::function<void()>;

    inline static constexpr std::uint32_t kNoneEvent = 0;
    inline static constexpr std::uint32_t kReadEvent = static_cast<std::uint32_t>(EPOLLIN | EPOLLPRI);
    inline static constexpr std::uint32_t kWriteEvent = static_cast<std::uint32_t>(EPOLLOUT);

    Channel(EventLoop* owner_loop, int fd);
    ~Channel() = default;

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    int fd() const noexcept;
    std::uint32_t events() const noexcept;
    std::uint32_t revents() const noexcept;
    void setRevents(std::uint32_t revents) noexcept;

    bool isNoneEvent() const noexcept;
    bool isReading() const noexcept;
    bool isWriting() const noexcept;

    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();

    EventLoop* ownerLoop() const noexcept;
    void setReadCallback(EventCallback callback);
    void setWriteCallback(EventCallback callback);
    void setCloseCallback(EventCallback callback);
    void setErrorCallback(EventCallback callback);

    void tie(const std::shared_ptr<void>& owner);
    void handleEvent();

private:
    void handleEventWithGuard();
    void update();

    EventLoop* owner_loop_;
    const int fd_;
    std::uint32_t events_{kNoneEvent};
    std::uint32_t revents_{kNoneEvent};
    bool tied_{false};
    std::weak_ptr<void> tie_;
    EventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback close_callback_;
    EventCallback error_callback_;
};

}  // namespace liteim
