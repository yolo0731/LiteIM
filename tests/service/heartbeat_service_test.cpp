#include "liteim/service/HeartbeatService.hpp"

#include "liteim/cache/ICache.hpp"
#include "liteim/concurrency/ThreadPool.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/service/SessionManager.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;

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

std::shared_ptr<liteim::Session> makeSession(liteim::EventLoop& loop, std::uint64_t session_id) {
    auto sockets = makeSocketPair();
    return std::make_shared<liteim::Session>(&loop, std::move(sockets.server), session_id);
}

liteim::Packet makePacket(liteim::MessageType type, std::uint64_t seq_id) {
    liteim::Packet packet;
    packet.header.msg_type = type;
    packet.header.seq_id = seq_id;
    return packet;
}

liteim::Bytes readExactOrEmpty(int fd, std::size_t len, std::chrono::milliseconds timeout) {
    liteim::Bytes output(len);
    std::size_t read_bytes = 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (read_bytes < len) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            output.resize(read_bytes);
            return output;
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
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
        output.resize(read_bytes);
        return output;
    }

    return output;
}

std::optional<liteim::Packet> readPacket(int fd, std::chrono::milliseconds timeout) {
    const auto header_bytes = readExactOrEmpty(fd, liteim::kPacketHeaderSize, timeout);
    if (header_bytes.size() != liteim::kPacketHeaderSize) {
        return std::nullopt;
    }

    liteim::PacketHeader header;
    const auto header_status = liteim::parseHeader(header_bytes.data(), header_bytes.size(), header);
    EXPECT_TRUE(header_status.isOk()) << header_status.message();
    if (!header_status.isOk()) {
        return std::nullopt;
    }

    liteim::Packet packet;
    packet.header = header;
    packet.body = readExactOrEmpty(fd, header.body_len, timeout);
    if (packet.body.size() != header.body_len) {
        return std::nullopt;
    }
    return packet;
}

class RunningSession {
public:
    explicit RunningSession(std::uint64_t session_id) {
        auto sockets = makeSocketPair();
        peer_ = std::move(sockets.peer);
        thread_ = std::thread([this, session_id, server = std::move(sockets.server)]() mutable {
            liteim::EventLoop loop;
            auto local_session =
                std::make_shared<liteim::Session>(&loop, std::move(server), session_id);
            local_session->start();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = &loop;
                session_ = local_session;
            }
            ready_.notify_one();

            loop.loop();
            local_session->close();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = nullptr;
            }
        });

        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this]() { return session_ != nullptr; });
    }

    RunningSession(const RunningSession&) = delete;
    RunningSession& operator=(const RunningSession&) = delete;

    ~RunningSession() {
        stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    liteim::Session::Ptr session() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return session_;
    }

    int peerFd() const noexcept {
        return peer_.fd();
    }

    void stop() noexcept {
        liteim::EventLoop* loop = nullptr;
        liteim::Session::Ptr session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop = loop_;
            session = session_;
        }

        if (loop != nullptr) {
            loop->queueInLoop([loop, session]() {
                if (session != nullptr) {
                    session->close();
                }
                loop->quit();
            });
        }
    }

private:
    liteim::UniqueFd peer_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    liteim::EventLoop* loop_{nullptr};
    liteim::Session::Ptr session_;
    std::thread thread_;
};

class FakeCache final : public liteim::ICache {
public:
    liteim::Status setUserOnline(const liteim::OnlineSession& session,
                                 std::chrono::seconds ttl) override {
        std::lock_guard<std::mutex> lock(mutex);
        ++set_online_calls;
        last_set_ttl = ttl;
        online_users[session.user_id] = session;
        return liteim::Status::ok();
    }

    liteim::Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl) override {
        std::lock_guard<std::mutex> lock(mutex);
        ++refresh_online_calls;
        last_refresh_user_id = user_id;
        last_refresh_ttl = ttl;
        if (fail_refresh.load()) {
            return liteim::Status::error(liteim::ErrorCode::IoError, "redis refresh failed");
        }
        if (online_users.find(user_id) == online_users.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user is offline");
        }
        return liteim::Status::ok();
    }

    liteim::Status setUserOffline(std::uint64_t user_id) override {
        std::lock_guard<std::mutex> lock(mutex);
        online_users.erase(user_id);
        return liteim::Status::ok();
    }

    liteim::Status isUserOnline(std::uint64_t user_id, bool& online) override {
        std::lock_guard<std::mutex> lock(mutex);
        online = online_users.find(user_id) != online_users.end();
        return liteim::Status::ok();
    }

    liteim::Status getOnlineSession(std::uint64_t user_id,
                                    liteim::OnlineSession& session) override {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = online_users.find(user_id);
        if (it == online_users.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user is offline");
        }
        session = it->second;
        return liteim::Status::ok();
    }

    liteim::Status incrUnread(const liteim::UnreadKey&, std::uint64_t delta,
                              std::uint64_t& unread_count) override {
        unread_count += delta;
        return liteim::Status::ok();
    }

    liteim::Status getUnread(const liteim::UnreadKey&, std::uint64_t& unread_count) override {
        unread_count = 0;
        return liteim::Status::ok();
    }

    liteim::Status clearUnread(const liteim::UnreadKey&) override {
        return liteim::Status::ok();
    }

    liteim::Status allowLoginAttempt(const liteim::LoginAttemptKey&, std::uint32_t,
                                     bool& allowed) override {
        allowed = true;
        return liteim::Status::ok();
    }

    liteim::Status recordLoginFailure(const liteim::LoginAttemptKey&,
                                      std::chrono::seconds) override {
        return liteim::Status::ok();
    }

    liteim::Status clearLoginFailure(const liteim::LoginAttemptKey&) override {
        return liteim::Status::ok();
    }

    int refreshCalls() const {
        std::lock_guard<std::mutex> lock(mutex);
        return refresh_online_calls;
    }

    std::uint64_t lastRefreshUserId() const {
        std::lock_guard<std::mutex> lock(mutex);
        return last_refresh_user_id;
    }

    std::chrono::seconds lastRefreshTtl() const {
        std::lock_guard<std::mutex> lock(mutex);
        return last_refresh_ttl;
    }

    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, liteim::OnlineSession> online_users;
    std::chrono::seconds last_set_ttl{0};
    std::chrono::seconds last_refresh_ttl{0};
    std::uint64_t last_refresh_user_id{0};
    int set_online_calls{0};
    int refresh_online_calls{0};
    std::atomic_bool fail_refresh{false};
};

