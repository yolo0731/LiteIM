#include "liteim/net/Buffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace liteim {

Buffer::Buffer(std::size_t initial_size) : buffer_(initial_size == 0 ? 1 : initial_size) {}

std::size_t Buffer::readableBytes() const noexcept {
    return write_index_ - read_index_;
}

std::size_t Buffer::writableBytes() const noexcept {
    return buffer_.size() - write_index_;
}

const char* Buffer::peek() const noexcept {
    return begin() + read_index_;
}

Status Buffer::append(const char* data, std::size_t len) {
    if (data == nullptr && len != 0) {
        return Status::error(ErrorCode::InvalidArgument, "buffer append input is null");
    }
    if (len == 0) {
        return Status::ok();
    }

    ensureWritableBytes(len);
    std::memcpy(begin() + write_index_, data, len);
    write_index_ += len;
    return Status::ok();
}

Status Buffer::append(const std::uint8_t* data, std::size_t len) {
    return append(reinterpret_cast<const char*>(data), len);
}

void Buffer::appendString(std::string_view value) {
    const auto status = append(value.data(), value.size());
    (void)status;
}

Status Buffer::retrieve(std::size_t len) {
    if (len > readableBytes()) {
        return Status::error(ErrorCode::InvalidArgument, "buffer retrieve exceeds readable bytes");
    }
    if (len == readableBytes()) {
        retrieveAll();
        return Status::ok();
    }

    read_index_ += len;
    return Status::ok();
}

void Buffer::retrieveAll() noexcept {
    read_index_ = 0;
    write_index_ = 0;
}

std::string Buffer::retrieveAllAsString() {
    std::string result(peek(), readableBytes());
    retrieveAll();
    return result;
}

void Buffer::ensureWritableBytes(std::size_t len) {
    if (writableBytes() >= len) {
        return;
    }

    makeSpace(len);
}

char* Buffer::begin() noexcept {
    return buffer_.data();
}

const char* Buffer::begin() const noexcept {
    return buffer_.data();
}

void Buffer::makeSpace(std::size_t len) {
    const auto readable = readableBytes();
    if (read_index_ + writableBytes() >= len) {
        std::move(begin() + read_index_, begin() + write_index_, begin());
        read_index_ = 0;
        write_index_ = readable;
        return;
    }

    if (read_index_ != 0) {
        std::move(begin() + read_index_, begin() + write_index_, begin());
        read_index_ = 0;
        write_index_ = readable;
    }

    buffer_.resize(write_index_ + len);
}

}  // namespace liteim
