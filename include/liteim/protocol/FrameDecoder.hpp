#pragma once

#include <cstddef>
#include <vector>

#include "liteim/base/Status.hpp"
#include "liteim/base/Types.hpp"
#include "liteim/protocol/Packet.hpp"

namespace liteim {

class FrameDecoder {
public:
    Status feed(const Byte* data, std::size_t len, std::vector<Packet>& output);
    Status feed(const Bytes& data, std::vector<Packet>& output);

    bool hasError() const noexcept;
    std::size_t bufferedBytes() const noexcept;
    void reset();

private:
    Bytes buffer_;
    bool error_{false};
};

}  // namespace liteim
