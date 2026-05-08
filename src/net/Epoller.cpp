#include "liteim/net/Epoller.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/net/Channel.hpp"

#include <cerrno>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace liteim {
namespace {

constexpr int kInitialEventListSize = 16;

Status invalidEpollStatus(const char* action) {
    return Status::error(ErrorCode::IoError, std::string(action) + " failed because epoll fd is invalid");
}

Status errnoStatus(const char* action, int error_number) {
    return Status::error(ErrorCode::IoError,
                         std::string(action) + " failed with errno " + std::to_string(error_number));
}

Status validateChannel(const char* action, Channel* channel) {
    if (channel == nullptr || channel->fd() < 0) {
        return Status::error(ErrorCode::InvalidArgument, std::string(action) + " requires a valid channel");
    }

    return Status::ok();
}

epoll_event makeEpollEvent(Channel* channel) {
    epoll_event event{};
    event.events = channel->events();
    event.data.ptr = channel;
    return event;
}

}  // namespace

Epoller::Epoller(EventLoop* owner_loop)
    : owner_loop_(owner_loop), epoll_fd_(::epoll_create1(EPOLL_CLOEXEC)), events_(kInitialEventListSize) {
    if (epoll_fd_ < 0) {
        throw std::runtime_error("epoll_create1 failed with errno " + std::to_string(errno));
    }
}

Epoller::~Epoller() {
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

Status Epoller::poll(int timeout_ms, ChannelList& active_channels) {
    active_channels.clear();
    if (epoll_fd_ < 0) {
        return invalidEpollStatus("epoll_wait");
    }

    const int event_count = ::epoll_wait(epoll_fd_,
                                         events_.data(),
                                         static_cast<int>(events_.size()),
                                         timeout_ms);
    if (event_count < 0) {
        if (errno == EINTR) {
            return Status::ok();
        }
        return errnoStatus("epoll_wait", errno);
    }

    active_channels.reserve(static_cast<std::size_t>(event_count));
    for (int index = 0; index < event_count; ++index) {
        auto* channel = static_cast<Channel*>(events_[static_cast<std::size_t>(index)].data.ptr);
        if (channel == nullptr) {
            continue;
        }

        channel->setRevents(events_[static_cast<std::size_t>(index)].events);
        active_channels.push_back(channel);
    }

    if (event_count == static_cast<int>(events_.size())) {
        events_.resize(events_.size() * 2);
    }

    return Status::ok();
}

Status Epoller::updateChannel(Channel* channel) {
    const auto validation = validateChannel("updateChannel", channel);
    if (!validation.isOk()) {
        return validation;
    }
    if (epoll_fd_ < 0) {
        return invalidEpollStatus("epoll_ctl");
    }

    const int fd = channel->fd();
    auto found = channels_.find(fd);
    if (found == channels_.end()) {
        if (channel->isNoneEvent()) {
            return Status::error(ErrorCode::InvalidArgument, "cannot add channel without interested events");
        }

        auto event = makeEpollEvent(channel);
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) < 0) {
            return errnoStatus("epoll_ctl(ADD)", errno);
        }
        channels_.emplace(fd, channel);
        return Status::ok();
    }

    if (found->second != channel) {
        return Status::error(ErrorCode::InvalidArgument, "fd already belongs to a different channel");
    }

    if (channel->isNoneEvent()) {
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
            return errnoStatus("epoll_ctl(DEL)", errno);
        }
        channels_.erase(found);
        channel->setRevents(Channel::kNoneEvent);
        return Status::ok();
    }

    auto event = makeEpollEvent(channel);
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) < 0) {
        return errnoStatus("epoll_ctl(MOD)", errno);
    }

    return Status::ok();
}

Status Epoller::removeChannel(Channel* channel) {
    const auto validation = validateChannel("removeChannel", channel);
    if (!validation.isOk()) {
        return validation;
    }
    if (epoll_fd_ < 0) {
        return invalidEpollStatus("epoll_ctl");
    }

    const int fd = channel->fd();
    auto found = channels_.find(fd);
    if (found == channels_.end() || found->second != channel) {
        return Status::error(ErrorCode::InvalidArgument, "channel is not registered in epoller");
    }

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        return errnoStatus("epoll_ctl(DEL)", errno);
    }

    channels_.erase(found);
    channel->setRevents(Channel::kNoneEvent);
    return Status::ok();
}

}  // namespace liteim
