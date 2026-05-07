#include "liteim/net/Session.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/Packet.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

struct SocketPair {
    liteim::UniqueFd server;
    liteim::UniqueFd peer;
};

SocketPair makeSocketPair() {
    int fds[2] = {liteim::kInvalidFd, liteim::kInvalidFd};
    const int rc = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    EXPECT_EQ(rc, 0) << "socketpair errno=" << errno;
    return SocketPair{liteim::UniqueFd(fds[0]), liteim::UniqueFd(fds[1])};
}

std::vector<std::uint8_t> bytesFromString(const std::string& value) {
    return {value.begin(), value.end()};
}

liteim::Packet makePacket(std::string body, std::uint64_t seq_id = 1) {
    liteim::Packet packet;
    packet.header.msg_type = liteim::MessageType::PrivateMessageRequest;
    packet.header.seq_id = seq_id;
    packet.body = bytesFromString(body);
    return packet;
}

std::vector<std::uint8_t> encodeOrDie(const liteim::Packet& packet) {
    std::vector<std::uint8_t> encoded;
    const auto status = liteim::encodePacket(packet, encoded);
    EXPECT_TRUE(status.isOk()) << status.message();
    return encoded;
}

void writeAll(int fd, const std::vector<std::uint8_t>& data) {
    std::size_t written = 0;
    while (written < data.size()) {
        const auto n = ::write(fd, data.data() + written, data.size() - written);
        if (n > 0) {
            written += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        FAIL() << "write failed errno=" << errno;
    }
}

std::vector<std::uint8_t> readExactWithTimeout(int fd,
                                               std::size_t len,
                                               std::chrono::milliseconds timeout) {
    std::vector<std::uint8_t> output(len);
    std::size_t read_bytes = 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (read_bytes < len) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            ADD_FAILURE() << "timed out while reading " << len << " bytes";
            output.resize(read_bytes);
            return output;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int rc = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (rc == 0) {
            continue;
        }
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            ADD_FAILURE() << "poll failed errno=" << errno;
            output.resize(read_bytes);
            return output;
        }

        const auto n = ::read(fd, output.data() + read_bytes, len - read_bytes);
        if (n > 0) {
            read_bytes += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        ADD_FAILURE() << "read failed errno=" << errno;
        output.resize(read_bytes);
        return output;
    }

    return output;
}

}  // namespace

TEST(SessionTest, CompletePacketInvokesMessageCallback) {
    auto sockets = makeSocketPair();
    liteim::EventLoop loop;
    auto session = std::make_shared<liteim::Session>(&loop, sockets.server.release());
    const auto packet = makePacket("hello", 7);
    const auto encoded = encodeOrDie(packet);
    int callback_count = 0;

    session->setMessageCallback(
        [&loop, &session, &callback_count](const std::shared_ptr<liteim::Session>& observed,
                                           const liteim::Packet& received) {
            EXPECT_EQ(observed.get(), session.get());
            EXPECT_EQ(received.header.seq_id, 7U);
            EXPECT_EQ(std::string(received.body.begin(), received.body.end()), "hello");
            ++callback_count;
            loop.quit();
        });
    session->start();

    writeAll(sockets.peer.fd(), encoded);
    loop.loop();

    EXPECT_EQ(callback_count, 1);
    session->close();
}

TEST(SessionTest, HalfPacketDoesNotInvokeMessageCallback) {
    auto sockets = makeSocketPair();
    liteim::EventLoop loop;
    auto session = std::make_shared<liteim::Session>(&loop, sockets.server.release());
    const auto encoded = encodeOrDie(makePacket("split", 8));
    std::atomic_int callback_count{0};

    session->setMessageCallback([&callback_count](const std::shared_ptr<liteim::Session>&,
                                                  const liteim::Packet&) {
        ++callback_count;
    });
    session->start();

    const std::vector<std::uint8_t> half(encoded.begin(), encoded.begin() + 5);
    writeAll(sockets.peer.fd(), half);
    std::thread quitter([&loop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        loop.queueInLoop([&loop]() { loop.quit(); });
    });
    loop.loop();
    quitter.join();

    EXPECT_EQ(callback_count.load(), 0);
    session->close();
}

