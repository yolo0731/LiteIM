#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "liteim/net/EventLoopThread.hpp"

namespace liteim {

class EventLoop;

class EventLoopThreadPool {
public:
    EventLoopThreadPool(EventLoop* base_loop, std::size_t thread_count);
    ~EventLoopThreadPool();

    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;

    void start();
    void stop() noexcept;
    EventLoop* getNextLoop();

    const std::vector<EventLoop*>& loops() const noexcept;
    std::size_t threadCount() const noexcept;
    bool started() const noexcept;

private:
    EventLoop* base_loop_;
    std::size_t thread_count_;
    std::size_t next_{0};
    bool started_{false};
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};

} // namespace liteim
