#include "TestUtil.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <unistd.h>

#include <sys/epoll.h>
#include <vector>

namespace {

using liteim::net::Channel;
using liteim::net::EventLoop;
using liteim::net::closeFd;
using liteim::tests::TestCase;
using liteim::tests::expect;

class PipeGuard {
public:
    PipeGuard() {
        int fds[2] = {-1, -1};
        const int rc = ::pipe(fds);
        expect(rc == 0, "pipe should be created for channel test");
        read_fd_ = fds[0];
        write_fd_ = fds[1];
    }

    ~PipeGuard() {
        closeFd(read_fd_);
        closeFd(write_fd_);
    }

    PipeGuard(const PipeGuard&) = delete;
    PipeGuard& operator=(const PipeGuard&) = delete;

    int readFd() const {
        return read_fd_;
    }

    int writeFd() const {
        return write_fd_;
    }

    void writeByte(char byte = 'x') const {
        const ssize_t written = ::write(write_fd_, &byte, 1);
        expect(written == 1, "pipe write should make read fd readable");
    }

private:
    int read_fd_ = -1;
    int write_fd_ = -1;
};

void testEnableReadingRegistersWithEventLoop() {
    PipeGuard pipe;
    EventLoop loop;
    Channel channel(&loop, pipe.readFd());

    bool read_called = false;
    char received = '\0';
    channel.setReadCallback([&]() {
        read_called = true;
        const ssize_t n = ::read(pipe.readFd(), &received, 1);
        expect(n == 1, "read callback should consume one byte");
        loop.quit();
    });

    channel.enableReading();
    pipe.writeByte('c');

    loop.loop();
    loop.removeChannel(&channel);

    expect(read_called, "enableReading should register the channel through EventLoop");
    expect(received == 'c', "registered read channel should receive the written byte");
    expect((channel.revents() & EPOLLIN) != 0, "poll result should be stored in channel revents");
}

void testDisableWritingRemovesWriteInterest() {
    PipeGuard pipe;
    EventLoop loop;
    Channel write_channel(&loop, pipe.writeFd());

    bool write_called = false;
    write_channel.setWriteCallback([&]() {
        write_called = true;
    });

    write_channel.enableWriting();
    write_channel.disableWriting();

    Channel stop_channel(&loop, pipe.readFd());
    stop_channel.setReadCallback([&]() {
        char received = '\0';
        const ssize_t n = ::read(pipe.readFd(), &received, 1);
        expect(n == 1, "stop channel should consume one byte");
        loop.quit();
    });
    stop_channel.enableReading();

    pipe.writeByte();
    loop.loop();
    loop.removeChannel(&stop_channel);
    loop.removeChannel(&write_channel);

    expect(!write_called, "disableWriting should remove write interest from epoll");
    expect(!write_channel.isWriting(), "disableWriting should clear the write bit");
    expect(write_channel.isNoneEvent(), "write-only channel should have no interested events after disableWriting");
}

void testDisableAllCanReEnableReading() {
    PipeGuard pipe;
    EventLoop loop;
    Channel channel(&loop, pipe.readFd());

    bool read_called = false;
    channel.setReadCallback([&]() {
        read_called = true;
        char received = '\0';
        const ssize_t n = ::read(pipe.readFd(), &received, 1);
        expect(n == 1, "read callback should consume one byte after re-enable");
        loop.quit();
    });

    channel.enableReading();
    channel.disableAll();
    expect(channel.isNoneEvent(), "disableAll should clear every interested event");

    channel.enableReading();
    pipe.writeByte();

    loop.loop();
    loop.removeChannel(&channel);

    expect(read_called, "channel should be able to re-register after disableAll");
}

void testHandleEventDispatchesCallbacksByReturnedEvents() {
    Channel channel(nullptr, 42);

    int close_calls = 0;
    int error_calls = 0;
    int read_calls = 0;
    int write_calls = 0;

    channel.setCloseCallback([&]() {
        ++close_calls;
    });
    channel.setErrorCallback([&]() {
        ++error_calls;
    });
    channel.setReadCallback([&]() {
        ++read_calls;
    });
    channel.setWriteCallback([&]() {
        ++write_calls;
    });

    channel.setRevents(EPOLLHUP);
    channel.handleEvent();

    channel.setRevents(EPOLLERR);
    channel.handleEvent();

    channel.setRevents(EPOLLIN | EPOLLOUT);
    channel.handleEvent();

    expect(close_calls == 1, "EPOLLHUP without EPOLLIN should dispatch close callback");
    expect(error_calls == 1, "EPOLLERR should dispatch error callback");
    expect(read_calls == 1, "EPOLLIN should dispatch read callback");
    expect(write_calls == 1, "EPOLLOUT should dispatch write callback");
}

}  // namespace

std::vector<TestCase> channelTests() {
    return {
        {"channel enableReading registers with event loop", testEnableReadingRegistersWithEventLoop},
        {"channel disableWriting removes write interest", testDisableWritingRemovesWriteInterest},
        {"channel disableAll can re-enable reading", testDisableAllCanReEnableReading},
        {"channel handleEvent dispatches callbacks by returned events", testHandleEventDispatchesCallbacksByReturnedEvents},
    };
}
