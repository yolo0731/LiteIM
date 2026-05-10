#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/net/Buffer.hpp"
#include "liteim/net/Channel.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/protocol/Packet.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace liteim {

class EventLoop;

inline constexpr std::size_t kSessionDefaultOutputHighWaterMark = 4 * 1024 * 1024;

enum class SessionState {
    kNew,
    kStarted,
    kClosing,
    kClosed,
};

class Session : public std::enable_shared_from_this<Session> {
public:
    using Ptr = std::shared_ptr<Session>;
    using MessageCallback = std::function<void(const Ptr&, const Packet&)>;
    using CloseCallback = std::function<void(const Ptr&)>;

    Session(EventLoop* loop, UniqueFd fd, std::uint64_t id = 0);
    ~Session() = default;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    std::uint64_t id() const noexcept;
    EventLoop* ownerLoop() const noexcept;
    bool closed() const noexcept;
    std::size_t outputHighWaterMark() const noexcept;
    std::size_t pendingOutputBytes() const;
    std::int64_t lastActiveTimeMilliseconds() const noexcept;

    void setMessageCallback(MessageCallback callback);
    void setCloseCallback(CloseCallback callback);
    void setOutputHighWaterMark(std::size_t high_water_mark);

    void start();
    Status sendPacket(const Packet& packet);
    void close();

private:
    void startInLoop();
    void sendEncodedInLoop(Bytes encoded);
    void handleRead();
    void handleWrite();
    void closeInLoop();
    void updateLastActiveTime() noexcept;

    EventLoop* loop_;
    UniqueFd fd_;
    std::uint64_t id_{0};
    std::unique_ptr<Channel> channel_;
    FrameDecoder decoder_;
    Buffer output_buffer_;
    std::size_t output_high_water_mark_{kSessionDefaultOutputHighWaterMark};
    MessageCallback message_callback_;
    CloseCallback close_callback_;
    std::atomic_bool closed_{false};
    SessionState state_{SessionState::kNew};
    bool channel_registered_{false};
    std::atomic<std::int64_t> last_active_time_ms_{0};
};

} // namespace liteim
