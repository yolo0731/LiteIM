#include "liteim/protocol/FrameDecoder.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace liteim {

Status FrameDecoder::feed(const Byte* data, std::size_t len, std::vector<Packet>& output) {
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

    std::size_t read_index = 0;
    const auto compact_consumed = [this](std::size_t consumed) {
        if (consumed == 0) {
            return;
        }
        if (consumed == buffer_.size()) {
            buffer_.clear();
            return;
        }
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
    };

    while (buffer_.size() - read_index >= kPacketHeaderSize) {
        PacketHeader header;
        const auto header_status =
            parseHeader(buffer_.data() + read_index, kPacketHeaderSize, header);
        if (!header_status.isOk()) {
            compact_consumed(read_index);
            error_ = true;
            return header_status;
        }

        const auto frame_size = kPacketHeaderSize + static_cast<std::size_t>(header.body_len);
        if (buffer_.size() - read_index < frame_size) {
            break;
        }

        Packet packet;
        packet.header = header;
        packet.body.assign(buffer_.begin() +
                               static_cast<std::ptrdiff_t>(read_index + kPacketHeaderSize),
                           buffer_.begin() + static_cast<std::ptrdiff_t>(read_index + frame_size));
        output.push_back(std::move(packet));

        read_index += frame_size;
    }

    compact_consumed(read_index);
    return Status::ok();
}

Status FrameDecoder::feed(const Bytes& data, std::vector<Packet>& output) {
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
