#pragma once

#include <sys/epoll.h>

#include <unordered_map>
#include <vector>

#include "liteim/base/Status.hpp"

namespace liteim {

class Channel;
class EventLoop;

class Epoller {
public:
    using ChannelList = std::vector<Channel*>;

    explicit Epoller(EventLoop* owner_loop);
    ~Epoller();

    Epoller(const Epoller&) = delete;
    Epoller& operator=(const Epoller&) = delete;

    Status poll(int timeout_ms, ChannelList& active_channels);
    Status updateChannel(Channel* channel);
    Status removeChannel(Channel* channel);

private:
    Status validateChannelOwner(Channel* channel) const;

    EventLoop* owner_loop_;
    int epoll_fd_{-1};
    std::vector<epoll_event> events_;
    std::unordered_map<int, Channel*> channels_;
};

} // namespace liteim
