#include "liteim/net/UniqueFd.hpp"

namespace liteim {

UniqueFd::UniqueFd(int fd) noexcept : fd_(fd) {}

UniqueFd::~UniqueFd() {
    reset();
}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

int UniqueFd::fd() const noexcept {
    return fd_;
}

UniqueFd::operator bool() const noexcept {
    return fd_ >= 0;
}

int UniqueFd::release() noexcept {
    const int fd = fd_;
    fd_ = kInvalidFd;
    return fd;
}

void UniqueFd::reset(int fd) noexcept {
    if (fd_ == fd) {
        return;
    }

    const auto status = closeFd(fd_);
    (void)status;
    fd_ = fd;
}

}  // namespace liteim
