#pragma once

#include <memory>

namespace liteim::net {

class Channel;
class Epoller;

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void loop();
    void quit();

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

private:
    bool quit_ = false;
    std::unique_ptr<Epoller> epoller_;
};

}  // namespace liteim::net
