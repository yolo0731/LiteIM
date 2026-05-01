#include "liteim/net/Epoller.hpp"

#include "liteim/net/Channel.hpp"

#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace liteim::net {
namespace {

constexpr int kInitialEventListSize = 16;

std::system_error makeSystemError(const char* operation) {
    return std::system_error(errno, std::generic_category(), operation);
}

}  // namespace

Epoller::Epoller()
    : epoll_fd_(::epoll_create1(EPOLL_CLOEXEC)), events_(kInitialEventListSize) {
    if (epoll_fd_ < 0) {
        throw makeSystemError("epoll_create1");
    }
}

Epoller::~Epoller() {
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
}

std::vector<Epoller::ActiveEvent> Epoller::poll(int timeout_ms) {
    const int ready = ::epoll_wait(
        epoll_fd_,
        events_.data(),
        static_cast<int>(events_.size()),
        timeout_ms);

    if (ready < 0) {
        if (errno == EINTR) {
            return {};
        }
        throw makeSystemError("epoll_wait");
    }

    std::vector<ActiveEvent> active_events;
    active_events.reserve(static_cast<std::size_t>(ready));

    for (int i = 0; i < ready; ++i) {
        auto* channel = static_cast<Channel*>(events_[static_cast<std::size_t>(i)].data.ptr);
        const auto events = events_[static_cast<std::size_t>(i)].events;
        if (channel == nullptr) {
            continue;
        }

        channel->setRevents(events);
        active_events.push_back(ActiveEvent{channel, events});
    }

    if (ready == static_cast<int>(events_.size())) {
        events_.resize(events_.size() * 2);
    }

    return active_events;
}

void Epoller::updateChannel(Channel* channel) {
    if (channel == nullptr) {
        throw std::invalid_argument("epoller update channel is null");
    }
    if (channel->fd() < 0) {
        throw std::invalid_argument("epoller update channel fd is invalid");
    }

    epoll_event event{};
    event.events = channel->events();
    event.data.ptr = channel;

    const bool exists = registered_fds_.find(channel->fd()) != registered_fds_.end();
    const int operation = exists ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

    if (::epoll_ctl(epoll_fd_, operation, channel->fd(), &event) < 0) {
        throw makeSystemError(exists ? "epoll_ctl(EPOLL_CTL_MOD)" : "epoll_ctl(EPOLL_CTL_ADD)");
    }

    if (!exists) {
        registered_fds_.insert(channel->fd());
    }
}

void Epoller::removeChannel(Channel* channel) {
    if (channel == nullptr || channel->fd() < 0) {
        return;
    }

    const auto it = registered_fds_.find(channel->fd());
    if (it == registered_fds_.end()) {
        return;
    }

    epoll_event event{};
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, channel->fd(), &event) < 0) {
        throw makeSystemError("epoll_ctl(EPOLL_CTL_DEL)");
    }

    registered_fds_.erase(it);
}

}  // namespace liteim::net
