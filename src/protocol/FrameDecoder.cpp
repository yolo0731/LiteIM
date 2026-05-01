#include "liteim/protocol/FrameDecoder.hpp"

#include <utility>

namespace liteim::protocol {

std::vector<Packet> FrameDecoder::feed(const char* data, std::size_t len) {
    std::vector<Packet> packets;
    if (has_error_) {
        return packets;
    }

    if (data == nullptr && len > 0) {
        setError("feed data is null");
        return packets;
    }

    if (len > 0) {
        buffer_.append(data, len);
    }

    while (buffer_.size() >= kPacketHeaderSize) {
        const auto header = parseHeader(buffer_.data(), buffer_.size());
        if (!header.has_value()) {
            setError("invalid packet header");
            return packets;
        }

        const std::size_t frame_size = kPacketHeaderSize + header->body_len;
        if (buffer_.size() < frame_size) {
            break;
        }

        Packet packet;
        packet.header = *header;
        packet.body.assign(buffer_.data() + kPacketHeaderSize, header->body_len);
        packets.push_back(std::move(packet));

        buffer_.erase(0, frame_size);
    }

    return packets;
}

void FrameDecoder::reset() {
    buffer_.clear();
    has_error_ = false;
    error_message_.clear();
}

void FrameDecoder::setError(std::string message) {
    buffer_.clear();
    has_error_ = true;
    error_message_ = std::move(message);
}

}  // namespace liteim::protocol

