#include "TestUtil.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/Epoller.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <unistd.h>

#include <stdexcept>
#include <vector>

namespace {

using liteim::net::Channel;
using liteim::net::Epoller;
using liteim::net::closeFd;
using liteim::tests::TestCase;
using liteim::tests::expect;

class PipeGuard {
public:
    PipeGuard() {
        int fds[2] = {-1, -1};
        const int rc = ::pipe(fds);
        expect(rc == 0, "pipe should be created for epoller test");
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

    void writeByte() const {
        char byte = 'x';
        const ssize_t written = ::write(write_fd_, &byte, 1);
        expect(written == 1, "pipe write should make read fd readable");
    }

private:
    int read_fd_ = -1;
    int write_fd_ = -1;
};

void testPollTimeoutReturnsEmpty() {
    Epoller epoller;
    const auto active_events = epoller.poll(0);

    expect(active_events.empty(), "poll with no registered fd should return no active events");
}

void testUpdateChannelAddsReadableFd() {
    PipeGuard pipe;
    Channel channel(nullptr, pipe.readFd());
    channel.enableReading();

    Epoller epoller;
    epoller.updateChannel(&channel);

    pipe.writeByte();
    const auto active_events = epoller.poll(100);

    expect(active_events.size() == 1, "readable pipe should produce one active event");
    expect(active_events.front().channel == &channel, "active event should point to registered channel");
    expect((active_events.front().events & EPOLLIN) != 0, "active event should include EPOLLIN");
    expect((channel.revents() & EPOLLIN) != 0, "poll should update channel returned events");
    expect((channel.events() & EPOLLET) == 0, "Step 7 should not enable edge-triggered mode");
}

void testPollUsesLevelTriggeredBehavior() {
    PipeGuard pipe;
    Channel channel(nullptr, pipe.readFd());
    channel.enableReading();

    Epoller epoller;
    epoller.updateChannel(&channel);

    pipe.writeByte();

    const auto first_events = epoller.poll(100);
    const auto second_events = epoller.poll(0);

    expect(!first_events.empty(), "first poll should observe readable fd");
    expect(!second_events.empty(), "LT mode should report unread data again");
}

void testUpdateChannelModifiesInterestMask() {
    PipeGuard pipe;
    Channel channel(nullptr, pipe.readFd());
    channel.enableReading();

    Epoller epoller;
    epoller.updateChannel(&channel);

    channel.disableAll();
    channel.enableWriting();
    epoller.updateChannel(&channel);

    pipe.writeByte();
    const auto active_events = epoller.poll(0);

    expect(active_events.empty(), "modified write-only interest should suppress readable event");
}

void testRemoveChannelStopsEvents() {
    PipeGuard pipe;
    Channel channel(nullptr, pipe.readFd());
    channel.enableReading();

    Epoller epoller;
    epoller.updateChannel(&channel);
    epoller.removeChannel(&channel);

    pipe.writeByte();
    const auto active_events = epoller.poll(0);

    expect(active_events.empty(), "removed channel should not produce active events");
    epoller.removeChannel(&channel);
}

void testUpdateChannelRejectsInvalidChannel() {
    Epoller epoller;

    bool null_thrown = false;
    try {
        epoller.updateChannel(nullptr);
    } catch (const std::invalid_argument&) {
        null_thrown = true;
    }

    Channel invalid_channel(nullptr, -1);
    bool fd_thrown = false;
    try {
        epoller.updateChannel(&invalid_channel);
    } catch (const std::invalid_argument&) {
        fd_thrown = true;
    }

    expect(null_thrown, "updateChannel should reject null channel");
    expect(fd_thrown, "updateChannel should reject invalid fd");
}

}  // namespace

std::vector<TestCase> epollerTests() {
    return {
        {"epoller poll timeout returns empty", testPollTimeoutReturnsEmpty},
        {"epoller add readable fd", testUpdateChannelAddsReadableFd},
        {"epoller poll is level triggered", testPollUsesLevelTriggeredBehavior},
        {"epoller modifies interest mask", testUpdateChannelModifiesInterestMask},
        {"epoller remove channel stops events", testRemoveChannelStopsEvents},
        {"epoller update rejects invalid channel", testUpdateChannelRejectsInvalidChannel},
    };
}
