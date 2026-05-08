#pragma once

#include <cstddef>
#include <cstdint>

#include "liteim/base/Types.hpp"
#include "liteim/base/Status.hpp"
#include "liteim/protocol/MessageType.hpp"

namespace liteim {

inline constexpr std::uint32_t kPacketMagic = 0x4C494D31;  // "LIM1"
inline constexpr std::uint8_t kPacketVersion = 1;
inline constexpr std::uint8_t kPacketFlagsNone = 0;
inline constexpr std::size_t kPacketHeaderSize = 20;
inline constexpr std::uint32_t kMaxPacketBodyLength = 1024 * 1024;

struct PacketHeader {
    std::uint32_t magic{kPacketMagic};
    std::uint8_t version{kPacketVersion};
    std::uint8_t flags{kPacketFlagsNone};
    MessageType msg_type{MessageType::Unknown};
    std::uint64_t seq_id{0};
    std::uint32_t body_len{0};
};

struct Packet {
    PacketHeader header;
    Bytes body;
};

Status validateHeader(const PacketHeader& header);
Status encodePacket(const Packet& packet, Bytes& output);
Status parseHeader(const Byte* data, std::size_t len, PacketHeader& output);

}  // namespace liteim
