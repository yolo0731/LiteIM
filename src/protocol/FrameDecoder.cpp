#include "liteim/protocol/FrameDecoder.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace liteim {

Status FrameDecoder::feed(const std::uint8_t* data,
                          std::size_t len,
                          std::vector<Packet>& output) {
    output.clear();

    if (error_) {
        return Status::error(ErrorCode::ParseError, "frame decoder is in error state");
    }
    if (data == nullptr && len != 0) {
        return Status::error(ErrorCode::InvalidArgument, "frame decoder input is null");
    }

    if (len != 0) {
        buffer_.insert(buffer_.end(), data, data + len);
    }

    while (buffer_.size() >= kPacketHeaderSize) {
        PacketHeader header;
        const auto header_status = parseHeader(buffer_.data(), kPacketHeaderSize, header);
        if (!header_status.isOk()) {
            error_ = true;
            return header_status;
        }

        const auto frame_size = kPacketHeaderSize + static_cast<std::size_t>(header.body_len);
        if (buffer_.size() < frame_size) {
            break;
        }

        Packet packet;
        packet.header = header;
        packet.body.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(kPacketHeaderSize),
                           buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
        output.push_back(std::move(packet));

        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
    }

    return Status::ok();
}

Status FrameDecoder::feed(const std::vector<std::uint8_t>& data, std::vector<Packet>& output) {
    return feed(data.data(), data.size(), output);
}

bool FrameDecoder::hasError() const noexcept {
    return error_;
}

std::size_t FrameDecoder::bufferedBytes() const noexcept {
    return buffer_.size();
}

void FrameDecoder::reset() {
    buffer_.clear();
    error_ = false;
}

}  // namespace liteim
