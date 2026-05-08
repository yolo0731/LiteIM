#pragma once

#include "liteim/net/SocketUtil.hpp"

namespace liteim {

class UniqueFd {
  public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept;
    ~UniqueFd();

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept;
    UniqueFd& operator=(UniqueFd&& other) noexcept;

    int fd() const noexcept;
    explicit operator bool() const noexcept;

    int release() noexcept;
    void reset(int fd = kInvalidFd) noexcept;

  private:
    int fd_{kInvalidFd};
};

}  // namespace liteim
