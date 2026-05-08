#include "liteim/net/Buffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>

namespace liteim {

Buffer::Buffer(std::size_t initial_size) : buffer_(initial_size == 0 ? 1 : initial_size) {
}

std::size_t Buffer::readableBytes() const noexcept {
    return write_index_ - read_index_;
}

std::size_t Buffer::writableBytes() const noexcept {
    return buffer_.size() - write_index_;
}

const Byte* Buffer::peek() const noexcept {
    return buffer_.data() + read_index_;
}

Status Buffer::append(const Byte* data, std::size_t len) {
    if (data == nullptr && len != 0) {
        return Status::error(ErrorCode::InvalidArgument, "buffer append input is null");
    }
    if (len == 0) {
        return Status::ok();
    }

    ensureWritableBytes(len);
    std::memcpy(buffer_.data() + write_index_, data, len);
    write_index_ += len;
    return Status::ok();
}

Status Buffer::append(const Bytes& data) {
    return append(data.data(), data.size());
}

Status Buffer::append(const std::string& value) {
    return append(reinterpret_cast<const Byte*>(value.data()), value.size());
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
    std::string result(reinterpret_cast<const char*>(peek()), readableBytes());
    retrieveAll();
    return result;
}

void Buffer::ensureWritableBytes(std::size_t len) {
    if (writableBytes() >= len) {
        return;
    }

    const auto readable = readableBytes();
    if (read_index_ + writableBytes() >= len) {
        std::move(buffer_.begin() + static_cast<std::ptrdiff_t>(read_index_),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(write_index_), buffer_.begin());
        read_index_ = 0;
        write_index_ = readable;
        return;
    }

    if (read_index_ != 0) {
        std::move(buffer_.begin() + static_cast<std::ptrdiff_t>(read_index_),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(write_index_), buffer_.begin());
        read_index_ = 0;
        write_index_ = readable;
    }

    buffer_.resize(write_index_ + len);
}

} // namespace liteim
