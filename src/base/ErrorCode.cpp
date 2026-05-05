#include "liteim/base/ErrorCode.hpp"

namespace liteim {

std::string_view toString(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:
            return "Ok";
        case ErrorCode::InvalidArgument:
            return "InvalidArgument";
        case ErrorCode::NotFound:
            return "NotFound";
        case ErrorCode::IoError:
            return "IoError";
        case ErrorCode::ParseError:
            return "ParseError";
        case ErrorCode::ConfigError:
            return "ConfigError";
        case ErrorCode::InternalError:
            return "InternalError";
    }

    return "Unknown";
}

}  // namespace liteim
