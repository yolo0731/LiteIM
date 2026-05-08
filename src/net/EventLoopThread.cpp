#include "liteim/net/EventLoopThread.hpp"

#include "liteim/net/EventLoop.hpp"

#include <stdexcept>
#include <utility>

namespace liteim {

EventLoopThread::~EventLoopThread() {
    stop();
}

EventLoop* EventLoopThread::startLoop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_) {
            throw std::logic_error("EventLoopThread has already been started");
        }
        started_ = true;
        running_ = true;
    }

    thread_ = std::thread([this]() { threadFunc(); });

    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this]() { return loop_ != nullptr || startup_exception_ != nullptr || !running_; });

    if (startup_exception_ != nullptr) {
        const auto exception = std::exchange(startup_exception_, nullptr);
        lock.unlock();
        if (thread_.joinable()) {
            thread_.join();
        }
        std::rethrow_exception(exception);
    }

    return loop_;
}

void EventLoopThread::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loop_ != nullptr) {
            loop_->quit();
        }
    }

    try {
        if (thread_.joinable()) {
            if (thread_.get_id() == std::this_thread::get_id()) {
                thread_.detach();
            } else {
                thread_.join();
            }
        }
    } catch (...) {
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = nullptr;
        running_ = false;
    }
}

bool EventLoopThread::running() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

void EventLoopThread::threadFunc() noexcept {
    try {
        EventLoop loop;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop_ = &loop;
        }
        condition_.notify_all();

        loop.loop();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop_ = nullptr;
            running_ = false;
        }
        condition_.notify_all();
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop_ = nullptr;
            running_ = false;
            startup_exception_ = std::current_exception();
        }
        condition_.notify_all();
    }
}

} // namespace liteim
