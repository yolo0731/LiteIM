#include "liteim/net/TcpServer.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/Packet.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;

liteim::Bytes bytesFromString(const std::string& value) {
    return {value.begin(), value.end()};
}

liteim::Packet makePacket(std::string body, std::uint64_t seq_id = 1) {
    liteim::Packet packet;
    packet.header.msg_type = liteim::MessageType::PrivateMessageRequest;
    packet.header.seq_id = seq_id;
    packet.body = bytesFromString(body);
    return packet;
}

liteim::Bytes encodeOrDie(const liteim::Packet& packet) {
    liteim::Bytes encoded;
    const auto status = liteim::encodePacket(packet, encoded);
    EXPECT_TRUE(status.isOk()) << status.message();
    return encoded;
}

void writeAll(int fd, const liteim::Bytes& data) {
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

liteim::Bytes readExactWithTimeout(int fd, std::size_t len, std::chrono::milliseconds timeout) {
    liteim::Bytes output(len);
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

sockaddr_in loopbackAddress(std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    const int rc = ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    EXPECT_EQ(rc, 1);
    return address;
}

liteim::UniqueFd connectTo(std::uint16_t port) {
    liteim::UniqueFd client(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    EXPECT_GE(client.fd(), 0);
    if (client.fd() < 0) {
        return client;
    }

    const auto address = loopbackAddress(port);
    const int rc =
        ::connect(client.fd(), reinterpret_cast<const sockaddr*>(&address), static_cast<socklen_t>(sizeof(address)));
    EXPECT_EQ(rc, 0) << "connect errno=" << errno;
    return client;
}

bool waitUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return condition();
}

class RunningTcpServer {
public:
    using ConfigureCallback = std::function<void(liteim::TcpServer&, liteim::EventLoop&)>;

    RunningTcpServer(std::size_t io_thread_count, ConfigureCallback configure = ConfigureCallback())
        : thread_([this, io_thread_count, configure = std::move(configure)]() mutable {
              liteim::EventLoop loop;
              liteim::TcpServer server(&loop, "127.0.0.1", 0, io_thread_count);
              if (configure) {
                  configure(server, loop);
              }
              server.start();

              {
                  std::lock_guard<std::mutex> lock(mutex_);
                  loop_ = &loop;
                  server_ = &server;
                  port_ = server.port();
              }
              ready_.notify_one();

              loop.loop();
              server.stop();

              {
                  std::lock_guard<std::mutex> lock(mutex_);
                  loop_ = nullptr;
                  server_ = nullptr;
              }
          }) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this]() { return port_ != 0; });
    }

    RunningTcpServer(const RunningTcpServer&) = delete;
    RunningTcpServer& operator=(const RunningTcpServer&) = delete;

    ~RunningTcpServer() {
        stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::uint16_t port() const noexcept {
        return port_;
    }

    std::size_t sessionCount() const {
        auto* server = serverPtr();
        if (server == nullptr) {
            return 0;
        }
        return server->sessionCount();
    }

    liteim::Status sendToSession(std::uint64_t session_id, const liteim::Packet& packet) {
        auto* server = serverPtr();
        if (server == nullptr) {
            return liteim::Status::error(liteim::ErrorCode::InvalidArgument, "server is stopped");
        }
        return server->sendToSession(session_id, packet);
    }

    liteim::Status sendToUser(std::uint64_t user_id, const liteim::Packet& packet) {
        auto* server = serverPtr();
        if (server == nullptr) {
            return liteim::Status::error(liteim::ErrorCode::InvalidArgument, "server is stopped");
        }
        return server->sendToUser(user_id, packet);
    }

    void stop() noexcept {
        liteim::EventLoop* loop = nullptr;
        liteim::TcpServer* server = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop = loop_;
            server = server_;
        }

        if (loop != nullptr && server != nullptr) {
            loop->queueInLoop([loop, server]() {
                server->stop();
                loop->quit();
            });
        }
    }

private:
    liteim::TcpServer* serverPtr() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return server_;
    }

    mutable std::mutex mutex_;
    std::condition_variable ready_;
    liteim::EventLoop* loop_{nullptr};
    liteim::TcpServer* server_{nullptr};
    std::uint16_t port_{0};
    std::thread thread_;
};

} // namespace

TEST(TcpServerTest, EchoesPacketToClient) {
    RunningTcpServer server(1);
    auto client = connectTo(server.port());
    ASSERT_GE(client.fd(), 0);

    const auto packet = makePacket("hello tcp server", 101);
    const auto expected = encodeOrDie(packet);

    writeAll(client.fd(), expected);
    const auto received = readExactWithTimeout(client.fd(), expected.size(), 2s);

    EXPECT_EQ(received, expected);
}

TEST(TcpServerTest, DistributesConnectionsAcrossIoLoops) {
    std::mutex mutex;
    std::condition_variable callback_ready;
    std::set<liteim::EventLoop*> observed_loops;
    int callback_count = 0;

    RunningTcpServer server(2, [&](liteim::TcpServer& tcp_server, liteim::EventLoop&) {
        tcp_server.setMessageCallback([&](const liteim::Session::Ptr& session, const liteim::Packet& packet) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                observed_loops.insert(session->ownerLoop());
                ++callback_count;
            }
            callback_ready.notify_one();
            const auto status = session->sendPacket(packet);
            EXPECT_TRUE(status.isOk()) << status.message();
        });
    });

    auto first_client = connectTo(server.port());
    auto second_client = connectTo(server.port());
    ASSERT_GE(first_client.fd(), 0);
    ASSERT_GE(second_client.fd(), 0);

    const auto first_packet = makePacket("first", 201);
    const auto second_packet = makePacket("second", 202);
    const auto first_expected = encodeOrDie(first_packet);
    const auto second_expected = encodeOrDie(second_packet);

    writeAll(first_client.fd(), first_expected);
    writeAll(second_client.fd(), second_expected);

    EXPECT_EQ(readExactWithTimeout(first_client.fd(), first_expected.size(), 2s), first_expected);
    EXPECT_EQ(readExactWithTimeout(second_client.fd(), second_expected.size(), 2s), second_expected);

    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(callback_ready.wait_for(lock, 2s, [&]() { return callback_count >= 2; }));
    EXPECT_GE(observed_loops.size(), 2U);
}

