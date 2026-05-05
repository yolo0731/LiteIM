#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "liteim/base/Status.hpp"
#include "liteim/protocol/Tlv.hpp"

namespace liteim
{

    inline constexpr std::size_t kTlvHeaderSize = 6;
    inline constexpr std::uint32_t kMaxTlvValueLength = 1024 * 1024;

    using TlvValue = std::vector<std::uint8_t>;
    using TlvValues = std::vector<TlvValue>;
    using TlvMap = std::unordered_map<TlvType, TlvValues>;

    Status appendString(TlvType type, std::string_view value, std::vector<std::uint8_t> &output); // 将一个字符串类型的 TLV 字段追加到 output 里
    Status appendUint64(TlvType type, std::uint64_t value, std::vector<std::uint8_t> &output);    // 将一个 uint64 类型的 TLV 字段追加到 output 里

    Status parseTlvMap(const std::uint8_t *data, std::size_t len, TlvMap &output); // 负责从 body 字节流里循环读取 TLV 字段，检查格式和长度是否合法，然后按 TlvType 把每个 value 保存到 TlvMap 里
    Status parseTlvMap(const std::vector<std::uint8_t> &body, TlvMap &output);     // 直接传 std::vector<std::uint8_t>，不用自己手动传 data() 和 size()

    Status getString(const TlvMap &map, TlvType type, std::string &output); // 从 TlvMap 里取出某个 type 对应的第一个 value，并转成 std::string。
    Status getUint64(const TlvMap &map, TlvType type, std::uint64_t &output);
    Status getRepeatedString(const TlvMap &map, TlvType type, std::vector<std::string> &output); // 从TlvMap里取出某个type对应的所有value，转成std::vector<std::string>
    Status getRepeatedUint64(const TlvMap &map, TlvType type, std::vector<std::uint64_t> &output);

} // namespace liteim
