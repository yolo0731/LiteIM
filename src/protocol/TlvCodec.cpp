#include "liteim/protocol/TlvCodec.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace liteim {
namespace {

void appendUint16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void appendUint32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void appendUint64Value(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFULL));
    }
}

std::uint16_t readUint16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                      static_cast<std::uint16_t>(data[1]));
}

std::uint32_t readUint32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint64_t readUint64(const TlvValue& value) {
    std::uint64_t result = 0;
    for (const auto byte : value) {
        result = (result << 8U) | static_cast<std::uint64_t>(byte);
    }
    return result;
}

Status appendValue(TlvType type,
                   const std::uint8_t* data,
                   std::size_t len,
                   std::vector<std::uint8_t>& output) {
    if (type == TlvType::Unknown) {
        return Status::error(ErrorCode::InvalidArgument, "cannot encode unknown tlv type");
    }
    if (data == nullptr && len != 0) {
        return Status::error(ErrorCode::InvalidArgument, "tlv value data is null");
    }
    if (len > kMaxTlvValueLength) {
        return Status::error(ErrorCode::InvalidArgument, "tlv value is too large");
    }

    output.reserve(output.size() + kTlvHeaderSize + len);
    appendUint16(output, static_cast<std::uint16_t>(type));
    appendUint32(output, static_cast<std::uint32_t>(len));
    if (len != 0) {
        output.insert(output.end(), data, data + len);
    }
    return Status::ok();
}

const TlvValues* findValues(const TlvMap& map, TlvType type) {
    const auto it = map.find(type);
    if (it == map.end() || it->second.empty()) {
        return nullptr;
    }
    return &it->second;
}

}  // namespace

Status appendString(TlvType type, std::string_view value, std::vector<std::uint8_t>& output) {
    return appendValue(type,
                       reinterpret_cast<const std::uint8_t*>(value.data()),
                       value.size(),
                       output);
}

Status appendUint64(TlvType type, std::uint64_t value, std::vector<std::uint8_t>& output) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(sizeof(std::uint64_t));
    appendUint64Value(bytes, value);
    return appendValue(type, bytes.data(), bytes.size(), output);
}

Status appendInt64(TlvType type, std::int64_t value, std::vector<std::uint8_t>& output) {
    return appendUint64(type, static_cast<std::uint64_t>(value), output);
}

Status parseTlvMap(const std::uint8_t* data, std::size_t len, TlvMap& output) {
    output.clear();
    if (data == nullptr && len != 0) {
        return Status::error(ErrorCode::InvalidArgument, "tlv body data is null");
    }

    std::size_t offset = 0;
    while (offset < len) {
        if (len - offset < kTlvHeaderSize) {
            return Status::error(ErrorCode::ParseError, "tlv header is incomplete");
        }

        const auto type = static_cast<TlvType>(readUint16(data + offset));
        const auto value_len = readUint32(data + offset + 2);
        offset += kTlvHeaderSize;

        if (value_len > kMaxTlvValueLength) {
            return Status::error(ErrorCode::ParseError, "tlv value is too large");
        }
        if (value_len > len - offset) {
            return Status::error(ErrorCode::ParseError, "tlv length exceeds body size");
        }

        auto& values = output[type];
        values.emplace_back(data + offset, data + offset + value_len);
        offset += value_len;
    }

    return Status::ok();
}

Status parseTlvMap(const std::vector<std::uint8_t>& body, TlvMap& output) {
    return parseTlvMap(body.data(), body.size(), output);
}

Status getString(const TlvMap& map, TlvType type, std::string& output) {
    const auto* values = findValues(map, type);
    if (values == nullptr) {
        return Status::error(ErrorCode::NotFound, "missing required string field");
    }

    const auto& value = values->front();
    output.assign(value.begin(), value.end());
    return Status::ok();
}

Status getUint64(const TlvMap& map, TlvType type, std::uint64_t& output) {
    const auto* values = findValues(map, type);
    if (values == nullptr) {
        return Status::error(ErrorCode::NotFound, "missing required uint64 field");
    }

    const auto& value = values->front();
    if (value.size() != sizeof(std::uint64_t)) {
        return Status::error(ErrorCode::ParseError, "uint64 tlv field must be 8 bytes");
    }

    output = readUint64(value);
    return Status::ok();
}

Status getRepeatedString(const TlvMap& map, TlvType type, std::vector<std::string>& output) {
    output.clear();
    const auto* values = findValues(map, type);
    if (values == nullptr) {
        return Status::error(ErrorCode::NotFound, "missing required repeated string field");
    }

    output.reserve(values->size());
    for (const auto& value : *values) {
        output.emplace_back(value.begin(), value.end());
    }
    return Status::ok();
}

}  // namespace liteim