TEST(TcpServerTest, RemovesSessionAfterClientDisconnects) {
    RunningTcpServer server(1);
    auto client = connectTo(server.port());
    ASSERT_GE(client.fd(), 0);

    ASSERT_TRUE(waitUntil([&]() { return server.sessionCount() == 1; }, 2s));

    client.reset();

    EXPECT_TRUE(waitUntil([&]() { return server.sessionCount() == 0; }, 2s));
}

TEST(TcpServerTest, SendToSessionFromOtherThreadDeliversPacket) {
    std::mutex mutex;
    std::condition_variable callback_ready;
    std::uint64_t observed_session_id = 0;

    RunningTcpServer server(1, [&](liteim::TcpServer& tcp_server, liteim::EventLoop&) {
        tcp_server.setMessageCallback([&](const liteim::Session::Ptr& session, const liteim::Packet&) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                observed_session_id = session->id();
            }
            callback_ready.notify_one();
        });
    });

    auto client = connectTo(server.port());
    ASSERT_GE(client.fd(), 0);

    const auto trigger = encodeOrDie(makePacket("trigger", 301));
    writeAll(client.fd(), trigger);

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(callback_ready.wait_for(lock, 2s, [&]() { return observed_session_id != 0; }));
    }

    const auto reply = makePacket("server push", 302);
    const auto expected = encodeOrDie(reply);
    const auto status = server.sendToSession(observed_session_id, reply);
    ASSERT_TRUE(status.isOk()) << status.message();

    EXPECT_EQ(readExactWithTimeout(client.fd(), expected.size(), 2s), expected);
}

TEST(TcpServerTest, SendToUnknownUserReturnsNotFound) {
    RunningTcpServer server(0);

    const auto status = server.sendToUser(42, makePacket("not bound", 401));

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::NotFound);
}

TEST(TcpServerTest, StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis) {
    EXPECT_DEATH(
        {
            liteim::EventLoop loop;
            liteim::TcpServer server(&loop, "127.0.0.1", 0, 0);
            std::thread caller([&server]() { server.stop(); });
            caller.join();
        },
        ".*");
}

TEST(TcpServerTest, IdleSessionIsClosedByHeartbeatTimeout) {
    RunningTcpServer server(1, [](liteim::TcpServer& tcp_server, liteim::EventLoop&) {
        tcp_server.setHeartbeatOptions(20ms, 60ms);
    });
    auto client = connectTo(server.port());
    ASSERT_GE(client.fd(), 0);

    ASSERT_TRUE(waitUntil([&]() { return server.sessionCount() == 1; }, 2s));

    EXPECT_TRUE(waitUntil([&]() { return server.sessionCount() == 0; }, 2s));
}

TEST(TcpServerTest, ActiveSessionSurvivesHeartbeatTimeout) {
    RunningTcpServer server(1, [](liteim::TcpServer& tcp_server, liteim::EventLoop&) {
        tcp_server.setHeartbeatOptions(20ms, 90ms);
    });
    auto client = connectTo(server.port());
    ASSERT_GE(client.fd(), 0);

    ASSERT_TRUE(waitUntil([&]() { return server.sessionCount() == 1; }, 2s));

    for (std::uint64_t seq_id = 501; seq_id < 506; ++seq_id) {
        const auto packet = makePacket("keep-active", seq_id);
        const auto expected = encodeOrDie(packet);
        writeAll(client.fd(), expected);
        EXPECT_EQ(readExactWithTimeout(client.fd(), expected.size(), 1s), expected);
        std::this_thread::sleep_for(35ms);
    }

    EXPECT_EQ(server.sessionCount(), 1U);

    client.reset();
    EXPECT_TRUE(waitUntil([&]() { return server.sessionCount() == 0; }, 2s));
}

TEST(TcpServerTest, ServerWritesDoNotRefreshHeartbeatActivity) {
    std::mutex mutex;
    std::condition_variable callback_ready;
    std::uint64_t observed_session_id = 0;

    RunningTcpServer server(1, [&](liteim::TcpServer& tcp_server, liteim::EventLoop&) {
        tcp_server.setHeartbeatOptions(20ms, 80ms);
        tcp_server.setMessageCallback([&](const liteim::Session::Ptr& session, const liteim::Packet&) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                observed_session_id = session->id();
            }
            callback_ready.notify_one();
        });
    });

    auto client = connectTo(server.port());
    ASSERT_GE(client.fd(), 0);

    const auto trigger = encodeOrDie(makePacket("trigger", 601));
    writeAll(client.fd(), trigger);

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(callback_ready.wait_for(lock, 2s, [&]() { return observed_session_id != 0; }));
    }

    std::atomic_bool keep_pushing{true};
    std::thread pusher([&]() {
        std::uint64_t seq_id = 602;
        while (keep_pushing.load()) {
            const auto status = server.sendToSession(observed_session_id, makePacket("server-push", seq_id++));
            (void)status;
            std::this_thread::sleep_for(10ms);
        }
    });

    const bool closed_by_heartbeat = waitUntil([&]() { return server.sessionCount() == 0; }, 2s);
    keep_pushing = false;
    pusher.join();

    EXPECT_TRUE(closed_by_heartbeat);
}
