#include "liteim/net/EventLoop.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/Epoller.hpp"

#include <memory>

namespace liteim::net {
namespace {

constexpr int kPollTimeoutMs = 10000;

}  // namespace

EventLoop::EventLoop() : epoller_(std::make_unique<Epoller>()) {}

EventLoop::~EventLoop() = default;

void EventLoop::loop() {
    while (!quit_.load()) {
        const auto active_events = epoller_->poll(kPollTimeoutMs);
        for (const auto& active_event : active_events) {
            if (active_event.channel == nullptr) {
                continue;
            }
            active_event.channel->handleEvent();
        }
    }
}

void EventLoop::quit() {
    quit_.store(true);
}

void EventLoop::updateChannel(Channel* channel) {
    epoller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel) {
    epoller_->removeChannel(channel);
}

}  // namespace liteim::net
