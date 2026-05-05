#include "liteim/base/Status.hpp"

namespace liteim {

Status::Status(ErrorCode code, std::string message)
    : code_(code),
      message_(std::move(message)) {}

Status Status::ok() {
    return Status{};
}

Status Status::error(ErrorCode code, std::string message) {
    return Status{code, std::move(message)};
}

bool Status::isOk() const noexcept {
    return code_ == ErrorCode::Ok;
}

ErrorCode Status::code() const noexcept {
    return code_;
}

const std::string& Status::message() const noexcept {
    return message_;
}

}  // namespace liteim
