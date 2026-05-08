#include "liteim/protocol/TlvCodec.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace liteim
{
    namespace
    {

        void appendUint16(Bytes &output, std::uint16_t value)
        {
            output.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
            output.push_back(static_cast<Byte>(value & 0xFFU));
        }

        void appendUint32(Bytes &output, std::uint32_t value)
        {
            output.push_back(static_cast<Byte>((value >> 24U) & 0xFFU));
            output.push_back(static_cast<Byte>((value >> 16U) & 0xFFU));
            output.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
            output.push_back(static_cast<Byte>(value & 0xFFU));
        }

        std::uint16_t readUint16(const Byte *data)
        {
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                              static_cast<std::uint16_t>(data[1]));
        }

        std::uint32_t readUint32(const Byte *data)
        {
            return (static_cast<std::uint32_t>(data[0]) << 24U) |
                   (static_cast<std::uint32_t>(data[1]) << 16U) |
                   (static_cast<std::uint32_t>(data[2]) << 8U) |
                   static_cast<std::uint32_t>(data[3]);
        }

        std::uint64_t readUint64(const TlvValue &value)
        {
            std::uint64_t result = 0;
            for (const auto byte : value)
            {
                result = (result << 8U) | static_cast<std::uint64_t>(byte);
            }
            return result;
        }

        Status appendValue(TlvType type,
                           const Byte *data,
                           std::size_t len,
                           Bytes &output)
        {
            if (type == TlvType::Unknown)
            {
                return Status::error(ErrorCode::InvalidArgument, "cannot encode unknown tlv type");
            }
            if (data == nullptr && len != 0)
            {
                return Status::error(ErrorCode::InvalidArgument, "tlv value data is null");
            }
            if (len > kMaxTlvValueLength)
            {
                return Status::error(ErrorCode::InvalidArgument, "tlv value is too large");
            }

            output.reserve(output.size() + kTlvHeaderSize + len);
            appendUint16(output, static_cast<std::uint16_t>(type));
            appendUint32(output, static_cast<std::uint32_t>(len));
            if (len != 0)
            {
                output.insert(output.end(), data, data + len);
            }
            return Status::ok();
        }

        const TlvValues *findValues(const TlvMap &map, TlvType type)
        {
            const auto it = map.find(type);
            if (it == map.end() || it->second.empty())
            {
                return nullptr;
            }
            return &it->second;
        }

    } // namespace

    Status appendString(TlvType type, const std::string &value, Bytes &output)
    {
        return appendValue(type,
                           reinterpret_cast<const Byte *>(value.data()),
                           value.size(),
                           output);
    }

    Status appendUint64(TlvType type, std::uint64_t value, Bytes &output)
    {
        Bytes bytes;
        bytes.reserve(sizeof(std::uint64_t));
        for (int shift = 56; shift >= 0; shift -= 8)
        {
            bytes.push_back(static_cast<Byte>((value >> shift) & 0xFFULL));
        }
        return appendValue(type, bytes.data(), bytes.size(), output);
    }

    Status parseTlvMap(const Byte *data, std::size_t len, TlvMap &output)
    {
        // parseTlvMap 负责从 body 字节流里循环读取 TLV 字段，检查格式和长度是否合法，然后按 TlvType 把每个 value 保存到 TlvMap 里
        output.clear();
        if (data == nullptr && len != 0)
        {
            return Status::error(ErrorCode::InvalidArgument, "tlv body data is null");
        }

        std::size_t offset = 0;
        while (offset < len)
        {
            if (len - offset < kTlvHeaderSize)
            {
                return Status::error(ErrorCode::ParseError, "tlv header is incomplete");
            }

            const auto type = static_cast<TlvType>(readUint16(data + offset));
            const auto value_len = readUint32(data + offset + 2);
            offset += kTlvHeaderSize;

            if (value_len > kMaxTlvValueLength)
            {
                return Status::error(ErrorCode::ParseError, "tlv value is too large");
            }
            if (value_len > len - offset)
            {
                return Status::error(ErrorCode::ParseError, "tlv length exceeds body size");
            }

            auto &values = output[type];
            values.emplace_back(data + offset, data + offset + value_len);
            offset += value_len;
        }

        return Status::ok();
    }

    Status parseTlvMap(const Bytes &body, TlvMap &output)
    {
        // 直接传 Bytes，调用方不用自己手动传 data() 和 size()
        return parseTlvMap(body.data(), body.size(), output);
    }

    Status getString(const TlvMap &map, TlvType type, std::string &output)
    // 从 TlvMap 里取出某个 type 对应的第一个 value，并转成 std::string。
    {
        const auto *values = findValues(map, type);
        if (values == nullptr)
        {
            return Status::error(ErrorCode::NotFound, "missing required string field");
        }

        const auto &value = values->front();
        output.assign(value.begin(), value.end());
        return Status::ok();
    }

    Status getUint64(const TlvMap &map, TlvType type, std::uint64_t &output)
    // 从 TlvMap 里取出某个 type 对应的第一个 value，检查长度必须是 8 字节，然后转成 uint64_t
    {
        const auto *values = findValues(map, type);
        if (values == nullptr)
        {
            return Status::error(ErrorCode::NotFound, "missing required uint64 field");
        }

        const auto &value = values->front();
        if (value.size() != sizeof(std::uint64_t))
        {
            return Status::error(ErrorCode::ParseError, "uint64 tlv field must be 8 bytes");
        }

        output = readUint64(value);
        return Status::ok();
    }

    Status getRepeatedString(const TlvMap &map, TlvType type, std::vector<std::string> &output)
    {
        output.clear();
        const auto *values = findValues(map, type);
        if (values == nullptr)
        {
            return Status::error(ErrorCode::NotFound, "missing required repeated string field");
        }

        output.reserve(values->size());
        for (const auto &value : *values)
        {
            output.emplace_back(value.begin(), value.end());
        }
        return Status::ok();
    }

    Status getRepeatedUint64(const TlvMap &map, TlvType type, std::vector<std::uint64_t> &output)
    {
        output.clear();
        const auto *values = findValues(map, type);
        if (values == nullptr)
        {
            return Status::error(ErrorCode::NotFound, "missing required repeated uint64 field");
        }

        output.reserve(values->size());
        for (const auto &value : *values)
        {
            if (value.size() != sizeof(std::uint64_t))
            {
                return Status::error(ErrorCode::ParseError, "uint64 tlv field must be 8 bytes");
            }
            output.push_back(readUint64(value));
        }
        return Status::ok();
    }

} // namespace liteim
