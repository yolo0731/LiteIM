#include "liteim/net/EventLoopThreadPool.hpp"

#include "liteim/net/EventLoop.hpp"

#include <stdexcept>

namespace liteim {

EventLoopThreadPool::EventLoopThreadPool(EventLoop* base_loop, std::size_t thread_count)
    : base_loop_(base_loop), thread_count_(thread_count) {
    if (base_loop_ == nullptr) {
        throw std::invalid_argument("EventLoopThreadPool requires a valid base EventLoop");
    }
}

EventLoopThreadPool::~EventLoopThreadPool() {
    stop();
}

void EventLoopThreadPool::start() {
    if (started_) {
        return;
    }

    threads_.reserve(thread_count_);
    loops_.reserve(thread_count_);

    try {
        for (std::size_t i = 0; i < thread_count_; ++i) {
            auto thread = std::make_unique<EventLoopThread>();
            auto* loop = thread->startLoop();
            loops_.push_back(loop);
            threads_.push_back(std::move(thread));
        }
        started_ = true;
    } catch (...) {
        stop();
        throw;
    }
}

void EventLoopThreadPool::stop() noexcept {
    for (auto& thread : threads_) {
        if (thread != nullptr) {
            thread->stop();
        }
    }

    threads_.clear();
    loops_.clear();
    next_ = 0;
    started_ = false;
}

EventLoop* EventLoopThreadPool::getNextLoop() {
    if (!started_) {
        throw std::logic_error("EventLoopThreadPool must be started before selecting a loop");
    }

    if (loops_.empty()) {
        return base_loop_;
    }

    auto* loop = loops_[next_];
    next_ = (next_ + 1) % loops_.size();
    return loop;
}

const std::vector<EventLoop*>& EventLoopThreadPool::loops() const noexcept {
    return loops_;
}

std::size_t EventLoopThreadPool::threadCount() const noexcept {
    return thread_count_;
}

bool EventLoopThreadPool::started() const noexcept {
    return started_;
}

}  // namespace liteim