class HeartbeatServiceFixture : public ::testing::Test {
protected:
    HeartbeatServiceFixture() : online(sessions, cache, "server-a", 30s), service(online) {}

    liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                    std::uint64_t seq_id = 40) {
        return liteim::MessageRouter::RouterRequest{
            session, makePacket(liteim::MessageType::HeartbeatRequest, seq_id), liteim::TlvMap{}};
    }

    liteim::Session::Ptr makeBoundSession(std::uint64_t user_id = 1001,
                                          std::uint64_t session_id = 7001) {
        auto session = makeSession(loop, session_id);
        EXPECT_TRUE(online.bindUser(user_id, session).isOk());
        return session;
    }

    liteim::EventLoop loop;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService online;
    liteim::HeartbeatService service;
};

}  // namespace

TEST(HeartbeatServiceTest, HeaderIsSelfContained) {
    using Service = liteim::HeartbeatService;

    static_assert(std::is_constructible_v<Service, liteim::OnlineService&>);
    static_assert(
        std::is_same_v<decltype(&Service::registerHandlers),
                       liteim::Status (Service::*)(liteim::MessageRouter&)>);
    static_assert(std::is_same_v<decltype(&Service::handleHeartbeat),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
}

TEST_F(HeartbeatServiceFixture, UnauthenticatedHeartbeatReturnsResponseWithoutRedisRefresh) {
    auto session = makeSession(loop, 7001);
    auto request = requestFor(session);
    liteim::Packet response;

    const auto status = service.handleHeartbeat(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::HeartbeatResponse);
    EXPECT_EQ(response.header.seq_id, 40U);
    EXPECT_EQ(cache.refreshCalls(), 0);
}

TEST_F(HeartbeatServiceFixture, LoggedInHeartbeatRefreshesRedisOnlineTtl) {
    auto session = makeBoundSession(1001, 7001);
    auto request = requestFor(session, 4001);
    liteim::Packet response;

    const auto status = service.handleHeartbeat(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::HeartbeatResponse);
    EXPECT_EQ(response.header.seq_id, 4001U);
    EXPECT_EQ(cache.refreshCalls(), 1);
    EXPECT_EQ(cache.lastRefreshUserId(), 1001U);
    EXPECT_EQ(cache.lastRefreshTtl(), 30s);
}

TEST_F(HeartbeatServiceFixture, LoggedInHeartbeatDoesNotMutateSessionLastActiveTime) {
    auto session = makeBoundSession(1001, 7001);
    const auto before = session->lastActiveTimeMilliseconds();
    auto request = requestFor(session);
    liteim::Packet response;

    const auto status = service.handleHeartbeat(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(session->lastActiveTimeMilliseconds(), before);
}

TEST_F(HeartbeatServiceFixture, RedisRefreshFailureReturnsErrorForLoggedInHeartbeat) {
    auto session = makeBoundSession(1001, 7001);
    cache.fail_refresh = true;
    auto request = requestFor(session);
    liteim::Packet response;

    const auto status = service.handleHeartbeat(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::IoError);
    EXPECT_EQ(cache.refreshCalls(), 1);
}

TEST_F(HeartbeatServiceFixture, RegisteredHandlerSendsHeartbeatResponseThroughRouter) {
    RunningSession running(7001);
    ASSERT_TRUE(online.bindUser(1001, running.session()).isOk());

    liteim::ThreadPool pool(1);
    ASSERT_TRUE(pool.start().isOk());
    liteim::MessageRouter router(pool);
    ASSERT_TRUE(service.registerHandlers(router).isOk());

    router.route(running.session(), makePacket(liteim::MessageType::HeartbeatRequest, 4002));

    const auto response = readPacket(running.peerFd(), 2s);
    pool.stop();

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::HeartbeatResponse);
    EXPECT_EQ(response->header.seq_id, 4002U);
    EXPECT_EQ(cache.refreshCalls(), 1);
}

TEST_F(HeartbeatServiceFixture, RegisteredHandlerReturnsErrorResponseWhenRedisRefreshFails) {
    RunningSession running(7001);
    ASSERT_TRUE(online.bindUser(1001, running.session()).isOk());
    cache.fail_refresh = true;

    liteim::ThreadPool pool(1);
    ASSERT_TRUE(pool.start().isOk());
    liteim::MessageRouter router(pool);
    ASSERT_TRUE(service.registerHandlers(router).isOk());

    router.route(running.session(), makePacket(liteim::MessageType::HeartbeatRequest, 4003));

    const auto response = readPacket(running.peerFd(), 2s);
    pool.stop();

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::ErrorResponse);
    EXPECT_EQ(response->header.seq_id, 4003U);
}
