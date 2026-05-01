#include "liteim/net/Buffer.hpp"

#include <algorithm>
#include <stdexcept>

namespace liteim::net {

void Buffer::append(const char* data, std::size_t len) {
    if (len == 0) {
        return;
    }
    if (data == nullptr) {
        throw std::invalid_argument("buffer append data is null");
    }

    buffer_.append(data, len);
}

void Buffer::appendString(const std::string& data) {
    append(data.data(), data.size());
}

std::size_t Buffer::readableBytes() const {
    return buffer_.size() - read_index_;
}

const char* Buffer::peek() const {
    return buffer_.data() + read_index_;
}

void Buffer::retrieve(std::size_t len) {
    if (len >= readableBytes()) {
        buffer_.clear();
        read_index_ = 0;
        return;
    }

    read_index_ += len;
    compactIfNeeded();
}

std::string Buffer::retrieveAllAsString() {
    std::string result(peek(), readableBytes());
    buffer_.clear();
    read_index_ = 0;
    return result;
}

void Buffer::compactIfNeeded() {
    constexpr std::size_t kCompactThreshold = 1024;
    if (read_index_ < kCompactThreshold) {
        return;
    }

    if (read_index_ * 2 < buffer_.size()) {
        return;
    }

    buffer_.erase(0, read_index_);
    read_index_ = 0;
}

}  // namespace liteim::net

