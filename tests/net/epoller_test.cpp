#include "liteim/net/Epoller.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <cerrno>
#include <csignal>
#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>

#include <gtest/gtest.h>

namespace {

class PipePair {
public:
    PipePair() {
        int fds[2] = {-1, -1};
        const int rc = ::pipe(fds);
        EXPECT_EQ(rc, 0);
        read_fd_ = fds[0];
        write_fd_ = fds[1];
    }

    PipePair(const PipePair&) = delete;
    PipePair& operator=(const PipePair&) = delete;

    ~PipePair() {
        const auto read_close = liteim::closeFd(read_fd_);
        const auto write_close = liteim::closeFd(write_fd_);
        (void)read_close;
        (void)write_close;
    }

    int readFd() const noexcept {
        return read_fd_;
    }

    int writeFd() const noexcept {
        return write_fd_;
    }

private:
    int read_fd_{liteim::kInvalidFd};
    int write_fd_{liteim::kInvalidFd};
};

bool containsChannel(const liteim::Epoller::ChannelList& channels, liteim::Channel* channel) {
    return std::find(channels.begin(), channels.end(), channel) != channels.end();
}

}  // namespace

TEST(EpollerTest, AddChannelReceivesReadableEvent) {
    liteim::Epoller epoller(nullptr);
    PipePair pipe;
    liteim::Channel channel(nullptr, pipe.readFd());
    channel.enableReading();

    ASSERT_TRUE(epoller.updateChannel(&channel).isOk());
    ASSERT_EQ(::write(pipe.writeFd(), "x", 1), 1);

    liteim::Epoller::ChannelList active_channels;
    const auto status = epoller.poll(100, active_channels);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_EQ(active_channels.size(), 1U);
    EXPECT_TRUE(containsChannel(active_channels, &channel));
    EXPECT_NE(channel.revents() & liteim::Channel::kReadEvent, 0U);
}

TEST(EpollerTest, ModifyChannelToWriteInterestTakesEffect) {
    liteim::Epoller epoller(nullptr);
    PipePair pipe;
    liteim::Channel channel(nullptr, pipe.writeFd());
    channel.enableReading();
    ASSERT_TRUE(epoller.updateChannel(&channel).isOk());

    channel.disableReading();
    channel.enableWriting();
    ASSERT_TRUE(epoller.updateChannel(&channel).isOk());

    liteim::Epoller::ChannelList active_channels;
    const auto status = epoller.poll(100, active_channels);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_EQ(active_channels.size(), 1U);
    EXPECT_TRUE(containsChannel(active_channels, &channel));
    EXPECT_NE(channel.revents() & liteim::Channel::kWriteEvent, 0U);
}

TEST(EpollerTest, RemoveChannelStopsEvents) {
    liteim::Epoller epoller(nullptr);
    PipePair pipe;
    liteim::Channel channel(nullptr, pipe.readFd());
    channel.enableReading();
    ASSERT_TRUE(epoller.updateChannel(&channel).isOk());
    ASSERT_TRUE(epoller.removeChannel(&channel).isOk());

    ASSERT_EQ(::write(pipe.writeFd(), "x", 1), 1);

    liteim::Epoller::ChannelList active_channels;
    const auto status = epoller.poll(10, active_channels);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_TRUE(active_channels.empty());
}

TEST(EpollerTest, PollTimeoutReturnsEmptyActiveList) {
    liteim::Epoller epoller(nullptr);
    liteim::Epoller::ChannelList active_channels;

    const auto status = epoller.poll(1, active_channels);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_TRUE(active_channels.empty());
}

TEST(EpollerTest, InvalidChannelOperationsReturnError) {
    liteim::Epoller epoller(nullptr);
    PipePair pipe;
    liteim::Channel invalid_channel(nullptr, liteim::kInvalidFd);
    liteim::Channel unregistered_channel(nullptr, pipe.readFd());

    EXPECT_FALSE(epoller.updateChannel(nullptr).isOk());
    EXPECT_FALSE(epoller.removeChannel(nullptr).isOk());
    EXPECT_FALSE(epoller.updateChannel(&invalid_channel).isOk());
    EXPECT_FALSE(epoller.removeChannel(&invalid_channel).isOk());
    EXPECT_FALSE(epoller.updateChannel(&unregistered_channel).isOk());
    EXPECT_FALSE(epoller.removeChannel(&unregistered_channel).isOk());
}
