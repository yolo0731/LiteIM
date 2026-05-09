#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/timer/TimerHeap.hpp"

#include <chrono>
#include <memory>

namespace liteim {

class Channel;
class EventLoop;

class TimerManager {
public:
    using TimerId = TimerHeap::TimerId;
    using TimerCallback = TimerHeap::TimerCallback;

    TimerManager(EventLoop* loop, std::chrono::milliseconds tick_interval);
    ~TimerManager();

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;

    Status start();
    void stop() noexcept;
    TimerId runAfter(std::chrono::milliseconds delay, TimerCallback callback);
    void cancel(TimerId timer_id);

    bool started() const noexcept;
    int timerFd() const noexcept;

private:
    Status startInLoop();
    void stopInLoop() noexcept;
    void handleRead() noexcept;
    std::int64_t steadyNowMilliseconds() const noexcept;

    EventLoop* loop_;
    std::chrono::milliseconds tick_interval_;
    UniqueFd timer_fd_;
    std::unique_ptr<Channel> timer_channel_;
    TimerHeap timers_;
    bool started_{false};
    bool channel_registered_{false};
    bool handling_timer_event_{false};
};

} // namespace liteim
