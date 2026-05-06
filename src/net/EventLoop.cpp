#include "liteim/net/EventLoop.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/Epoller.hpp"

#include <stdexcept>

namespace liteim {

EventLoop::EventLoop()
    : thread_id_(std::this_thread::get_id()), epoller_(std::make_unique<Epoller>(this)) {}

EventLoop::~EventLoop() = default;

void EventLoop::quit() noexcept {
    quit_ = true;
}

void EventLoop::updateChannel(Channel* channel) {
    assertInLoopThread();
    const auto status = epoller_->updateChannel(channel);
    if (!status.isOk()) {
        throw std::runtime_error(status.message());
    }
}

void EventLoop::removeChannel(Channel* channel) {
    assertInLoopThread();
    const auto status = epoller_->removeChannel(channel);
    if (!status.isOk()) {
        throw std::runtime_error(status.message());
    }
}

bool EventLoop::isInLoopThread() const noexcept {
    return std::this_thread::get_id() == thread_id_;
}

void EventLoop::assertInLoopThread() const {
    if (!isInLoopThread()) {
        throw std::logic_error("EventLoop used from a different thread");
    }
}

}  // namespace liteim
