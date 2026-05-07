#include "liteim/net/Session.hpp"

#include "liteim/base/Timestamp.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SocketUtil.hpp"

#include <array>
#include <cerrno>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>

namespace liteim {

namespace {

constexpr std::size_t kReadBufferSize = 4096;

std::runtime_error statusError(const Status& status) {
    return std::runtime_error(status.message());
}

}  // namespace

Session::Session(EventLoop* loop, int fd)
    : loop_(loop),
      fd_(fd),
      channel_(loop == nullptr ? nullptr : std::make_unique<Channel>(loop, fd)),
      last_active_time_ms_(Timestamp::now().millisecondsSinceEpoch()) {
    if (loop_ == nullptr) {
        throw std::invalid_argument("Session requires a valid EventLoop");
    }
    if (fd < 0) {
        throw std::invalid_argument("Session requires a valid fd");
    }

    const auto status = setNonBlocking(fd_.fd());
    if (!status.isOk()) {
        throw statusError(status);
    }
}

int Session::fd() const noexcept {
    return fd_.fd();
}

EventLoop* Session::ownerLoop() const noexcept {
    return loop_;
}

bool Session::closed() const noexcept {
    return closed_.load();
}

std::size_t Session::pendingOutputBytes() const noexcept {
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

void Session::start() {
    auto self = shared_from_this();
    loop_->runInLoop([self]() { self->startInLoop(); });
}

Status Session::sendPacket(const Packet& packet) {
    std::vector<std::uint8_t> encoded;
    const auto status = encodePacket(packet, encoded);
    if (!status.isOk()) {
        return status;
    }

    auto self = shared_from_this();
    if (loop_->isInLoopThread()) {
        self->sendEncodedInLoop(std::move(encoded));
        return Status::ok();
    }

    loop_->queueInLoop([self, encoded = std::move(encoded)]() mutable {
        self->sendEncodedInLoop(std::move(encoded));
    });
    return Status::ok();
}

void Session::close() {
    auto self = shared_from_this();
    loop_->runInLoop([self]() { self->closeInLoop(); });
}

void Session::startInLoop() {
    loop_->assertInLoopThread();
    if (started_ || closed_.load()) {
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
            owner->handleClose();
        }
    });
    channel_->setErrorCallback([weak_self]() {
        if (auto owner = weak_self.lock()) {
            owner->handleClose();
        }
    });

    channel_->enableReading();
    channel_registered_ = true;
    started_ = true;
    if (output_buffer_.readableBytes() > 0) {
        channel_->enableWriting();
    }
}

void Session::sendEncodedInLoop(std::vector<std::uint8_t> encoded) {
    loop_->assertInLoopThread();
    if (closed_.load() || encoded.empty()) {
        return;
    }

    const auto status = output_buffer_.append(encoded.data(), encoded.size());
    if (!status.isOk()) {
        closeInLoop();
        return;
    }
    updateLastActiveTime();

    if (started_ && channel_ != nullptr && !channel_->isWriting()) {
        channel_->enableWriting();
    }
}

void Session::handleRead() {
    loop_->assertInLoopThread();
    if (closed_.load() || !fd_) {
        return;
    }

    std::array<char, kReadBufferSize> buffer{};
    while (fd_) {
        const auto n = ::read(fd_.fd(), buffer.data(), buffer.size());
        if (n > 0) {
            updateLastActiveTime();
            const auto append_status = input_buffer_.append(buffer.data(), static_cast<std::size_t>(n));
            if (!append_status.isOk()) {
                closeInLoop();
                return;
            }
            if (!feedInputBuffer()) {
                return;
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
    if (closed_.load() || !fd_) {
        return;
    }

    while (output_buffer_.readableBytes() > 0) {
        const auto n = ::write(fd_.fd(), output_buffer_.peek(), output_buffer_.readableBytes());
        if (n > 0) {
            updateLastActiveTime();
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

void Session::handleClose() {
    closeInLoop();
}

void Session::closeInLoop() {
    loop_->assertInLoopThread();
    if (closed_.exchange(true)) {
        return;
    }

    if (channel_ != nullptr && channel_registered_) {
        try {
            loop_->removeChannel(channel_.get());
        } catch (...) {
        }
        channel_registered_ = false;
    }

    fd_.reset();
    input_buffer_.retrieveAll();
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
}

bool Session::feedInputBuffer() {
    if (input_buffer_.readableBytes() == 0) {
        return true;
    }

    std::vector<Packet> packets;
    const auto status = decoder_.feed(
        reinterpret_cast<const std::uint8_t*>(input_buffer_.peek()),
        input_buffer_.readableBytes(),
        packets);
    input_buffer_.retrieveAll();
    if (!status.isOk()) {
        closeInLoop();
        return false;
    }

    for (const auto& packet : packets) {
        if (closed_.load()) {
            return false;
        }
        if (message_callback_) {
            message_callback_(shared_from_this(), packet);
        }
    }

    return !closed_.load();
}

void Session::updateLastActiveTime() noexcept {
    last_active_time_ms_ = Timestamp::now().millisecondsSinceEpoch();
}

}  // namespace liteim
