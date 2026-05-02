#include "TestUtil.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <unistd.h>

#include <stdexcept>
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
        expect(rc == 0, "pipe should be created for event loop test");
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

void testEventLoopDispatchesReadCallbackAndQuits() {
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
    loop.updateChannel(&channel);
    pipe.writeByte('r');

    loop.loop();
    loop.removeChannel(&channel);

    expect(read_called, "event loop should dispatch readable channel callback");
    expect(received == 'r', "read callback should receive the byte written before loop");
    expect((channel.revents() & EPOLLIN) != 0, "event loop should preserve returned read event");
}

void testEventLoopDispatchesWriteCallbackAndQuits() {
    PipeGuard pipe;
    EventLoop loop;
    Channel channel(&loop, pipe.writeFd());

    bool write_called = false;
    channel.setWriteCallback([&]() {
        write_called = true;
        loop.quit();
    });

    channel.enableWriting();
    loop.updateChannel(&channel);

    loop.loop();
    loop.removeChannel(&channel);

    expect(write_called, "event loop should dispatch writable channel callback");
    expect((channel.revents() & EPOLLOUT) != 0, "event loop should preserve returned write event");
}

void testEventLoopRemoveChannelStopsDispatch() {
    PipeGuard pipe;
    EventLoop loop;
    Channel read_channel(&loop, pipe.readFd());
    Channel write_channel(&loop, pipe.writeFd());

    bool read_called = false;
    read_channel.setReadCallback([&]() {
        read_called = true;
    });
    read_channel.enableReading();
    loop.updateChannel(&read_channel);
    loop.removeChannel(&read_channel);

    bool write_called = false;
    write_channel.setWriteCallback([&]() {
        write_called = true;
        loop.quit();
    });
    write_channel.enableWriting();
    loop.updateChannel(&write_channel);

    pipe.writeByte();
    loop.loop();
    loop.removeChannel(&write_channel);

    expect(write_called, "write callback should stop the test loop");
    expect(!read_called, "removed read channel should not be dispatched");
}

void testEventLoopQuitBeforeLoopReturnsImmediately() {
    EventLoop loop;

    loop.quit();
    loop.loop();

    expect(true, "loop should return immediately when quit was requested first");
}

void testEventLoopUpdateRejectsInvalidChannel() {
    EventLoop loop;

    bool thrown = false;
    try {
        loop.updateChannel(nullptr);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(thrown, "event loop should forward invalid channel rejection");
}

}  // namespace

std::vector<TestCase> eventLoopTests() {
    return {
        {"event loop dispatches read callback and quits", testEventLoopDispatchesReadCallbackAndQuits},
        {"event loop dispatches write callback and quits", testEventLoopDispatchesWriteCallbackAndQuits},
        {"event loop remove channel stops dispatch", testEventLoopRemoveChannelStopsDispatch},
        {"event loop quit before loop returns immediately", testEventLoopQuitBeforeLoopReturnsImmediately},
        {"event loop update rejects invalid channel", testEventLoopUpdateRejectsInvalidChannel},
    };
}
