#pragma once

#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

namespace liteim {

class EventLoop;

class EventLoopThread {
public:
    EventLoopThread() = default;
    ~EventLoopThread();

    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    EventLoop* startLoop();
    void stop() noexcept;
    bool running() const noexcept;

private:
    void threadFunc() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
    EventLoop* loop_{nullptr};
    bool started_{false};
    bool running_{false};
    std::exception_ptr startup_exception_;
};

}  // namespace liteim
