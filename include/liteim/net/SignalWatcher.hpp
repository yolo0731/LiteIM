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
    Status rebuildSignalSet();          // 把 signals_ 中的信号加入 signal_set_
    Status blockSignals();              // 阻塞 signals_ 中的信号，防止它们被默认处理
    void restoreSignalMask() noexcept;  // 恢复原来的信号掩码
    void handleRead() noexcept;

    EventLoop* loop_;
    std::vector<int> signals_;  // 要监听的信号列表
    SignalCallback callback_;
    sigset_t signal_set_{};      // 当前的信号集合
    sigset_t old_signal_set_{};  // 保存原来的信号集合，以便在停止时恢复
    bool signal_mask_saved_{false};
    UniqueFd signal_fd_;
    std::unique_ptr<Channel> signal_channel_;
    bool started_{false};
    bool channel_registered_{false};
    bool handling_signal_event_{
        false};  // 防止 在 Channel 正在执行事件回调时，把这个 Channel 自己析构掉。
};

}  // namespace liteim
