#include "liteim/protocol/TlvCodec.hpp"

#include "liteim/protocol/ByteOrder.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace liteim {
namespace {
Status appendValue(TlvType type, const Byte* data, std::size_t len, Bytes& output) {
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
    appendUint16BE(output, static_cast<std::uint16_t>(type));
    appendUint32BE(output, static_cast<std::uint32_t>(len));
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

Status appendString(TlvType type, const std::string& value, Bytes& output) {
    return appendValue(type, reinterpret_cast<const Byte*>(value.data()), value.size(), output);
}

Status appendUint64(TlvType type, std::uint64_t value, Bytes& output) {
    Bytes bytes;
    bytes.reserve(sizeof(std::uint64_t));
    appendUint64BE(bytes, value);
    return appendValue(type, bytes.data(), bytes.size(), output);
}

Status parseTlvMap(const Byte* data, std::size_t len, TlvMap& output) {
    output.clear();
    if (data == nullptr && len != 0) {
        return Status::error(ErrorCode::InvalidArgument, "tlv body data is null");
    }

    std::size_t offset = 0;
    while (offset < len) {
        if (len - offset < kTlvHeaderSize) {
            return Status::error(ErrorCode::ParseError, "tlv header is incomplete");
        }

        const auto type = static_cast<TlvType>(readUint16BE(data + offset));
        const auto value_len = readUint32BE(data + offset + 2);
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

Status parseTlvMap(const Bytes& body, TlvMap& output) {
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

    output = readUint64BE(value.data());
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

Status getRepeatedUint64(const TlvMap& map, TlvType type, std::vector<std::uint64_t>& output) {
    output.clear();
    const auto* values = findValues(map, type);
    if (values == nullptr) {
        return Status::error(ErrorCode::NotFound, "missing required repeated uint64 field");
    }

    output.reserve(values->size());
    for (const auto& value : *values) {
        if (value.size() != sizeof(std::uint64_t)) {
            return Status::error(ErrorCode::ParseError, "uint64 tlv field must be 8 bytes");
        }
        output.push_back(readUint64BE(value.data()));
    }
    return Status::ok();
}

}  // namespace liteim
