#pragma once

#include <cstddef>
#include <string>

#include "liteim/base/Status.hpp"
#include "liteim/base/Types.hpp"

namespace liteim {

inline constexpr std::size_t kDefaultBufferSize = 1024;

class Buffer {
public:
    explicit Buffer(std::size_t initial_size = kDefaultBufferSize);

    std::size_t readableBytes() const noexcept;
    std::size_t writableBytes() const noexcept;
    const Byte* peek() const noexcept;

    Status append(const Byte* data, std::size_t len);
    Status append(const Bytes& data);
    Status append(const std::string& value);

    Status retrieve(std::size_t len);
    void retrieveAll() noexcept;
    std::string retrieveAllAsString();

private:
    void ensureWritableBytes(std::size_t len);

    Bytes buffer_;
    std::size_t read_index_{0};
    std::size_t write_index_{0};
};

} // namespace liteim
