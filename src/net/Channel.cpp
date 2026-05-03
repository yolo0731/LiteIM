#include "liteim/net/Channel.hpp"

#include "liteim/net/EventLoop.hpp"

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
    update();
}

void Channel::enableWriting() {
    events_ |= kWriteEvent;
    update();
}

void Channel::disableWriting() {
    events_ &= ~kWriteEvent;
    update();
}

void Channel::disableAll() {
    events_ = kNoneEvent;
    update();
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

void Channel::handleEvent() {
    if ((revents_ & EPOLLHUP) != 0 && (revents_ & EPOLLIN) == 0) {
        if (close_callback_) {
            close_callback_();
        }
    }

    if ((revents_ & EPOLLERR) != 0) {
        if (error_callback_) {
            error_callback_();
        }
    }

    if ((revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) != 0) {
        if (read_callback_) {
            read_callback_();
        }
    }

    if ((revents_ & EPOLLOUT) != 0) {
        if (write_callback_) {
            write_callback_();
        }
    }
}

void Channel::update() {
    if (loop_ == nullptr) {
        return;
    }

    if (isNoneEvent()) {
        loop_->removeChannel(this);
        return;
    }

    loop_->updateChannel(this);
}

}  // namespace liteim::net
