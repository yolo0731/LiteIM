#include "liteim/net/EventLoop.hpp"

#include "liteim/base/Logger.hpp"
#include "liteim/net/Channel.hpp"
#include "liteim/net/Epoller.hpp"

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>

namespace liteim
{
    namespace
    {
        class LoopingGuard
        {
        public:
            LoopingGuard(std::atomic_bool& looping, std::atomic_bool& loop_exited)
                : looping_(looping), loop_exited_(loop_exited)
            {
                bool expected = false;
                if (!looping_.compare_exchange_strong(expected, true))
                {
                    throw std::logic_error("EventLoop::loop is already running");
                }
                loop_exited_ = false;
            }

            ~LoopingGuard()
            {
                looping_ = false;
                loop_exited_ = true;
            }

            LoopingGuard(const LoopingGuard&) = delete;
            LoopingGuard& operator=(const LoopingGuard&) = delete;

        private:
            std::atomic_bool& looping_;
            std::atomic_bool& loop_exited_;
        };

        int createEventFd()
        {
            const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (fd < 0)
            {
                throw std::runtime_error("eventfd failed with errno " + std::to_string(errno));
            }

            return fd;
        }

    } // namespace

    EventLoop::EventLoop()
        : thread_id_(std::this_thread::get_id()),
          epoller_(std::make_unique<Epoller>(this)),
          wakeup_fd_(createEventFd()),
          wakeup_channel_(std::make_unique<Channel>(this, wakeup_fd_.fd()))
    {
        wakeup_channel_->setReadCallback([this]()
                                         { handleWakeup(); });
        wakeup_channel_->enableReading();
    }

    EventLoop::~EventLoop()
    {
        if (wakeup_channel_ != nullptr)
        {
            const auto status = epoller_->removeChannel(wakeup_channel_.get());
            (void)status;
        }
        wakeup_channel_.reset();
        wakeup_fd_.reset();
    }

    void EventLoop::loop()
    {
        assertInLoopThread();
        LoopingGuard looping_guard(looping_, loop_exited_);
        Epoller::ChannelList active_channels;

        while (true)
        {
            doPendingTasks();
            if (quit_.load())
            {
                break;
            }

            const auto status = epoller_->poll(-1, active_channels); // -1表示无限等待直到有事件发生
            if (!status.isOk())
            {
                throw std::runtime_error(status.message());
            }

            for (auto *channel : active_channels)
            {
                if (channel != nullptr)
                {
                    try
                    {
                        channel->handleEvent();
                    }
                    catch (const std::exception& ex)
                    {
                        Logger::get()->error("EventLoop channel callback failed: {}", ex.what());
                    }
                    catch (...)
                    {
                        Logger::get()->error("EventLoop channel callback failed with unknown exception");
                    }
                }
            }

            doPendingTasks();
            if (quit_.load())
            {
                break;
            }
        }
    }

    void EventLoop::quit() noexcept
    {
        quit_ = true;
        if (!isInLoopThread())
        {
            wakeup();
        }
    }

    void EventLoop::runInLoop(Functor task)
    {
        if (!task)
        {
            return;
        }

        if (isInLoopThread())
        {
            task();
            return;
        }

        queueInLoop(std::move(task));
    }

    void EventLoop::queueInLoop(Functor task)
    {
        if (!task)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_tasks_.push_back(std::move(task));
        }

        if (!isInLoopThread() || calling_pending_tasks_.load())
        {
            wakeup();
        }
    }

    void EventLoop::updateChannel(Channel *channel)
    {
        assertInLoopThread();
        const auto status = epoller_->updateChannel(channel);
        if (!status.isOk())
        {
            throw std::runtime_error(status.message());
        }
    }

    void EventLoop::removeChannel(Channel *channel)
    {
        assertInLoopThread();
        const auto status = epoller_->removeChannel(channel);
        if (!status.isOk())
        {
            throw std::runtime_error(status.message());
        }
    }

    bool EventLoop::isInLoopThread() const noexcept
    {
        return std::this_thread::get_id() == thread_id_;
    }

    bool EventLoop::isStopped() const noexcept
    {
        return loop_exited_.load() && !looping_.load();
    }

    void EventLoop::assertInLoopThread() const
    {
        if (!isInLoopThread())
        {
            throw std::logic_error("EventLoop used from a different thread");
        }
    }

    void EventLoop::wakeup() noexcept
    {
        if (!wakeup_fd_)
        {
            return;
        }

        eventfd_t value = 1;
        while (::eventfd_write(wakeup_fd_.fd(), value) < 0)
        {                       // 给 wakeup_fd_ 对应的 eventfd 内部计数器加 1,只要计数器大于 0，这个 fd 就会变成可读状态，唤醒 epoll_wait
            if (errno == EINTR) // 如果被信号中断了，就继续写入, EINTR 不代表 fd 出错，只表示这次系统调用被信号打断了
            {
                continue;
            }
            return;
        }
    }

    void EventLoop::handleWakeup() noexcept
    {
        if (!wakeup_fd_)
        {
            return;
        }

        eventfd_t value = 0;
        while (::eventfd_read(wakeup_fd_.fd(), &value) < 0) // 读出 wakeup_fd_ 内部计数器的值,如果计数器的值大于 0，读出后就会将计数器减 1, 存放到 value 里，当计数器的值变为 0 时，wakeup_fd_ 就会变为不可读状态
        {
            if (errno == EINTR)
            {
                continue;
            }
            return;
        }
    }

    void EventLoop::doPendingTasks()
    {
        std::vector<Functor> tasks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks.swap(pending_tasks_);
        }

        calling_pending_tasks_ = true;
        for (auto &task : tasks)
        {
            if (task)
            {
                try
                {
                    task();
                }
                catch (const std::exception& ex)
                {
                    Logger::get()->error("EventLoop pending task failed: {}", ex.what());
                }
                catch (...)
                {
                    Logger::get()->error("EventLoop pending task failed with unknown exception");
                }
            }
        }
        calling_pending_tasks_ = false;
    }

} // namespace liteim
