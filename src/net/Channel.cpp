#include "liteim/net/Channel.hpp"

#include <utility>

namespace liteim {

Channel::Channel(EventLoop* owner_loop, int fd) : owner_loop_(owner_loop), fd_(fd) {}

int Channel::fd() const noexcept {
    return fd_;
}

std::uint32_t Channel::events() const noexcept {
    return events_;
}

std::uint32_t Channel::revents() const noexcept {
    return revents_;
}

void Channel::setRevents(std::uint32_t revents) noexcept {
    revents_ = revents;
}

bool Channel::isNoneEvent() const noexcept {
    return events_ == kNoneEvent;
}

bool Channel::isReading() const noexcept {
    return (events_ & kReadEvent) != 0;
}

bool Channel::isWriting() const noexcept {
    return (events_ & kWriteEvent) != 0;
}

void Channel::enableReading() {
    events_ |= kReadEvent;
}

void Channel::disableReading() {
    events_ &= ~kReadEvent;
}

void Channel::enableWriting() {
    events_ |= kWriteEvent;
}

void Channel::disableWriting() {
    events_ &= ~kWriteEvent;
}

void Channel::disableAll() {
    events_ = kNoneEvent;
}

EventLoop* Channel::ownerLoop() const noexcept {
    return owner_loop_;
}

void Channel::setReadCallback(EventCallback callback) {
    read_callback_ = std::move(callback);
}

void Channel::setWriteCallback(EventCallback callback) {
    write_callback_ = std::move(callback);
}

void Channel::setCloseCallback(EventCallback callback) {
    close_callback_ = std::move(callback);
}

void Channel::setErrorCallback(EventCallback callback) {
    error_callback_ = std::move(callback);
}

}  // namespace liteim
