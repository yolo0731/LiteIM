#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace liteim::protocol {

inline constexpr std::uint32_t kPacketMagic = 0x4C494D31;  // "LIM1"
inline constexpr std::uint16_t kPacketVersion = 1;
inline constexpr std::size_t kPacketHeaderSize = 16;
inline constexpr std::uint32_t kMaxBodyLength = 1024 * 1024;

struct PacketHeader {
    std::uint32_t magic = kPacketMagic;
    std::uint16_t version = kPacketVersion;
    std::uint16_t msg_type = 0;
    std::uint32_t seq_id = 0;
    std::uint32_t body_len = 0;
};

struct Packet {
    PacketHeader header;
    std::string body;
};

std::string encodePacket(const Packet& packet);

std::optional<PacketHeader> parseHeader(const char* data, std::size_t len);

}  // namespace liteim::protocol

