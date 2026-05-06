#pragma once

#include <sys/epoll.h>

#include <unordered_map>
#include <vector>

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

    ChannelList poll(int timeout_ms);
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

private:
    EventLoop* owner_loop_;
    int epoll_fd_{-1};
    std::vector<epoll_event> events_;
    std::unordered_map<int, Channel*> channels_;
};

}  // namespace liteim
