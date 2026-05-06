# Step 8：实现 SocketUtil

本 Step 继续完善 `liteim_net` 网络模块，但还不进入 `epoll`、Reactor 或连接生命周期。

目标是把 Linux socket 常用系统调用收敛到 `SocketUtil`，让后续 `Acceptor`、`Session`、`TcpServer` 不需要到处手写 `socket()`、`fcntl()`、`setsockopt()` 和 `close()`。

```text
Acceptor / Session
    -> SocketUtil
        -> Linux socket syscalls
```

## 1. 为什么需要 SocketUtil

Linux 网络服务端会反复做几类 fd 操作：

- 创建 TCP socket。
- 把 fd 设置成非阻塞。
- 设置 `SO_REUSEADDR` / `SO_REUSEPORT`。
- 设置 `TCP_NODELAY` / `SO_KEEPALIVE`。
- 查询 socket 上的 pending error。
- 关闭 fd，并避免同一个 fd 变量被重复关闭。

如果这些逻辑分散在 `Acceptor`、`Session`、`TcpServer` 里，后续错误处理会很乱：有的地方忘记设置非阻塞，有的地方直接 `exit()`，有的地方重复 `close()`。所以本 Step 先把这些底层动作集中起来。

## 2. 本 Step 新增文件

```text
include/liteim/net/SocketUtil.hpp
src/net/SocketUtil.cpp
tests/net/socket_util_test.cpp
```

同时更新：

```text
src/net/CMakeLists.txt
tests/CMakeLists.txt
```

`SocketUtil.cpp` 被加入现有 `liteim_net` target，后续网络层代码可以直接复用。

## 3. SocketUtil 的公开接口

```cpp
inline constexpr int kInvalidFd = -1;

Status createNonBlockingSocket(int& fd);
Status setNonBlocking(int fd);
Status setReuseAddr(int fd, bool enabled = true);
Status setReusePort(int fd, bool enabled = true);
Status setTcpNoDelay(int fd, bool enabled = true);
Status setKeepAlive(int fd, bool enabled = true);
Status closeFd(int& fd);
Status getSocketError(int fd, int& error_code);
```

接口含义：

- `kInvalidFd`：统一表示无效 fd，当前值为 `-1`。
- `createNonBlockingSocket()`：创建 `AF_INET` / `SOCK_STREAM` TCP socket，并通过 `SOCK_NONBLOCK` 设置非阻塞，通过 `SOCK_CLOEXEC` 设置 close-on-exec。
- `setNonBlocking()`：对已有 fd 使用 `fcntl(F_GETFL)` 和 `fcntl(F_SETFL)` 设置 `O_NONBLOCK`。
- `setReuseAddr()`：设置 `SO_REUSEADDR`，方便服务重启后快速复用地址。
- `setReusePort()`：设置 `SO_REUSEPORT`，为后续多监听扩展保留统一工具接口。
- `setTcpNoDelay()`：设置 `TCP_NODELAY`，关闭 Nagle 算法，降低小包交互延迟。
- `setKeepAlive()`：设置 `SO_KEEPALIVE`，允许内核 TCP keepalive 机制参与连接探测。
- `closeFd()`：关闭 fd，并把传入的 fd 变量置为 `kInvalidFd`。
- `getSocketError()`：读取 `SO_ERROR`，后续非阻塞连接和异常处理会用到。

所有函数都返回 `Status`，不在工具函数里直接退出进程。

## 4. createNonBlockingSocket 为什么直接带 SOCK_NONBLOCK

本项目的网络 I/O 线程不能被单个连接阻塞。后续 `accept` 得到的连接 fd、客户端连接 fd、监听 fd 都应该是非阻塞 fd。

本 Step 的创建函数直接使用：

```cpp
::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)
```

这样 fd 从创建出来的第一刻就是非阻塞，并且子进程执行新程序时不会继承这个 fd。

`setNonBlocking()` 仍然保留，是因为后续可能遇到已有 fd，例如 `accept()` 返回的 fd 或测试里手动创建的 fd，需要补充设置 `O_NONBLOCK`。

## 5. closeFd 为什么接收 int&

`closeFd(int& fd)` 的关键不是只调用 `close(fd)`，而是关闭后把原变量改成：

```cpp
fd = kInvalidFd;
```

这样同一个 fd 变量第二次传入 `closeFd()` 时，会直接按无效 fd 处理并返回成功，避免重复关闭同一个整数值。

需要注意：

