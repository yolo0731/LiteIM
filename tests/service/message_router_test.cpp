#include "liteim/service/MessageRouter.hpp"

#include "liteim/concurrency/ThreadPool.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
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

liteim::Packet makeRequest(liteim::MessageType type, std::uint64_t seq_id,
                           liteim::Bytes body = {}) {
    liteim::Packet packet;
    packet.header.msg_type = type;
    packet.header.seq_id = seq_id;
    packet.body = std::move(body);
    return packet;
}

std::uint64_t errorCodeField(const liteim::Packet& packet) {
    liteim::TlvMap fields;
    const auto parse_status = liteim::parseTlvMap(packet.body, fields);
    EXPECT_TRUE(parse_status.isOk()) << parse_status.message();

    std::uint64_t error_code = 0;
    const auto get_status = liteim::getUint64(fields, liteim::TlvType::ErrorCode, error_code);
    EXPECT_TRUE(get_status.isOk()) << get_status.message();
    return error_code;
}

std::string errorMessageField(const liteim::Packet& packet) {
    liteim::TlvMap fields;
    const auto parse_status = liteim::parseTlvMap(packet.body, fields);
    EXPECT_TRUE(parse_status.isOk()) << parse_status.message();

    std::string message;
    const auto get_status = liteim::getString(fields, liteim::TlvType::ErrorMessage, message);
    EXPECT_TRUE(get_status.isOk()) << get_status.message();
    return message;
}

class RunningSession {
public:
    RunningSession() {
        auto sockets = makeSocketPair();
        peer_ = std::move(sockets.peer);
        thread_ = std::thread([this, server = std::move(sockets.server)]() mutable {
            liteim::EventLoop loop;
            auto local_session =
                std::make_shared<liteim::Session>(&loop, std::move(server), 9001);
            local_session->start();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = &loop;
                session_ = local_session;
                loop_thread_id_ = std::this_thread::get_id();
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

    std::thread::id loopThreadId() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return loop_thread_id_;
    }

    void closeSession() {
        auto session = this->session();
        if (session != nullptr) {
            session->close();
        }
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
    std::thread::id loop_thread_id_;
    std::thread thread_;
};

}  // namespace

TEST(MessageRouterTest, HeaderIsSelfContained) {
    using Router = liteim::MessageRouter;
    using Request = Router::RouterRequest;
    using Handler = std::function<liteim::Status(const Request&, liteim::Packet&)>;

    static_assert(std::is_constructible_v<Router, liteim::ThreadPool&>);
    static_assert(std::is_same_v<Router::Handler, Handler>);
    static_assert(std::is_enum_v<Router::DispatchMode>);
    static_assert(std::is_same_v<decltype(&Router::registerHandler),
                                 liteim::Status (Router::*)(
                                     liteim::MessageType, Handler, Router::DispatchMode)>);
    static_assert(std::is_same_v<decltype(&Router::route),
                                 void (Router::*)(liteim::Session::Ptr, liteim::Packet)>);

    liteim::ThreadPool pool(1);
    Router router(pool);
    SUCCEED();
}

TEST(MessageRouterTest, HeartbeatRequestUsesInlineHandlerWithoutStartingBusinessPool) {
    liteim::ThreadPool pool(1);
    liteim::MessageRouter router(pool);
    RunningSession running;

    router.route(running.session(), makeRequest(liteim::MessageType::HeartbeatRequest, 42));

    const auto response = readPacket(running.peerFd(), 2s);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::HeartbeatResponse);
    EXPECT_EQ(response->header.seq_id, 42U);
    EXPECT_TRUE(response->body.empty());
    EXPECT_FALSE(pool.started());
}

TEST(MessageRouterTest, BusinessThreadHandlerRunsOnWorkerAndSendsResponse) {
    liteim::ThreadPool pool(1);
    ASSERT_TRUE(pool.start().isOk());
    liteim::MessageRouter router(pool);
    RunningSession running;

    std::mutex mutex;
    std::condition_variable handler_ready;
    bool handler_called = false;
    std::thread::id handler_thread_id;

    ASSERT_TRUE(router
                    .registerHandler(
                        liteim::MessageType::LoginRequest,
                        [&](const liteim::MessageRouter::RouterRequest& request,
                            liteim::Packet& response) {
                            std::string username;
                            const auto username_status = liteim::getString(
                                request.fields, liteim::TlvType::Username, username);
                            if (!username_status.isOk()) {
                                return username_status;
                            }

                            {
                                std::lock_guard<std::mutex> lock(mutex);
                                handler_called = true;
                                handler_thread_id = std::this_thread::get_id();
                            }
                            handler_ready.notify_one();

                            response.header.msg_type = liteim::MessageType::LoginResponse;
                            return liteim::appendString(liteim::TlvType::Username, username,
                                                        response.body);
                        },
                        liteim::MessageRouter::DispatchMode::BusinessThread)
                    .isOk());

    liteim::Bytes body;
    ASSERT_TRUE(liteim::appendString(liteim::TlvType::Username, "alice", body).isOk());
    router.route(running.session(), makeRequest(liteim::MessageType::LoginRequest, 77, body));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(handler_ready.wait_for(lock, 2s, [&]() { return handler_called; }));
    }

