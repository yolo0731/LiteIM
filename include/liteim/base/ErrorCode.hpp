#pragma once

#include <string_view>

namespace liteim {

enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    IoError,
    ParseError,
    ConfigError,
    InternalError,
};

std::string_view toString(ErrorCode code);

}  // namespace liteim
