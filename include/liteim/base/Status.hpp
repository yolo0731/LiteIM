#pragma once

#include <string>
#include <utility>

#include "liteim/base/ErrorCode.hpp"

namespace liteim {

class Status {
public:
    Status() = default;

    static Status ok();
    static Status error(ErrorCode code, std::string message);

    bool isOk() const noexcept;
    ErrorCode code() const noexcept;
    const std::string& message() const noexcept;

private:
    Status(ErrorCode code, std::string message);

    ErrorCode code_{ErrorCode::Ok};
    std::string message_;
};

}  // namespace liteim
