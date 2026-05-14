#include "liteim/net/SignalWatcher.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"

#include <errno.h>
#include <pthread.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace liteim {
namespace {

Status errnoStatus(const char* action, int error_number) {
    return Status::error(ErrorCode::IoError, std::string(action) + " failed with errno " +
                                                 std::to_string(error_number));
}
// 把 pthread 相关函数返回的错误码，包装成 Status 错误对象
Status pthreadStatus(const char* action, int error_number) {
    return Status::error(ErrorCode::IoError, std::string(action) + " failed with error " +
                                                 std::to_string(error_number));
}

}  // namespace

SignalWatcher::SignalWatcher(EventLoop* loop, std::vector<int> signals, SignalCallback callback)
    : loop_(loop), signals_(std::move(signals)), callback_(std::move(callback)) {
    if (loop_ == nullptr) {
        throw std::invalid_argument("SignalWatcher requires a valid EventLoop");
    }
    ::sigemptyset(&signal_set_);  // 初始化信号集合
    ::sigemptyset(&old_signal_set_);
}

SignalWatcher::~SignalWatcher() {
    if (loop_ != nullptr && !loop_->isInLoopThread()) {
        std::terminate();
    }
    stopInLoop();
}

Status SignalWatcher::start() {
    if (!loop_->isInLoopThread()) {
        return Status::error(ErrorCode::InvalidArgument,
                             "SignalWatcher must start in its owner EventLoop thread");
    }

    return startInLoop();
}

void SignalWatcher::stop() noexcept {
    if (loop_ == nullptr) {
        return;
    }

    if (!loop_->isInLoopThread()) {
        std::terminate();
    }

    stopInLoop();
}

bool SignalWatcher::started() const noexcept {
    return started_;
}

int SignalWatcher::signalFd() const noexcept {
    return signal_fd_.fd();
}

Status SignalWatcher::startInLoop() {
    loop_->assertInLoopThread();
    if (started_) {
        return Status::ok();
    }
    if (signals_.empty()) {
        return Status::error(ErrorCode::InvalidArgument,
                             "SignalWatcher requires at least one signal");
    }
    if (!callback_) {
        return Status::error(ErrorCode::InvalidArgument, "SignalWatcher requires a callback");
    }

    const auto signal_status = rebuildSignalSet();
    if (!signal_status.isOk()) {
        return signal_status;
    }

    const auto block_status = blockSignals();
    if (!block_status.isOk()) {
        return block_status;
    }

    signal_channel_.reset();
    // signalfd函数创建一个新的文件描述符，用于接收指定信号的通知。
    signal_fd_.reset(::signalfd(-1, &signal_set_, SFD_NONBLOCK | SFD_CLOEXEC));
    if (!signal_fd_) {
        const auto status = errnoStatus("signalfd", errno);
        restoreSignalMask();
        return status;
    }

    signal_channel_ = std::make_unique<Channel>(loop_, signal_fd_.fd());
    signal_channel_->setReadCallback([this]() { handleRead(); });
    try {
        signal_channel_->enableReading();
    } catch (const std::exception& ex) {
        signal_channel_.reset();
        signal_fd_.reset();
        restoreSignalMask();
        return Status::error(ErrorCode::IoError, ex.what());
    }

    channel_registered_ = true;
    started_ = true;
    return Status::ok();
}

void SignalWatcher::stopInLoop() noexcept {
    if (!started_ && !signal_fd_ && signal_channel_ == nullptr) {
        restoreSignalMask();
        return;
    }

    started_ = false;

    if (signal_channel_ != nullptr && channel_registered_) {
        try {
            loop_->removeChannel(signal_channel_.get());
        } catch (...) {
        }
        channel_registered_ = false;
    }

    signal_fd_.reset();
    if (!handling_signal_event_) {
        signal_channel_.reset();
    }
    restoreSignalMask();
}

Status SignalWatcher::rebuildSignalSet() {
    ::sigemptyset(&signal_set_);
    for (const int signo : signals_) {
        if (::sigaddset(&signal_set_, signo) < 0) {  // 把信号加入 signal_set_
            return errnoStatus("sigaddset", errno);
        }
    }
    return Status::ok();
}

Status SignalWatcher::blockSignals() {
    const int rc = ::pthread_sigmask(SIG_BLOCK, &signal_set_,
                                     &old_signal_set_);  // 保存之前的信号掩码到 old_signal_set_
    if (rc != 0) {
        return pthreadStatus("pthread_sigmask", rc);
    }
    signal_mask_saved_ = true;
    return Status::ok();
}

void SignalWatcher::restoreSignalMask() noexcept {
    if (!signal_mask_saved_) {
        return;
    }

    const int rc = ::pthread_sigmask(SIG_SETMASK, &old_signal_set_, nullptr);
    (void)rc;
    signal_mask_saved_ = false;
}

void SignalWatcher::handleRead() noexcept {
    if (!signal_fd_) {
        return;
    }

    handling_signal_event_ = true;
    while (signal_fd_) {
        signalfd_siginfo
            info{};  // 定义一个 signalfd_siginfo 结构体变量，用于存储从 signalfd 读取到的信号信息
        const auto n = ::read(signal_fd_.fd(), &info, sizeof(info));  // n 是实际读取到的字节数
        if (n == static_cast<ssize_t>(sizeof(info))) {
            try {
                callback_(static_cast<int>(info.ssi_signo));  // 传入信号编号,执行对应的回调函数
            } catch (...) {
            }
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {  // 非阻塞
            break;
        }
        break;
    }
    handling_signal_event_ = false;
}

}  // namespace liteim
