#include "TestUtil.hpp"

#include "liteim/net/Acceptor.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

using liteim::net::Acceptor;
using liteim::net::EventLoop;
using liteim::net::closeFd;
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

    return client_fd;
}

bool isNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    expect(flags >= 0, "fcntl should read accepted fd flags");
    return (flags & O_NONBLOCK) != 0;
}

void testAcceptorListenIsIdempotent() {
    EventLoop loop;
    Acceptor acceptor(&loop, "127.0.0.1", 0);

    expect(!acceptor.listening(), "acceptor should not listen before listen() is called");
    expect(acceptor.listenFd() >= 0, "acceptor should own a valid listen fd");
    expect(acceptor.port() != 0, "port 0 bind should be resolved to an actual local port");

    acceptor.listen();
    acceptor.listen();

    expect(acceptor.listening(), "acceptor should report listening after listen()");
}

void testAcceptorAcceptsConnectionAndInvokesCallback() {
    EventLoop loop;
    Acceptor acceptor(&loop, "127.0.0.1", 0);

    bool callback_called = false;
    bool accepted_fd_nonblocking = false;
    std::uint16_t peer_port = 0;

    acceptor.setNewConnectionCallback([&](int conn_fd, const sockaddr_in& peer_address) {
        callback_called = true;
        accepted_fd_nonblocking = isNonBlocking(conn_fd);
        peer_port = ntohs(peer_address.sin_port);
        closeFd(conn_fd);
        loop.quit();
    });
    acceptor.listen();

    auto client_fd = connectClient(acceptor.port());
    loop.loop();

    expect(callback_called, "acceptor should invoke callback for a new connection");
    expect(accepted_fd_nonblocking, "accepted connection fd should be nonblocking");
    expect(peer_port != 0, "callback should receive peer address information");
}

void testAcceptorAcceptsAllPendingConnections() {
    EventLoop loop;
    Acceptor acceptor(&loop, "127.0.0.1", 0);

    int accepted_count = 0;
    acceptor.setNewConnectionCallback([&](int conn_fd, const sockaddr_in&) {
        ++accepted_count;
        closeFd(conn_fd);
        if (accepted_count == 3) {
            loop.quit();
        }
    });
    acceptor.listen();

    std::vector<FdGuard> clients;
    clients.push_back(connectClient(acceptor.port()));
    clients.push_back(connectClient(acceptor.port()));
    clients.push_back(connectClient(acceptor.port()));

    loop.loop();

    expect(accepted_count == 3, "acceptor should drain all pending connections");
}

void testAcceptorRejectsNullLoop() {
    bool thrown = false;
    try {
        Acceptor acceptor(nullptr, "127.0.0.1", 0);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(thrown, "acceptor should reject null EventLoop");
}

}  // namespace

std::vector<TestCase> acceptorTests() {
    return {
        {"acceptor listen is idempotent", testAcceptorListenIsIdempotent},
        {"acceptor accepts connection and invokes callback", testAcceptorAcceptsConnectionAndInvokesCallback},
        {"acceptor accepts all pending connections", testAcceptorAcceptsAllPendingConnections},
        {"acceptor rejects null loop", testAcceptorRejectsNullLoop},
    };
}
