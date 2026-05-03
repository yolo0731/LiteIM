#include "TestUtil.hpp"

#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/Session.hpp"
#include "liteim/net/SocketUtil.hpp"
#include "liteim/net/TcpServer.hpp"
#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/protocol/Packet.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using liteim::net::Channel;
using liteim::net::EventLoop;
using liteim::net::Session;
using liteim::net::TcpServer;
using liteim::net::closeFd;
using liteim::net::setNonBlocking;
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

private:
    int fd_ = -1;
};

Packet makePacket(std::uint16_t msg_type, std::uint32_t seq_id, std::string body) {
    Packet packet;
    packet.header.msg_type = msg_type;
    packet.header.seq_id = seq_id;
    packet.body = std::move(body);
    return packet;
}

FdGuard connectClient(std::uint16_t port) {
    FdGuard client_fd(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    expect(client_fd.get() >= 0, "client socket should be created");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    const int pton_rc = ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    expect(pton_rc == 1, "localhost address should be parseable");

    const int rc = ::connect(
        client_fd.get(),
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address));
    if (rc < 0) {
        throw std::runtime_error("client connect should succeed");
    }

    expect(setNonBlocking(client_fd.get()), "client fd should become nonblocking");
    return client_fd;
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
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
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

void testTcpServerRejectsNullLoop() {
    bool thrown = false;
    try {
        TcpServer server(nullptr, "127.0.0.1", 0);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(thrown, "tcp server should reject null EventLoop");
}

void testTcpServerAcceptsClientAndDispatchesMessage() {
    EventLoop loop;
    TcpServer server(&loop, "127.0.0.1", 0);

    bool message_called = false;
    int observed_session_fd = -1;
    server.setMessageCallback([&](Session& session, const Packet& packet) {
        message_called = true;
        observed_session_fd = session.fd();
        expect(server.sessionCount() == 1, "server should track accepted session before callback");
        expect(packet.header.msg_type == 100, "server should dispatch decoded msg_type");
        expect(packet.header.seq_id == 1, "server should dispatch decoded seq_id");
        expect(packet.body == "hello", "server should dispatch decoded body");
        loop.quit();
    });

    server.start();
    expect(server.started(), "server should report started after start()");
    expect(server.listening(), "server should listen after start()");
    expect(server.port() != 0, "port 0 bind should expose actual port");

    auto client = connectClient(server.port());
    writeAll(client.get(), encodePacket(makePacket(100, 1, "hello")));

    loop.loop();

    expect(message_called, "server should invoke message callback");
    expect(observed_session_fd >= 0, "callback should observe a valid session fd");
    expect(server.sessionCount() == 1, "session should remain active after message dispatch");

    server.stop();
    expect(server.stopped(), "server should report stopped after stop()");
    expect(!server.listening(), "server should stop listening after stop()");
    expect(server.sessionCount() == 0, "stop should close all sessions");
}

void testTcpServerSendsToSessionAndUser() {
    EventLoop loop;
    TcpServer server(&loop, "127.0.0.1", 0);
    FrameDecoder decoder;
    std::vector<Packet> received_packets;

    server.setMessageCallback([&](Session& session, const Packet& packet) {
        expect(packet.body == "request", "server should receive request before responding");
        expect(server.sendToUser(42, makePacket(299, 99, "missing")) == false,
               "sendToUser should fail before user binding exists");
        expect(server.sendToSession(session.fd(), makePacket(201, 2, "by-fd")),
               "sendToSession should queue response for active session");
        expect(server.bindUserToSession(42, session.fd()),
               "bindUserToSession should bind known session");
        expect(server.sendToUser(42, makePacket(202, 3, "by-user")),
               "sendToUser should queue response for bound user");
    });

    server.start();
    auto client = connectClient(server.port());

    Channel client_channel(&loop, client.get());
    client_channel.setReadCallback([&]() {
        const std::string bytes = readAvailable(client.get());
        const auto packets = decoder.feed(bytes.data(), bytes.size());
        received_packets.insert(received_packets.end(), packets.begin(), packets.end());
        if (received_packets.size() == 2) {
            loop.quit();
        }
    });
    client_channel.enableReading();

    writeAll(client.get(), encodePacket(makePacket(101, 1, "request")));
    loop.loop();
    loop.removeChannel(&client_channel);

    expect(received_packets.size() == 2, "client should receive two server responses");
    expect(received_packets[0].header.msg_type == 201, "first response should come from sendToSession");
    expect(received_packets[0].body == "by-fd", "sendToSession body should match");
    expect(received_packets[1].header.msg_type == 202, "second response should come from sendToUser");
    expect(received_packets[1].body == "by-user", "sendToUser body should match");

    server.stop();
}

void testTcpServerStopsOnSignalFdEvent() {
    EventLoop loop;
    TcpServer server(&loop, "127.0.0.1", 0);

    server.start();
    expect(server.listening(), "server should listen before signal shutdown");

    const int rc = ::raise(SIGTERM);
    expect(rc == 0, "raise(SIGTERM) should succeed");

    loop.loop();

    expect(server.stopped(), "server should stop after SIGTERM signalfd event");
    expect(!server.started(), "server should no longer report started after signal shutdown");
    expect(!server.listening(), "server should close listener after signal shutdown");
    expect(server.sessionCount() == 0, "signal shutdown should leave no active sessions");
}

}  // namespace

std::vector<TestCase> tcpServerTests() {
    return {
        {"tcp server rejects null loop", testTcpServerRejectsNullLoop},
        {"tcp server accepts client and dispatches message", testTcpServerAcceptsClientAndDispatchesMessage},
        {"tcp server sends to session and user", testTcpServerSendsToSessionAndUser},
        {"tcp server stops on signalfd event", testTcpServerStopsOnSignalFdEvent},
    };
}
