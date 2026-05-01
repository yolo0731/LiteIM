#pragma once

#include <cstdint>
#include <sys/epoll.h>
#include <unordered_set>
#include <vector>

namespace liteim::net {

class Channel;

class Epoller {
public:
    struct ActiveEvent {
        Channel* channel = nullptr;
        std::uint32_t events = 0;
    };

    Epoller();
    ~Epoller();

    Epoller(const Epoller&) = delete;
    Epoller& operator=(const Epoller&) = delete;

    std::vector<ActiveEvent> poll(int timeout_ms);
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

private:
    int epoll_fd_ = -1;
    std::vector<epoll_event> events_;
    std::unordered_set<int> registered_fds_;
};

}  // namespace liteim::net
