#include "liteim/net/Session.hpp"

#include "liteim/base/Logger.hpp"
#include "liteim/base/Timestamp.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <array>
#include <cerrno>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>
#include <utility>

namespace liteim {

namespace {

constexpr std::size_t kReadBufferSize = 4096;

} // namespace

Session::Session(EventLoop* loop, UniqueFd fd, std::uint64_t id)
    : loop_(loop), fd_(std::move(fd)), id_(id), channel_(nullptr),
      last_active_time_ms_(Timestamp::now().millisecondsSinceEpoch()) {
    if (loop_ == nullptr) {
        throw std::invalid_argument("Session requires a valid EventLoop");
    }
    if (!fd_) {
        throw std::invalid_argument("Session requires a valid fd");
    }

    const auto status = setNonBlocking(fd_.fd());
    if (!status.isOk()) {
        throw std::runtime_error(status.message());
    }
    channel_ = std::make_unique<Channel>(loop_, fd_.fd());
}

std::uint64_t Session::id() const noexcept {
    return id_;
}

EventLoop* Session::ownerLoop() const noexcept {
    return loop_;
}

bool Session::closed() const noexcept {
    return closed_.load();
}

std::size_t Session::outputHighWaterMark() const noexcept {
    return output_high_water_mark_;
}

std::size_t Session::pendingOutputBytes() const {
    loop_->assertInLoopThread();
    return output_buffer_.readableBytes();
}

std::int64_t Session::lastActiveTimeMilliseconds() const noexcept {
    return last_active_time_ms_.load();
}

void Session::setMessageCallback(MessageCallback callback) {
    message_callback_ = std::move(callback);
}

void Session::setCloseCallback(CloseCallback callback) {
    close_callback_ = std::move(callback);
}

void Session::setOutputHighWaterMark(std::size_t high_water_mark) {
    loop_->assertInLoopThread();
    if (high_water_mark == 0) {
        throw std::invalid_argument("output high water mark must be positive");
    }

    output_high_water_mark_ = high_water_mark;
}

void Session::start() {
    auto self = shared_from_this();
    loop_->runInLoop([self]() { self->startInLoop(); });
}

Status Session::sendPacket(const Packet& packet) {
    Bytes encoded;
    const auto status = encodePacket(packet, encoded);
    if (!status.isOk()) {
        return status;
    }

    auto self = shared_from_this();
    if (loop_->isInLoopThread()) { // loop_是Session所在的EventLoop的指针
        self->sendEncodedInLoop(std::move(encoded));
        return Status::ok();
    }

    loop_->queueInLoop([self, encoded = std::move(encoded)]() mutable { self->sendEncodedInLoop(std::move(encoded)); });
    return Status::ok();
}

void Session::close() {
    auto self = shared_from_this();
    loop_->runInLoop([self]() { self->closeInLoop(); });
}

void Session::startInLoop() {
    loop_->assertInLoopThread();
    if (state_ != SessionState::kNew || closed_.load()) {
        return;
    }

    auto self = shared_from_this();
    std::weak_ptr<Session> weak_self(self);
    channel_->tie(self);
    channel_->setReadCallback([weak_self]() {
        if (auto owner = weak_self.lock()) {
            owner->handleRead();
        }
    });
    channel_->setWriteCallback([weak_self]() {
        if (auto owner = weak_self.lock()) {
            owner->handleWrite();
        }
    });
    channel_->setCloseCallback([weak_self]() {
        if (auto owner = weak_self.lock()) {
            owner->closeInLoop();
        }
    });
    channel_->setErrorCallback([weak_self]() {
        if (auto owner = weak_self.lock()) {
            owner->closeInLoop();
        }
    });

    channel_->enableReading();
    channel_registered_ = true;
    state_ = SessionState::kStarted;
    if (output_buffer_.readableBytes() > 0) {
        channel_->enableWriting();
    }
}

void Session::sendEncodedInLoop(Bytes encoded) {
    loop_->assertInLoopThread();
    if (state_ != SessionState::kStarted || closed_.load() || encoded.empty()) {
        return;
    }

    const auto pending_bytes = output_buffer_.readableBytes();
    if (encoded.size() > output_high_water_mark_ ||
        pending_bytes > output_high_water_mark_ - encoded.size()) {
        Logger::get()->warn("Session {} output buffer high water mark exceeded: pending={}, incoming={}, limit={}",
                            id_,
                            pending_bytes,
                            encoded.size(),
                            output_high_water_mark_);
        closeInLoop();
        return;
    }

    const auto status = output_buffer_.append(encoded);
    if (!status.isOk()) {
        closeInLoop();
        return;
    }

    if (state_ == SessionState::kStarted && channel_ != nullptr && !channel_->isWriting()) {
        channel_->enableWriting();
    }
}

void Session::handleRead() {
    loop_->assertInLoopThread();
    if (state_ != SessionState::kStarted || closed_.load() || !fd_) {
        return;
    }

    std::array<Byte, kReadBufferSize> buffer{};
    while (fd_) {
        const auto n = ::read(fd_.fd(), buffer.data(), buffer.size());
        if (n > 0) {
            std::vector<Packet> packets;
            const auto status = decoder_.feed(buffer.data(), static_cast<std::size_t>(n), packets);
            if (!status.isOk()) {
                closeInLoop();
                return;
            }

            for (const auto& packet : packets) {
                if (closed_.load()) {
                    return;
                }
                updateLastActiveTime();
                if (message_callback_) {
                    message_callback_(shared_from_this(), packet);
                }
            }
            continue;
        }

        if (n == 0) {
            closeInLoop();
            return;
        }

        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }

        closeInLoop();
        return;
    }
}

void Session::handleWrite() {
    loop_->assertInLoopThread();
    if (state_ != SessionState::kStarted || closed_.load() || !fd_) {
        return;
    }

    while (output_buffer_.readableBytes() > 0) {
        const auto n = ::write(fd_.fd(), output_buffer_.peek(), output_buffer_.readableBytes());
        if (n > 0) {
            const auto retrieve_status = output_buffer_.retrieve(static_cast<std::size_t>(n));
            if (!retrieve_status.isOk()) {
                closeInLoop();
                return;
            }
            continue;
        }

        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        closeInLoop();
        return;
    }

    if (output_buffer_.readableBytes() == 0 && channel_ != nullptr && channel_->isWriting()) {
        channel_->disableWriting();
    }
}

void Session::closeInLoop() {
    loop_->assertInLoopThread();
    if (closed_.exchange(true)) {
        return;
    }
    state_ = SessionState::kClosing;

    if (channel_ != nullptr && channel_registered_) {
        try {
            loop_->removeChannel(channel_.get());
        } catch (...) {
        }
        channel_registered_ = false;
    }

    fd_.reset();
    output_buffer_.retrieveAll();

    auto self = shared_from_this();
    loop_->queueInLoop([self]() {
        if (self->closed_.load()) {
            self->channel_.reset();
        }
    });

    if (close_callback_) {
        close_callback_(self);
    }
    state_ = SessionState::kClosed;
}

void Session::updateLastActiveTime() noexcept {
    last_active_time_ms_ = Timestamp::now().millisecondsSinceEpoch();
}

} // namespace liteim
