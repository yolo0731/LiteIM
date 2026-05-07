#include "liteim/net/Channel.hpp"

#include "liteim/net/EventLoop.hpp"

#include <sys/epoll.h>

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
    update();
}

void Channel::disableReading() {
    events_ &= ~kReadEvent;
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

void Channel::tie(const std::shared_ptr<void>& owner) {  // void表示不关心 owner 类型
    tie_ = owner;
    tied_ = true;
}

void Channel::handleEvent() {
    std::shared_ptr<void> guard;
    if (tied_) {
        guard = tie_.lock();  // std::weak_ptr 的成员函数 lock() 将 weak_ptr 转换为
                              // shared_ptr，如果原来的对象已经被销毁了，那么 lock()
                              // 返回一个空的 shared_ptr
        if (guard == nullptr) {
            return;
        }
    }

    const auto active_events = revents_;

    if ((active_events & EPOLLHUP) != 0 && (active_events & EPOLLIN) == 0) {
        if (close_callback_) {
            close_callback_();
        }
        return;
    }

    if ((active_events & EPOLLERR) != 0) {
        if (error_callback_) {
            error_callback_();
        }
        return;
    }

    if ((active_events & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) != 0) {
        if (read_callback_) {
            read_callback_();
        }
    }

    if ((active_events & EPOLLOUT) != 0) {
        if (write_callback_) {
            write_callback_();
        }
    }
}

void Channel::update() {
    if (owner_loop_ != nullptr) {
        owner_loop_->updateChannel(this);
    }
}

}  // namespace liteim
