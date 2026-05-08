#pragma once

#include <cstddef>
#include <cstdint>

#include "liteim/base/Types.hpp"

namespace liteim {

inline void appendUint16BE(Bytes& output, std::uint16_t value) {
    output.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<Byte>(value & 0xFFU));
}

inline void appendUint32BE(Bytes& output, std::uint32_t value) {
    output.push_back(static_cast<Byte>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<Byte>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<Byte>(value & 0xFFU));
}

inline void appendUint64BE(Bytes& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<Byte>((value >> shift) & 0xFFULL));
    }
}

inline std::uint16_t readUint16BE(const Byte* data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                      static_cast<std::uint16_t>(data[1]));
}

inline std::uint32_t readUint32BE(const Byte* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) | (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) | static_cast<std::uint32_t>(data[3]);
}

inline std::uint64_t readUint64BE(const Byte* data) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(std::uint64_t); ++index) {
        value = (value << 8U) | static_cast<std::uint64_t>(data[index]);
    }
    return value;
}

} // namespace liteim
