#include "TestUtil.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/Session.hpp"
#include "liteim/net/SocketUtil.hpp"
#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/protocol/Packet.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using liteim::net::Channel;
using liteim::net::EventLoop;
using liteim::net::Session;
using liteim::net::closeFd;
using liteim::protocol::FrameDecoder;
using liteim::protocol::Packet;
using liteim::protocol::encodePacket;
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

    void closeNow() {
        closeFd(fd_);
        fd_ = -1;
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

    void closePeer() {
        peer_fd_.closeNow();
    }

private:
    FdGuard session_fd_;
    FdGuard peer_fd_;
};

Packet makePacket(std::uint16_t msg_type, std::uint32_t seq_id, std::string body) {
    Packet packet;
    packet.header.msg_type = msg_type;
    packet.header.seq_id = seq_id;
    packet.body = std::move(body);
    return packet;
}

void writeAll(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = ::send(
            fd,
            data.data() + offset,
            data.size() - offset,
            MSG_NOSIGNAL);
        if (n > 0) {
            offset += static_cast<std::size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        throw std::runtime_error("test writeAll failed");
    }
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

void testSessionReadsPacketAndInvokesMessageCallback() {
    SocketPair sockets;
    EventLoop loop;
    Session session(&loop, sockets.releaseSessionFd());

    bool called = false;
    session.setMessageCallback([&](Session&, const Packet& packet) {
        called = true;
        expect(packet.header.msg_type == 100, "session should decode packet msg_type");
        expect(packet.header.seq_id == 7, "session should decode packet seq_id");
        expect(packet.body == "hello", "session should decode packet body");
        loop.quit();
    });
    session.start();

    writeAll(sockets.peerFd(), encodePacket(makePacket(100, 7, "hello")));
    loop.loop();

    expect(called, "session should call message callback for a complete packet");
    expect(session.started(), "session should report started after start()");
    expect(!session.closed(), "session should stay open after normal read");
}

void testSessionDecodesStickyPackets() {
    SocketPair sockets;
    EventLoop loop;
    Session session(&loop, sockets.releaseSessionFd());

    int message_count = 0;
    session.setMessageCallback([&](Session&, const Packet& packet) {
        ++message_count;
        if (message_count == 1) {
            expect(packet.body == "one", "first sticky packet body should match");
        }
        if (message_count == 2) {
            expect(packet.body == "two", "second sticky packet body should match");
            loop.quit();
        }
    });
    session.start();

    const std::string first = encodePacket(makePacket(101, 1, "one"));
    const std::string second = encodePacket(makePacket(102, 2, "two"));
    writeAll(sockets.peerFd(), first + second);

    loop.loop();

    expect(message_count == 2, "session should decode two sticky packets");
}

void testSessionDecodesLargeFrameAcrossMultipleReads() {
    SocketPair sockets;
    EventLoop loop;
    Session session(&loop, sockets.releaseSessionFd());

    std::string large_body(8000, 'x');
    bool called = false;
    session.setMessageCallback([&](Session&, const Packet& packet) {
        called = true;
        expect(packet.body == large_body, "large packet body should survive multiple reads");
        loop.quit();
    });
    session.start();

    writeAll(sockets.peerFd(), encodePacket(makePacket(103, 3, large_body)));
    loop.loop();

    expect(called, "session should decode a frame larger than one read buffer");
}

void testSessionSendPacketWritesToPeer() {
    SocketPair sockets;
    EventLoop loop;
    Session session(&loop, sockets.releaseSessionFd());
    Channel peer_channel(&loop, sockets.peerFd());

    FrameDecoder decoder;
    bool received = false;
    peer_channel.setReadCallback([&]() {
        const std::string bytes = readAvailable(sockets.peerFd());
        const auto packets = decoder.feed(bytes.data(), bytes.size());
        if (!packets.empty()) {
            received = true;
            expect(packets.front().header.msg_type == 200, "peer should decode sent msg_type");
            expect(packets.front().header.seq_id == 9, "peer should decode sent seq_id");
            expect(packets.front().body == "response", "peer should decode sent body");
            loop.quit();
        }
    });
    peer_channel.enableReading();

    session.sendPacket(makePacket(200, 9, "response"));
    expect(session.pendingOutputBytes() > 0, "sendPacket should append data to output buffer");

    loop.loop();
    loop.removeChannel(&peer_channel);

    expect(received, "peer should receive packet written by session");
    expect(session.pendingOutputBytes() == 0, "output buffer should be empty after write");
}

void testSessionCloseOnPeerEof() {
    SocketPair sockets;
    EventLoop loop;
    Session session(&loop, sockets.releaseSessionFd());

    bool close_called = false;
    int closed_fd = -1;
    session.setCloseCallback([&](int fd) {
        close_called = true;
        closed_fd = fd;
        loop.quit();
    });
    session.start();

    sockets.closePeer();
    loop.loop();

    expect(close_called, "session should call close callback when peer closes");
    expect(closed_fd >= 0, "close callback should receive the closed fd value");
    expect(session.closed(), "session should be closed after peer EOF");
    expect(session.fd() == -1, "session fd should be invalid after close");
}

void testSessionClosesOnInvalidFrame() {
    SocketPair sockets;
    EventLoop loop;
    Session session(&loop, sockets.releaseSessionFd());

    bool close_called = false;
    session.setCloseCallback([&](int) {
        close_called = true;
        loop.quit();
    });
    session.start();

    writeAll(sockets.peerFd(), std::string(16, '\0'));
    loop.loop();

    expect(close_called, "session should close when frame decoder reports an error");
    expect(session.closed(), "session should be closed after invalid frame");
}

}  // namespace

std::vector<TestCase> sessionTests() {
    return {
        {"session reads packet and invokes message callback", testSessionReadsPacketAndInvokesMessageCallback},
        {"session decodes sticky packets", testSessionDecodesStickyPackets},
        {"session decodes large frame across multiple reads", testSessionDecodesLargeFrameAcrossMultipleReads},
        {"session sendPacket writes to peer", testSessionSendPacketWritesToPeer},
        {"session close on peer eof", testSessionCloseOnPeerEof},
        {"session closes on invalid frame", testSessionClosesOnInvalidFrame},
    };
}