- `closeFd()` 只能保护同一个变量不会重复关闭。
- 如果调用方复制了 fd 整数值，另一个副本仍然可能造成误用。
- 后续连接类会通过 RAII 和明确所有权进一步收紧这个边界。

## 6. getSocketError 的用途

`getSocketError()` 读取的是：

```cpp
getsockopt(fd, SOL_SOCKET, SO_ERROR, ...)
```

它的作用是拿到 socket 当前挂起的错误。后续做非阻塞 `connect()`、写事件处理、连接异常诊断时，常用 `SO_ERROR` 判断连接是否真正成功或失败。

当前 Step 只提供这个薄封装，不实现客户端连接流程。

## 7. 本 Step 不做什么

本 Step 不实现：

- `bind()` / `listen()` / `accept()` 封装。
- `Epoller`
- `Channel`
- `EventLoop`
- `Acceptor`
- `Session`
- `TcpServer`
- 慢客户端高水位回压

这些属于后续 Step。当前只把 socket 常用工具函数准备好。

## 8. 测试清单

新增测试文件：

```text
tests/net/socket_util_test.cpp
```

测试用例：

```cpp
TEST(SocketUtilTest, CreateNonBlockingSocketReturnsNonblockingFd)
TEST(SocketUtilTest, SetNonBlockingMarksPlainSocketNonblocking)
TEST(SocketUtilTest, SocketOptionsCanBeEnabled)
TEST(SocketUtilTest, InvalidFdReturnsError)
TEST(SocketUtilTest, CloseFdInvalidatesDescriptorAndCanBeRepeated)
TEST(SocketUtilTest, GetSocketErrorReturnsCurrentSoError)
```

这些测试覆盖：

- `createNonBlockingSocket()` 创建出的 fd 带 `O_NONBLOCK` 和 `FD_CLOEXEC`。
- 普通 blocking socket 可以通过 `setNonBlocking()` 改成非阻塞。
- `SO_REUSEADDR`、`SO_REUSEPORT`、`TCP_NODELAY`、`SO_KEEPALIVE` 可以设置成功，并能用 `getsockopt()` 读回。
- 负 fd 会返回错误，不会静默成功。
- `closeFd()` 会把 fd 变量改成 `kInvalidFd`，第二次关闭同一变量也安全。
- `getSocketError()` 能读取新 socket 的 `SO_ERROR`，正常情况下是 `0`。

## 9. 验证命令

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

当前 Step 8 预期 CTest 通过 73 个测试，其中 6 个是新增的 `SocketUtilTest`。

## 10. 面试讲法

可以这样讲：

> 我把 Linux socket 常用操作封装成 `SocketUtil`，包括创建非阻塞 TCP socket、设置非阻塞、设置常见 socket option、读取 `SO_ERROR` 和关闭 fd。这个模块不拥有连接生命周期，也不做 epoll 或业务逻辑，只保证后续 `Acceptor`、`Session` 和 `TcpServer` 对 fd 的基础操作一致、可测试、不会在底层工具函数里直接退出进程。

重点边界：

- `SocketUtil` 是系统调用薄封装，不是连接类。
- 非阻塞是网络线程的基本前提。
- fd 关闭后的所有权问题不能只靠整数值解决，后续还要靠 RAII 和连接生命周期管理。

## 11. 面试常见追问

**为什么不用 blocking socket？**

因为 I/O 线程要同时处理多个连接。如果某个 fd 的 `read()`、`write()` 或 `accept()` 阻塞，整个事件循环都会停住。

**`SO_REUSEADDR` 和 `SO_REUSEPORT` 有什么区别？**

`SO_REUSEADDR` 主要解决地址快速复用问题，常见于服务重启后端口仍处于 `TIME_WAIT` 相关状态。`SO_REUSEPORT` 允许多个 socket 绑定同一地址端口，后续可以用于多进程或多监听扩展；当前 Step 只是提供工具函数。

**为什么 `closeFd()` 不能完全防止 fd 误用？**

因为 fd 本质是一个整数。如果调用方复制了这个整数，关闭原变量只能把原变量置为 `-1`，不能自动修改所有副本。后续连接对象需要用 RAII 明确 fd 所有权。

**为什么 `getSocketError()` 重要？**

非阻塞连接和某些写事件里，事件到来并不等于操作一定成功。读取 `SO_ERROR` 可以拿到 socket 上真正的错误状态。

## 12. 提交信息

```text
feat(net): add socket utility functions
```
