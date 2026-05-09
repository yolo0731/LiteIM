#include "liteim/timer/TimerManager.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/timerfd.h>
#include <unistd.h>

namespace liteim {
namespace {

Status errnoStatus(const char* action, int error_number) {
    return Status::error(ErrorCode::IoError,
                         std::string(action) + " failed with errno " + std::to_string(error_number));
}

itimerspec makeIntervalSpec(std::chrono::milliseconds interval) {
    itimerspec spec{};
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(interval);
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(interval - seconds);
    spec.it_value.tv_sec = seconds.count();
    spec.it_value.tv_nsec = nanos.count();
    spec.it_interval.tv_sec = spec.it_value.tv_sec;
    spec.it_interval.tv_nsec = spec.it_value.tv_nsec;
    return spec;
}

} // namespace

TimerManager::TimerManager(EventLoop* loop, std::chrono::milliseconds tick_interval)
    : loop_(loop), tick_interval_(tick_interval) {
    if (loop_ == nullptr) {
        throw std::invalid_argument("TimerManager requires a valid EventLoop");
    }
}

TimerManager::~TimerManager() {
    stop();
}

Status TimerManager::start() {
    if (!loop_->isInLoopThread()) {
        return Status::error(ErrorCode::InvalidArgument, "TimerManager must start in its owner EventLoop thread");
    }

    return startInLoop();
}

void TimerManager::stop() noexcept {
    if (loop_ == nullptr) {
        return;
    }

    if (!loop_->isInLoopThread()) {
        try {
            loop_->queueInLoop([this]() { stopInLoop(); });
        } catch (...) {
        }
        return;
    }

    stopInLoop();
}

TimerManager::TimerId TimerManager::runAfter(std::chrono::milliseconds delay, TimerCallback callback) {
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
        return Status::error(ErrorCode::InvalidArgument, "TimerManager tick interval must be positive");
    }

    timer_channel_.reset();
    timer_fd_.reset(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC));
    if (!timer_fd_) {
        return errnoStatus("timerfd_create", errno);
    }

    auto spec = makeIntervalSpec(tick_interval_);
    if (::timerfd_settime(timer_fd_.fd(), 0, &spec, nullptr) < 0) {
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
    while (true) {
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

} // namespace liteim
