#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "liteim/base/Status.hpp"
#include "liteim/protocol/Packet.hpp"

namespace liteim {

class FrameDecoder {
public:
    Status feed(const std::uint8_t* data, std::size_t len, std::vector<Packet>& output);
    Status feed(const std::vector<std::uint8_t>& data, std::vector<Packet>& output);

    bool hasError() const noexcept;
    std::size_t bufferedBytes() const noexcept;
    void reset();

private:
    std::vector<std::uint8_t> buffer_;
    bool error_{false};
};

}  // namespace liteim
