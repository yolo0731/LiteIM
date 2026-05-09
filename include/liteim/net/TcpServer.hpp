#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/net/EventLoopThreadPool.hpp"
#include "liteim/net/Session.hpp"
#include "liteim/protocol/Packet.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace liteim {

class Acceptor;
class EventLoop;
class UniqueFd;

class TcpServer {
public:
    using MessageCallback = std::function<void(const Session::Ptr&, const Packet&)>;

    TcpServer(EventLoop* base_loop, std::string listen_ip, std::uint16_t port, std::size_t io_thread_count);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void setMessageCallback(MessageCallback callback);

    void start();
    void stop() noexcept;

    std::uint16_t port() const noexcept;
    std::size_t sessionCount() const;
    bool started() const noexcept;

    Status sendToSession(std::uint64_t session_id, const Packet& packet);
    Status sendToUser(std::uint64_t user_id, const Packet& packet);

private:
    void stopInLoop() noexcept;
    void handleNewConnection(UniqueFd accepted_fd);
    void createSessionInLoop(EventLoop* io_loop, std::shared_ptr<UniqueFd> accepted_fd);
    void handleMessage(const Session::Ptr& session, const Packet& packet);
    void removeSession(std::uint64_t session_id);

    EventLoop* base_loop_;
    std::string listen_ip_;
    std::uint16_t requested_port_;
    EventLoopThreadPool io_threads_;
    std::unique_ptr<Acceptor> acceptor_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, Session::Ptr> sessions_;
    MessageCallback message_callback_;
    std::atomic<std::uint16_t> port_{0};
    std::atomic<std::uint64_t> next_session_id_{1};
    std::atomic_bool started_{false};
};

} // namespace liteim
