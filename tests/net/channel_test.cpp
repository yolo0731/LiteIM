#include "liteim/net/Channel.hpp"

#include <sys/epoll.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

TEST(ChannelTest, EnableAndDisableEventsUpdateInterestMask) {
    liteim::Channel channel(nullptr, 10);

    EXPECT_TRUE(channel.isNoneEvent());
    EXPECT_FALSE(channel.isReading());
    EXPECT_FALSE(channel.isWriting());

    channel.enableReading();
    EXPECT_FALSE(channel.isNoneEvent());
    EXPECT_TRUE(channel.isReading());
    EXPECT_FALSE(channel.isWriting());
    EXPECT_NE(channel.events() & static_cast<std::uint32_t>(EPOLLRDHUP), 0U);

    channel.enableWriting();
    EXPECT_TRUE(channel.isReading());
    EXPECT_TRUE(channel.isWriting());

    channel.disableWriting();
    EXPECT_TRUE(channel.isReading());
    EXPECT_FALSE(channel.isWriting());

    channel.disableReading();
    EXPECT_TRUE(channel.isNoneEvent());

    channel.enableWriting();
    EXPECT_FALSE(channel.isReading());
    EXPECT_TRUE(channel.isWriting());

    channel.disableAll();
    EXPECT_TRUE(channel.isNoneEvent());
}

TEST(ChannelTest, ReadableEventInvokesReadCallback) {
    liteim::Channel channel(nullptr, 10);
    int read_count = 0;
    channel.setReadCallback([&read_count]() { ++read_count; });

    channel.setRevents(EPOLLIN);
    channel.handleEvent();

    EXPECT_EQ(read_count, 1);
}

TEST(ChannelTest, WritableEventInvokesWriteCallback) {
    liteim::Channel channel(nullptr, 10);
    int write_count = 0;
    channel.setWriteCallback([&write_count]() { ++write_count; });

    channel.setRevents(EPOLLOUT);
    channel.handleEvent();

    EXPECT_EQ(write_count, 1);
}

TEST(ChannelTest, ReadWriteEventInvokesCallbacksInStableOrder) {
    liteim::Channel channel(nullptr, 10);
    std::vector<std::string> calls;
    channel.setReadCallback([&calls]() { calls.push_back("read"); });
    channel.setWriteCallback([&calls]() { calls.push_back("write"); });

    channel.setRevents(EPOLLIN | EPOLLOUT);
    channel.handleEvent();

    ASSERT_EQ(calls.size(), 2U);
    EXPECT_EQ(calls[0], "read");
    EXPECT_EQ(calls[1], "write");
}

TEST(ChannelTest, HangupWithoutReadableEventInvokesCloseOnly) {
    liteim::Channel channel(nullptr, 10);
    int close_count = 0;
    int read_count = 0;
    channel.setCloseCallback([&close_count]() { ++close_count; });
    channel.setReadCallback([&read_count]() { ++read_count; });

    channel.setRevents(EPOLLHUP);
    channel.handleEvent();

    EXPECT_EQ(close_count, 1);
    EXPECT_EQ(read_count, 0);
}

TEST(ChannelTest, HangupWithErrorWithoutReadableEventInvokesErrorThenClose) {
    liteim::Channel channel(nullptr, 10);
    std::vector<std::string> calls;
    channel.setErrorCallback([&calls]() { calls.push_back("error"); });
    channel.setCloseCallback([&calls]() { calls.push_back("close"); });

    channel.setRevents(EPOLLHUP | EPOLLERR);
    channel.handleEvent();

    ASSERT_EQ(calls.size(), 2U);
    EXPECT_EQ(calls[0], "error");
    EXPECT_EQ(calls[1], "close");
}

TEST(ChannelTest, ErrorEventInvokesErrorCallback) {
    liteim::Channel channel(nullptr, 10);
    int error_count = 0;
    channel.setErrorCallback([&error_count]() { ++error_count; });

    channel.setRevents(EPOLLERR);
    channel.handleEvent();

    EXPECT_EQ(error_count, 1);
}

TEST(ChannelTest, ErrorWithReadableEventInvokesErrorThenRead) {
    liteim::Channel channel(nullptr, 10);
    std::vector<std::string> calls;
    channel.setErrorCallback([&calls]() { calls.push_back("error"); });
    channel.setReadCallback([&calls]() { calls.push_back("read"); });

    channel.setRevents(EPOLLERR | EPOLLIN);
    channel.handleEvent();

    ASSERT_EQ(calls.size(), 2U);
    EXPECT_EQ(calls[0], "error");
    EXPECT_EQ(calls[1], "read");
}

TEST(ChannelTest, HandleEventToleratesMissingCallbacks) {
    liteim::Channel channel(nullptr, 10);

    channel.setRevents(EPOLLIN | EPOLLOUT);

    EXPECT_NO_THROW(channel.handleEvent());
}

TEST(ChannelTest, TiedExpiredOwnerSkipsCallbacks) {
    liteim::Channel channel(nullptr, 10);
    auto owner = std::make_shared<int>(42);
    int read_count = 0;

    channel.tie(owner);
    channel.setReadCallback([&read_count]() { ++read_count; });
    owner.reset();

    channel.setRevents(EPOLLIN);
    channel.handleEvent();

    EXPECT_EQ(read_count, 0);
}

TEST(ChannelTest, TiedOwnerStaysAliveDuringCallback) {
    liteim::Channel channel(nullptr, 10);
    auto owner = std::make_shared<int>(42);
    std::weak_ptr<int> weak_owner = owner;
    int read_count = 0;

    channel.tie(owner);
    channel.setReadCallback([&owner, &weak_owner, &read_count]() {
        owner.reset();
        EXPECT_FALSE(weak_owner.expired());
        ++read_count;
    });

    channel.setRevents(EPOLLIN);
    channel.handleEvent();

    EXPECT_EQ(read_count, 1);
    EXPECT_TRUE(weak_owner.expired());
}

TEST(ChannelTest, HandleEventDoesNotCopyStoredCallbacks) {
    struct CopyCountingCallback {
        int* copies;
        int* calls;

        CopyCountingCallback(int* copy_count, int* call_count)
            : copies(copy_count), calls(call_count) {}

        CopyCountingCallback(const CopyCountingCallback& other)
            : copies(other.copies), calls(other.calls) {
            ++(*copies);
        }

        CopyCountingCallback(CopyCountingCallback&& other) noexcept
            : copies(std::exchange(other.copies, nullptr)),
              calls(std::exchange(other.calls, nullptr)) {}

        CopyCountingCallback& operator=(const CopyCountingCallback&) = delete;
        CopyCountingCallback& operator=(CopyCountingCallback&&) = delete;

        void operator()() const {
            ++(*calls);
        }
    };

    liteim::Channel channel(nullptr, 10);
    int copies = 0;
    int calls = 0;
    channel.setReadCallback(CopyCountingCallback(&copies, &calls));
    copies = 0;

    channel.setRevents(EPOLLIN);
    channel.handleEvent();

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(copies, 0);
}
