#pragma once

namespace liteim::net {

int createNonBlockingSocket();

bool setNonBlocking(int fd);

bool setReuseAddr(int fd);

bool setReusePort(int fd);

void closeFd(int fd);

int getSocketError(int fd);

}  // namespace liteim::net

