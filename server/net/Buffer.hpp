#pragma once

#include <cstddef>
#include <string>

namespace liteim::net {

class Buffer {
public:
    void append(const char* data, std::size_t len);
    void appendString(const std::string& data);

    std::size_t readableBytes() const;
    const char* peek() const;

    void retrieve(std::size_t len);
    std::string retrieveAllAsString();

private:
    void compactIfNeeded();

    std::string buffer_;
    std::size_t read_index_ = 0;
};

}  // namespace liteim::net

