#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/net/UniqueFd.hpp"

#include <signal.h>

#include <functional>
#include <memory>
#include <vector>

namespace liteim {

class Channel;
class EventLoop;

class SignalWatcher {
public:
    using SignalCallback = std::function<void(int)>;

    SignalWatcher(EventLoop* loop, std::vector<int> signals, SignalCallback callback);
    ~SignalWatcher();

    SignalWatcher(const SignalWatcher&) = delete;
    SignalWatcher& operator=(const SignalWatcher&) = delete;

    Status start();
    void stop() noexcept;

    bool started() const noexcept;
    int signalFd() const noexcept;

private:
    Status startInLoop();
    void stopInLoop() noexcept;
    Status rebuildSignalSet();
    Status blockSignals();
    void restoreSignalMask() noexcept;
    void handleRead() noexcept;

    EventLoop* loop_;
    std::vector<int> signals_;
    SignalCallback callback_;
    sigset_t signal_set_{};
    sigset_t old_signal_set_{};
    bool signal_mask_saved_{false};
    UniqueFd signal_fd_;
    std::unique_ptr<Channel> signal_channel_;
    bool started_{false};
    bool channel_registered_{false};
    bool handling_signal_event_{false};
};

} // namespace liteim
