#pragma once

#include <atomic>
#include <memory>
#include <thread>

namespace liteim {

class Channel;
class Epoller;

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void loop();
    void quit() noexcept;

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    bool isInLoopThread() const noexcept;
    void assertInLoopThread() const;

private:
    bool looping_{false};
    std::atomic_bool quit_{false};
    const std::thread::id thread_id_;
    std::unique_ptr<Epoller> epoller_;
};

}  // namespace liteim
