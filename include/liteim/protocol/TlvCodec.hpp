#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "liteim/base/Status.hpp"
#include "liteim/protocol/Tlv.hpp"

namespace liteim {

inline constexpr std::size_t kTlvHeaderSize = 6;
inline constexpr std::uint32_t kMaxTlvValueLength = 1024 * 1024;

using TlvValue = std::vector<std::uint8_t>;
using TlvValues = std::vector<TlvValue>;
using TlvMap = std::unordered_map<TlvType, TlvValues>;

Status appendString(TlvType type, std::string_view value, std::vector<std::uint8_t>& output);
Status appendUint64(TlvType type, std::uint64_t value, std::vector<std::uint8_t>& output);

Status parseTlvMap(const std::uint8_t* data, std::size_t len, TlvMap& output);
Status parseTlvMap(const std::vector<std::uint8_t>& body, TlvMap& output);

Status getString(const TlvMap& map, TlvType type, std::string& output);
Status getUint64(const TlvMap& map, TlvType type, std::uint64_t& output);
Status getRepeatedString(const TlvMap& map, TlvType type, std::vector<std::string>& output);
Status getRepeatedUint64(const TlvMap& map, TlvType type, std::vector<std::uint64_t>& output);

}  // namespace liteim
