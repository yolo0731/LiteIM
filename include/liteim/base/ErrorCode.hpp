#pragma once

namespace liteim {

enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    IoError,
    ParseError,
    ConfigError,
    InternalError,
    ResourceExhausted,
};

const char* toString(ErrorCode code) noexcept;

}  // namespace liteim
