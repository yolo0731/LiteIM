#pragma once

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

const char* toString(ErrorCode code) noexcept;

} // namespace liteim
