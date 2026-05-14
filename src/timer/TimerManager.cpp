#include "liteim/timer/TimerManager.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"

#include <cerrno>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <sys/timerfd.h>
#include <unistd.h>

namespace liteim {
namespace {

Status errnoStatus(const char* action, int error_number) {
    return Status::error(ErrorCode::IoError, std::string(action) + " failed with errno " +
                                                 std::to_string(error_number));
}

// 把 C++ 的毫秒时间间隔，转换成 Linux timerfd_settime() 需要的 itimerspec 配置。
itimerspec makeIntervalSpec(std::chrono::milliseconds interval) {
    itimerspec spec{};
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        interval);  // timerfd_settime() 的时间参数是秒和纳秒的组合，所以先把毫秒转换成秒
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        interval - seconds);  // 再把剩下毫秒转换成纳秒

    // 设置定时器的初始过期时间
    spec.it_value.tv_sec = seconds.count();
    spec.it_value.tv_nsec = nanos.count();

    // 设置定时器的间隔时间，单位是秒和纳秒,这里将初始过期时间和间隔时间设置成一样的
    spec.it_interval.tv_sec = spec.it_value.tv_sec;
    spec.it_interval.tv_nsec = spec.it_value.tv_nsec;
    return spec;
}

}  // namespace

TimerManager::TimerManager(EventLoop* loop, std::chrono::milliseconds tick_interval)
    : loop_(loop), tick_interval_(tick_interval) {
    if (loop_ == nullptr) {
        throw std::invalid_argument("TimerManager requires a valid EventLoop");
    }
}

TimerManager::~TimerManager() {
    if (loop_ != nullptr && !loop_->isInLoopThread()) {
        std::terminate();
    }
    stopInLoop();
}

Status TimerManager::start() {
    if (!loop_->isInLoopThread()) {
        return Status::error(ErrorCode::InvalidArgument,
                             "TimerManager must start in its owner EventLoop thread");
    }

    return startInLoop();
}

void TimerManager::stop() noexcept {
    if (loop_ == nullptr) {
        return;
    }

    if (!loop_->isInLoopThread()) {
        std::terminate();
    }

    stopInLoop();
}

TimerManager::TimerId TimerManager::runAfter(std::chrono::milliseconds delay,
                                             TimerCallback callback) {
    loop_->assertInLoopThread();
    const auto delay_ms = delay.count() < 0 ? 0 : delay.count();
    return timers_.add(steadyNowMilliseconds() + delay_ms, std::move(callback));
}

void TimerManager::cancel(TimerId timer_id) {
    loop_->assertInLoopThread();
    timers_.cancel(timer_id);
}

bool TimerManager::started() const noexcept {
    return started_;
}

int TimerManager::timerFd() const noexcept {
    return timer_fd_.fd();
}

Status TimerManager::startInLoop() {
    loop_->assertInLoopThread();
    if (started_) {
        return Status::ok();
    }
    if (tick_interval_.count() <= 0) {
        return Status::error(ErrorCode::InvalidArgument,
                             "TimerManager tick interval must be positive");
    }

    timer_channel_.reset();
    timer_fd_.reset(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC));
    // CLOCK_MONOTONIC指用系统的单调时钟，避免了系统时间调整带来的影响
    if (!timer_fd_) {
        return errnoStatus("timerfd_create", errno);
    }

    auto spec =
        makeIntervalSpec(tick_interval_);  // 设置定时器的初始过期时间和间隔时间，单位是秒和纳秒
    if (::timerfd_settime(timer_fd_.fd(), 0, &spec, nullptr) < 0) {
        // 内核维护timerfd，每spec.it_interval时间到期一次，变得可读，直到被read读掉
        const auto status = errnoStatus("timerfd_settime", errno);
        timer_fd_.reset();
        return status;
    }

    timer_channel_ = std::make_unique<Channel>(loop_, timer_fd_.fd());
    timer_channel_->setReadCallback([this]() { handleRead(); });
    try {
        timer_channel_->enableReading();
    } catch (const std::exception& ex) {
        timer_channel_.reset();
        timer_fd_.reset();
        return Status::error(ErrorCode::IoError, ex.what());
    }

    channel_registered_ = true;
    started_ = true;
    return Status::ok();
}

void TimerManager::stopInLoop() noexcept {
    if (!started_ && !timer_fd_ && timer_channel_ == nullptr) {
        timers_.clear();
        return;
    }

    started_ = false;
    timers_.clear();

    if (timer_channel_ != nullptr && channel_registered_) {
        try {
            loop_->removeChannel(timer_channel_.get());
        } catch (...) {
        }
        channel_registered_ = false;
    }

    timer_fd_.reset();
    if (!handling_timer_event_) {
        timer_channel_.reset();
    }
}

void TimerManager::handleRead() noexcept {
    if (!timer_fd_) {
        return;
    }

    std::uint64_t expirations = 0;
    // 从上次读之后，定时器响了几次，定时器可能因为系统调度等原因没有及时被读掉，导致过期了好几次了
    while (true) {
        // timerfd 读操作必须要读出8字节，否则会一直触发可读事件，内核把读出来的 8 字节写到
        // expirations这块内存里，告诉我们定时器过期了多少次了
        const auto n = ::read(timer_fd_.fd(), &expirations, sizeof(expirations));
        if (n == static_cast<ssize_t>(sizeof(expirations))) {
            break;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        return;
    }

    handling_timer_event_ = true;
    try {
        (void)timers_.popExpired(steadyNowMilliseconds());
    } catch (...) {
    }
    handling_timer_event_ = false;
}

std::int64_t TimerManager::steadyNowMilliseconds() const noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace liteim
