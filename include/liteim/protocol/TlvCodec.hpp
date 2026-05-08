#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "liteim/base/Status.hpp"
#include "liteim/base/Types.hpp"
#include "liteim/protocol/Tlv.hpp"

namespace liteim {

inline constexpr std::size_t kTlvHeaderSize = 6;
inline constexpr std::uint32_t kMaxTlvValueLength = 1024 * 1024;

using TlvValue = Bytes;
using TlvValues = std::vector<TlvValue>;
using TlvMap = std::unordered_map<TlvType, TlvValues>;

Status appendString(TlvType type, const std::string& value, Bytes& output);
Status appendUint64(TlvType type, std::uint64_t value, Bytes& output);

Status parseTlvMap(const Byte* data, std::size_t len, TlvMap& output);
Status parseTlvMap(const Bytes& body, TlvMap& output);

Status getString(const TlvMap& map, TlvType type, std::string& output);
Status getUint64(const TlvMap& map, TlvType type, std::uint64_t& output);
Status getRepeatedString(const TlvMap& map, TlvType type, std::vector<std::string>& output);
Status getRepeatedUint64(const TlvMap& map, TlvType type, std::vector<std::uint64_t>& output);

} // namespace liteim
