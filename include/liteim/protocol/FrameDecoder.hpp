#pragma once

#include "liteim/protocol/Packet.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace liteim::protocol {

class FrameDecoder {
public:
    std::vector<Packet> feed(const char* data, std::size_t len);

    bool hasError() const {
        return has_error_;
    }

    const std::string& errorMessage() const {
        return error_message_;
    }

    std::size_t bufferedBytes() const {
        return buffer_.size();
    }

    void reset();

private:
    void setError(std::string message);

    std::string buffer_;
    bool has_error_ = false;
    std::string error_message_;
};

}  // namespace liteim::protocol