    const auto response = readPacket(running.peerFd(), 2s);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::LoginResponse);
    EXPECT_EQ(response->header.seq_id, 77U);
    EXPECT_NE(handler_thread_id, running.loopThreadId());

    liteim::TlvMap response_fields;
    ASSERT_TRUE(liteim::parseTlvMap(response->body, response_fields).isOk());
    std::string username;
    ASSERT_TRUE(liteim::getString(response_fields, liteim::TlvType::Username, username).isOk());
    EXPECT_EQ(username, "alice");

    pool.stop();
}

TEST(MessageRouterTest, UnknownMessageTypeReturnsErrorResponse) {
    liteim::ThreadPool pool(1);
    liteim::MessageRouter router(pool);
    RunningSession running;

    router.route(running.session(), makeRequest(static_cast<liteim::MessageType>(777), 88));

    const auto response = readPacket(running.peerFd(), 2s);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::ErrorResponse);
    EXPECT_EQ(response->header.seq_id, 88U);
    EXPECT_EQ(errorCodeField(*response),
              static_cast<std::uint64_t>(liteim::ErrorCode::InvalidArgument));
    EXPECT_FALSE(errorMessageField(*response).empty());
}

TEST(MessageRouterTest, MalformedTlvReturnsErrorResponse) {
    liteim::ThreadPool pool(1);
    liteim::MessageRouter router(pool);
    RunningSession running;
    const liteim::Bytes malformed(liteim::kTlvHeaderSize - 1, 0);

    router.route(running.session(),
                 makeRequest(liteim::MessageType::HeartbeatRequest, 89, malformed));

    const auto response = readPacket(running.peerFd(), 2s);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::ErrorResponse);
    EXPECT_EQ(response->header.seq_id, 89U);
    EXPECT_EQ(errorCodeField(*response),
              static_cast<std::uint64_t>(liteim::ErrorCode::ParseError));
}

TEST(MessageRouterTest, HandlerErrorReturnsErrorResponse) {
    liteim::ThreadPool pool(1);
    liteim::MessageRouter router(pool);
    RunningSession running;

    ASSERT_TRUE(router
                    .registerHandler(
                        liteim::MessageType::LoginRequest,
                        [](const liteim::MessageRouter::RouterRequest&, liteim::Packet&) {
                            return liteim::Status::error(liteim::ErrorCode::InvalidArgument,
                                                         "bad login request");
                        },
                        liteim::MessageRouter::DispatchMode::Inline)
                    .isOk());

    router.route(running.session(), makeRequest(liteim::MessageType::LoginRequest, 90));

    const auto response = readPacket(running.peerFd(), 2s);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::ErrorResponse);
    EXPECT_EQ(response->header.seq_id, 90U);
    EXPECT_EQ(errorCodeField(*response),
              static_cast<std::uint64_t>(liteim::ErrorCode::InvalidArgument));
    EXPECT_EQ(errorMessageField(*response), "bad login request");
}

TEST(MessageRouterTest, HandlerResponseSeqIdIsOverriddenWithRequestSeqId) {
    liteim::ThreadPool pool(1);
    liteim::MessageRouter router(pool);
    RunningSession running;

    ASSERT_TRUE(router
                    .registerHandler(
                        liteim::MessageType::LoginRequest,
                        [](const liteim::MessageRouter::RouterRequest&, liteim::Packet& response) {
                            response.header.msg_type = liteim::MessageType::LoginResponse;
                            response.header.seq_id = 9999;
                            return liteim::Status::ok();
                        },
                        liteim::MessageRouter::DispatchMode::Inline)
                    .isOk());

    router.route(running.session(), makeRequest(liteim::MessageType::LoginRequest, 91));

    const auto response = readPacket(running.peerFd(), 2s);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::LoginResponse);
    EXPECT_EQ(response->header.seq_id, 91U);
}

TEST(MessageRouterTest, ClosedSessionBeforeAsyncCompletionDoesNotSendResponse) {
    liteim::ThreadPool pool(1);
    ASSERT_TRUE(pool.start().isOk());
    liteim::MessageRouter router(pool);
    RunningSession running;

    std::mutex mutex;
    std::condition_variable handler_entered;
    std::condition_variable release_handler;
    bool entered = false;
    bool release = false;

    ASSERT_TRUE(router
                    .registerHandler(
                        liteim::MessageType::LoginRequest,
                        [&](const liteim::MessageRouter::RouterRequest&, liteim::Packet& response) {
                            {
                                std::lock_guard<std::mutex> lock(mutex);
                                entered = true;
                            }
                            handler_entered.notify_one();

                            std::unique_lock<std::mutex> lock(mutex);
                            release_handler.wait(lock, [&]() { return release; });

                            response.header.msg_type = liteim::MessageType::LoginResponse;
                            return liteim::Status::ok();
                        },
                        liteim::MessageRouter::DispatchMode::BusinessThread)
                    .isOk());

    router.route(running.session(), makeRequest(liteim::MessageType::LoginRequest, 92));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(handler_entered.wait_for(lock, 2s, [&]() { return entered; }));
    }

    running.closeSession();
    ASSERT_TRUE(waitUntil([&]() { return running.session()->closed(); }, 2s));

    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    release_handler.notify_one();

    EXPECT_FALSE(readPacket(running.peerFd(), 100ms).has_value());
    pool.stop();
}
