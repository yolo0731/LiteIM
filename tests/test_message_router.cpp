#include "TestUtil.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/Session.hpp"
#include "liteim/net/SocketUtil.hpp"
#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/protocol/MessageType.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/service/MessageRouter.hpp"

#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using liteim::net::Channel;
using liteim::net::EventLoop;
using liteim::net::Session;
using liteim::net::closeFd;
using liteim::protocol::FrameDecoder;
using liteim::protocol::MsgType;
using liteim::protocol::Packet;
using liteim::protocol::toUint16;
using liteim::service::MessageRouter;
using liteim::tests::TestCase;
using liteim::tests::expect;

class FdGuard {
public:
    explicit FdGuard(int fd = -1) : fd_(fd) {}

    ~FdGuard() {
        closeFd(fd_);
    }

    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    FdGuard(FdGuard&& other) noexcept : fd_(other.release()) {}

    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            closeFd(fd_);
            fd_ = other.release();
        }
        return *this;
    }

    int get() const {
        return fd_;
    }

    int release() {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_ = -1;
};

class SocketPair {
public:
    SocketPair() {
        int fds[2] = {-1, -1};
        const int rc = ::socketpair(
            AF_UNIX,
            SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
            0,
            fds);
        expect(rc == 0, "socketpair should create connected stream fds");
        session_fd_ = FdGuard(fds[0]);
        peer_fd_ = FdGuard(fds[1]);
    }

    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;

    int peerFd() const {
        return peer_fd_.get();
    }

    int releaseSessionFd() {
        return session_fd_.release();
    }

private:
    FdGuard session_fd_;
    FdGuard peer_fd_;
};

Packet makeRequest(std::uint16_t msg_type, std::uint32_t seq_id, std::string body) {
    Packet packet;
    packet.header.msg_type = msg_type;
    packet.header.seq_id = seq_id;
    packet.body = std::move(body);
    return packet;
}

std::string readAvailable(int fd) {
    std::string data;
    char buffer[4096];

    while (true) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            data.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            return data;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return data;
        }
        if (errno == EINTR) {
            continue;
        }

        throw std::runtime_error("test readAvailable failed");
    }
}

Packet routeAndReadSingleResponse(const Packet& request) {
    SocketPair sockets;
    EventLoop loop;
    Session session(&loop, sockets.releaseSessionFd());
    MessageRouter router;
    Channel peer_channel(&loop, sockets.peerFd());
    FrameDecoder decoder;
    std::vector<Packet> responses;

    peer_channel.setReadCallback([&]() {
        const std::string bytes = readAvailable(sockets.peerFd());
        const auto packets = decoder.feed(bytes.data(), bytes.size());
        responses.insert(responses.end(), packets.begin(), packets.end());
        if (!responses.empty()) {
            loop.quit();
        }
    });
    peer_channel.enableReading();

    router.route(session, request);
    expect(session.pendingOutputBytes() > 0, "router should write through Session::sendPacket");

    loop.loop();
    loop.removeChannel(&peer_channel);

    expect(!session.closed(), "routing should not close the session");
    expect(responses.size() == 1, "router should produce one response packet");
    return responses.front();
}

void testHeartbeatRequestSendsHeartbeatResponse() {
    const Packet response = routeAndReadSingleResponse(
        makeRequest(toUint16(MsgType::HEARTBEAT_REQ), 41, "client-ping"));

    expect(
        response.header.msg_type == toUint16(MsgType::HEARTBEAT_RESP),
        "heartbeat request should produce HEARTBEAT_RESP");
    expect(response.header.seq_id == 41, "heartbeat response should preserve request seq_id");
    expect(response.body.empty(), "heartbeat response body should be empty in Step 13");
}

void testHeartbeatRequestAllowsEmptyBody() {
    const Packet response = routeAndReadSingleResponse(
        makeRequest(toUint16(MsgType::HEARTBEAT_REQ), 42, ""));

    expect(
        response.header.msg_type == toUint16(MsgType::HEARTBEAT_RESP),
        "empty heartbeat request should still produce HEARTBEAT_RESP");
    expect(response.header.seq_id == 42, "empty heartbeat response should preserve seq_id");
    expect(response.body.empty(), "empty heartbeat response should have empty body");
}

void testUnknownMessageTypeSendsErrorResponse() {
    const Packet response = routeAndReadSingleResponse(makeRequest(777, 43, "payload"));

    expect(
        response.header.msg_type == toUint16(MsgType::ERROR_RESP),
        "unknown message type should produce ERROR_RESP");
    expect(response.header.seq_id == 43, "error response should preserve request seq_id");
    expect(
        response.body == "unknown message type",
        "error response body should explain the routing failure");
}

}  // namespace

std::vector<TestCase> messageRouterTests() {
    return {
        {"MessageRouter replies to heartbeat requests", testHeartbeatRequestSendsHeartbeatResponse},
        {"MessageRouter accepts empty heartbeat bodies", testHeartbeatRequestAllowsEmptyBody},
        {"MessageRouter rejects unknown message types", testUnknownMessageTypeSendsErrorResponse},
    };
}
