#pragma once

#include "liteim/net/SocketUtil.hpp"

namespace liteim
{

    class UniqueFd
    {
    public:
        UniqueFd() noexcept = default;
        explicit UniqueFd(int fd) noexcept;
        ~UniqueFd();

        UniqueFd(const UniqueFd &) = delete;
        UniqueFd &operator=(const UniqueFd &) = delete;

        UniqueFd(UniqueFd &&other) noexcept;
        UniqueFd &operator=(UniqueFd &&other) noexcept;

        int fd() const noexcept;
        explicit operator bool() const noexcept;

        int release() noexcept;                   // 把 fd 交出去，不关闭它
        void reset(int fd = kInvalidFd) noexcept; // 关闭当前 fd，然后接管新的 fd

    private:
        int fd_{kInvalidFd};
    };

} // namespace liteim