TEST(SessionTest, StickyPacketsInvokeCallbackForEachPacket) {
    auto sockets = makeSocketPair();
    liteim::EventLoop loop;
    auto session = std::make_shared<liteim::Session>(&loop, sockets.server.release());
    const auto first = encodeOrDie(makePacket("one", 11));
    const auto second = encodeOrDie(makePacket("two", 12));
    std::vector<std::uint64_t> seq_ids;

    session->setMessageCallback(
        [&loop, &seq_ids](const std::shared_ptr<liteim::Session>&, const liteim::Packet& packet) {
            seq_ids.push_back(packet.header.seq_id);
            if (seq_ids.size() == 2) {
                loop.quit();
            }
        });
    session->start();

    std::vector<std::uint8_t> sticky = first;
    sticky.insert(sticky.end(), second.begin(), second.end());
    writeAll(sockets.peer.fd(), sticky);
    loop.loop();

    ASSERT_EQ(seq_ids.size(), 2U);
    EXPECT_EQ(seq_ids[0], 11U);
    EXPECT_EQ(seq_ids[1], 12U);
    session->close();
}

TEST(SessionTest, PeerCloseInvokesCloseCallback) {
    auto sockets = makeSocketPair();
    liteim::EventLoop loop;
    auto session = std::make_shared<liteim::Session>(&loop, sockets.server.release());
    int close_count = 0;

    session->setCloseCallback([&loop, &close_count](const std::shared_ptr<liteim::Session>& closed) {
        EXPECT_TRUE(closed->closed());
        ++close_count;
        loop.quit();
    });
    session->start();

    sockets.peer.reset();
    loop.loop();

    EXPECT_EQ(close_count, 1);
    EXPECT_TRUE(session->closed());
}

TEST(SessionTest, SendPacketFromOtherThreadDeliversEncodedPacket) {
    auto sockets = makeSocketPair();
    const auto packet = makePacket("from-other-thread", 21);
    const auto expected = encodeOrDie(packet);

    liteim::EventLoop* loop_ptr = nullptr;
    std::shared_ptr<liteim::Session> session;
    std::mutex mutex;
    std::condition_variable ready;

    std::thread loop_thread([&]() {
        liteim::EventLoop loop;
        auto local_session = std::make_shared<liteim::Session>(&loop, sockets.server.release());
        local_session->start();
        {
            std::lock_guard<std::mutex> lock(mutex);
            loop_ptr = &loop;
            session = local_session;
        }
        ready.notify_one();
        loop.loop();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&]() { return session != nullptr; });
    }

    const auto status = session->sendPacket(packet);
    ASSERT_TRUE(status.isOk()) << status.message();
    const auto received = readExactWithTimeout(sockets.peer.fd(), expected.size(), std::chrono::seconds(2));
    EXPECT_EQ(received, expected);

    loop_ptr->queueInLoop([&]() {
        session->close();
        loop_ptr->quit();
    });
    loop_thread.join();
}

TEST(SessionTest, LargePacketLeavesPendingOutputWhenPeerDoesNotRead) {
    auto sockets = makeSocketPair();
    int send_buffer_size = 4096;
    const int rc = ::setsockopt(sockets.server.fd(),
                                SOL_SOCKET,
                                SO_SNDBUF,
                                &send_buffer_size,
                                static_cast<socklen_t>(sizeof(send_buffer_size)));
    ASSERT_EQ(rc, 0) << "setsockopt errno=" << errno;

    liteim::EventLoop loop;
    auto session = std::make_shared<liteim::Session>(&loop, sockets.server.release());
    liteim::Packet packet;
    packet.header.msg_type = liteim::MessageType::PrivateMessageRequest;
    packet.header.seq_id = 99;
    packet.body.assign(liteim::kMaxPacketBodyLength, static_cast<std::uint8_t>('x'));
    const auto encoded = encodeOrDie(packet);
    session->start();

    const auto status = session->sendPacket(packet);
    ASSERT_TRUE(status.isOk()) << status.message();

    std::thread quitter([&loop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        loop.queueInLoop([&loop]() { loop.quit(); });
    });
    loop.loop();
    quitter.join();

    EXPECT_GT(session->pendingOutputBytes(), 0U);
    EXPECT_LT(session->pendingOutputBytes(), encoded.size());
    session->close();
}

TEST(SessionTest, LastActiveTimeIsInitialized) {
    auto sockets = makeSocketPair();
    liteim::EventLoop loop;
    auto session = std::make_shared<liteim::Session>(&loop, sockets.server.release());

    EXPECT_GT(session->lastActiveTimeMilliseconds(), 0);
    session->close();
}
