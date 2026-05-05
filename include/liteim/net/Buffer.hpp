#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "liteim/base/Status.hpp"

namespace liteim {

inline constexpr std::size_t kDefaultBufferSize = 1024;

class Buffer {
public:
    explicit Buffer(std::size_t initial_size = kDefaultBufferSize);

    std::size_t readableBytes() const noexcept;
    std::size_t writableBytes() const noexcept;
    const char* peek() const noexcept;

    Status append(const char* data, std::size_t len);
    Status append(const std::uint8_t* data, std::size_t len);
    void appendString(std::string_view value);

    Status retrieve(std::size_t len);
    void retrieveAll() noexcept;
    std::string retrieveAllAsString();

    void ensureWritableBytes(std::size_t len);

private:
    char* begin() noexcept;
    const char* begin() const noexcept;
    void makeSpace(std::size_t len);

    std::vector<char> buffer_;
    std::size_t read_index_{0};
    std::size_t write_index_{0};
};

}  // namespace liteim
