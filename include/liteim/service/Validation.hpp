#pragma once

#include "liteim/base/ErrorCode.hpp"
#include "liteim/base/Status.hpp"
#include "liteim/protocol/MessageLimits.hpp"

#include <cstddef>
#include <string>

namespace liteim {

inline constexpr std::size_t kMaxUsernameBytes = 64;
inline constexpr std::size_t kMaxNicknameBytes = 64;
inline constexpr std::size_t kMaxPasswordBytes = 128;
inline constexpr std::size_t kMaxGroupNameBytes = 128;
inline constexpr std::size_t kMaxClientMessageIdBytes = 64;
inline Status validateMaxBytes(const std::string& value, std::size_t max_bytes,
                               const char* field_name) {
    if (value.size() > max_bytes) {
        return Status::error(ErrorCode::InvalidArgument,
                             std::string(field_name) + " is too long");
    }
    return Status::ok();
}

}  // namespace liteim
