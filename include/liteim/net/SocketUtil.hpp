#pragma once

#include "liteim/base/Status.hpp"

namespace liteim {

inline constexpr int kInvalidFd = -1;

Status createNonBlockingSocket(int& fd);
Status setNonBlocking(int fd);
Status setReuseAddr(int fd, bool enabled = true);
Status setReusePort(int fd, bool enabled = true);
Status setTcpNoDelay(int fd, bool enabled = true);
Status setKeepAlive(int fd, bool enabled = true);
Status closeFd(int& fd);
Status getSocketError(int fd, int& error_code);

}  // namespace liteim
