#pragma once

#include "liteim/net/Acceptor.hpp"
#include "liteim/net/Channel.hpp"
#include "liteim/net/Session.hpp"
#include "liteim/protocol/Packet.hpp"

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace liteim::net {

class EventLoop;

class TcpServer {
public:
    using UserId = std::uint64_t;
    using MessageCallback = std::function<void(Session&, const protocol::Packet&)>;

    TcpServer(EventLoop* loop, const std::string& listen_ip, std::uint16_t port);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void start();
    void stop();

    bool started() const;
    bool stopped() const;
    bool listening() const;
    std::uint16_t port() const;
    std::size_t sessionCount() const;

    bool sendToSession(int session_fd, const protocol::Packet& packet);
    bool bindUserToSession(UserId user_id, int session_fd);
    void unbindUser(UserId user_id);
    bool sendToUser(UserId user_id, const protocol::Packet& packet);

    void setMessageCallback(MessageCallback callback);
    EventLoop* loop() const;

private:
    int createSignalFd();
    void restoreSignalMask();
    void closeSignalFd();

    void handleNewConnection(int conn_fd, const sockaddr_in& peer_address);
    void handleSessionMessage(Session& session, const protocol::Packet& packet);
    void handleSessionClosed(int session_fd);
    void handleSignal();

    void closeAllSessions();
    void cleanupRetiredSessions();

    EventLoop* loop_ = nullptr;
    Acceptor acceptor_;
    sigset_t signal_mask_{};
    sigset_t previous_signal_mask_{};
    bool signal_mask_installed_ = false;
    int signal_fd_ = -1;
    Channel signal_channel_;
    std::unordered_map<int, std::shared_ptr<Session>> sessions_;
    std::unordered_map<UserId, int> user_session_fds_;
    std::vector<std::shared_ptr<Session>> retired_sessions_;
    MessageCallback message_callback_;
    bool started_ = false;
    bool stopped_ = false;
};

}  // namespace liteim::net
