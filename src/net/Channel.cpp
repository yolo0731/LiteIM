#include "liteim/net/Channel.hpp"

#include <utility>

namespace liteim::net {

Channel::Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd) {}

Channel::~Channel() = default;

int Channel::fd() const {
    return fd_;
}

std::uint32_t Channel::events() const {
    return events_;
}

std::uint32_t Channel::revents() const {
    return revents_;
}

void Channel::setRevents(std::uint32_t revents) {
    revents_ = revents;
}

bool Channel::isNoneEvent() const {
    return events_ == kNoneEvent;
}

bool Channel::isWriting() const {
    return (events_ & kWriteEvent) != 0;
}

void Channel::enableReading() {
    events_ |= kReadEvent;
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

}  // namespace liteim::net
