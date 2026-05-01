# Step 5：实现 SocketUtil

本步骤目标：封装 Linux socket 常用函数，为后续 `Acceptor`、`Session` 和 `TcpServer` 做准备。

Step 4 的 `Buffer` 只是内存里的字节容器。Step 5 开始接触真正的 Linux 网络系统调用。

## 1. 这一步要解决什么问题

后续服务端会频繁用到这些操作：

- 创建 socket。
- 设置 fd 非阻塞。
- 设置端口复用。
- 关闭 fd。
- 查询 socket 错误。

如果每个模块里都直接写：

```cpp
socket()
fcntl()
setsockopt()
getsockopt()
close()
```

代码会重复，而且错误处理容易不统一。

所以 Step 5 先封装 `SocketUtil`。

## 2. SocketUtil 的职责边界

`SocketUtil` 只封装底层 Linux socket 工具函数。

它负责：

- 创建非阻塞 TCP socket。
- 把已有 fd 设置为非阻塞。
- 设置 `SO_REUSEADDR`。
- 设置 `SO_REUSEPORT`。
- 关闭 fd。
- 读取 socket pending error。
- 系统调用失败时打印 errno。

它不负责：

- 不 `bind()`。
- 不 `listen()`。
- 不 `accept()`。
- 不注册 epoll。
- 不保存连接状态。
- 不处理协议包。

这些功能会留到后续 Step：

- Step 7 / Step 8 / Step 9：epoll、EventLoop、Channel。
- Step 10：Acceptor。
- Step 11：Session。

## 3. 本步骤新增文件

```text
server/net/SocketUtil.hpp
server/net/SocketUtil.cpp
tests/test_socket_util.cpp
tutorials/step05_socket_util.md
```

修改文件：

```text
server/CMakeLists.txt
tests/CMakeLists.txt
tests/test_main.cpp
docs/architecture.md
docs/interview_notes.md
tutorials/README.md
task_plan.md
findings.md
progress.md
```

## 4. SocketUtil.hpp 讲解

文件：

```text
server/net/SocketUtil.hpp
```

这一层只暴露函数声明，不暴露系统调用细节。

### createNonBlockingSocket()

```cpp
int createNonBlockingSocket();
```

作用：

- 创建一个 IPv4 TCP socket。
- 创建时直接带上非阻塞标志。
- 成功返回 fd，失败返回 -1。

为什么创建时就设置非阻塞：

- LiteIM 服务端后续会基于 epoll Reactor。
- Reactor 模型里不能让某个 fd 的 `read()` / `write()` 阻塞整个事件循环。

### setNonBlocking()

```cpp
bool setNonBlocking(int fd);
```

作用：

- 给一个已经存在的 fd 设置 `O_NONBLOCK`。

为什么还需要它：

- `createNonBlockingSocket()` 只管主动创建的 socket。
- 后续 `accept()` 得到的新连接 fd 也必须是非阻塞的。
- 有些 API 或平台路径可能不会在创建时直接带 `SOCK_NONBLOCK`，所以单独封装更灵活。

成功返回 true，失败返回 false。

失败场景：

- fd 无效。
- `fcntl(F_GETFL)` 失败。
- `fcntl(F_SETFL)` 失败。

### setReuseAddr()

```cpp
bool setReuseAddr(int fd);
```

作用：

- 对 socket 设置 `SO_REUSEADDR`。 // 地址复用

它主要用于服务端监听 socket。开发时服务端反复重启，如果端口还处于某些状态，`SO_REUSEADDR` 能减少 bind 失败的概率。

成功返回 true，失败返回 false。

### setReusePort()

```cpp
bool setReusePort(int fd);
```

作用：

- 对 socket 设置 `SO_REUSEPORT`。 // 端口复用

第一版 LiteIM 不做多进程监听同一端口，但这个选项在 Linux 服务端里很常见，后续可以用于多进程或多实例负载分摊。

成功返回 true，失败返回 false。

### closeFd()

```cpp
void closeFd(int fd);
```

作用：

- 封装 `close()`。
- fd 小于 0 时直接返回。
- `close()` 失败时打印 errno。

为什么封装：

- 后续 `Session`、`Acceptor`、`Epoller` 都会关闭 fd。
- 统一封装后，错误日志更一致。

### getSocketError()

```cpp
int getSocketError(int fd);
```

作用：

- 通过 `getsockopt(SO_ERROR)` 读取 socket pending error。

它常用于处理 fd 的错误事件。后续 `Channel` 或 `Session` 收到错误事件时，可以通过这个函数拿到内核记录的 socket 错误。

成功时返回 socket error，通常新 socket 返回 0。

失败时打印 errno，并返回捕获到的 errno。

## 5. SocketUtil.cpp 讲解

文件：

```text
server/net/SocketUtil.cpp
```

### printSyscallError()

这是 `.cpp` 文件里的私有辅助函数。

作用：

- 捕获当前 `errno`。
- 打印系统调用名称。
- 打印 errno 数值和可读错误信息。
- 返回捕获到的 errno。

为什么要先保存 errno：

- 后续调用 `std::strerror()` 或输出流时理论上可能影响 errno。
- 先保存能保证日志和返回值使用的是系统调用失败时的真实 errno。

### createNonBlockingSocket() 实现思路

核心调用：

```cpp
::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)
```

字段含义：

- `AF_INET`：IPv4。
- `SOCK_STREAM`：TCP。
- `SOCK_NONBLOCK`：创建时设置非阻塞。
- `SOCK_CLOEXEC`：exec 时自动关闭 fd，避免 fd 泄漏到子进程。

如果 `socket()` 失败，函数打印 errno 并返回 -1。

### setNonBlocking() 实现思路

它分两步：

1. `fcntl(fd, F_GETFL, 0)` 读取当前 fd flags。
2. `fcntl(fd, F_SETFL, flags | O_NONBLOCK)` 加上非阻塞标志。

为什么不能直接 `F_SETFL, O_NONBLOCK`：

- 直接设置会覆盖原有 flags。
- 正确做法是保留原 flags，再按位或上 `O_NONBLOCK`。

### setReuseAddr() / setReusePort() 实现思路

这两个函数都调用 `setsockopt()`。

`setReuseAddr()` 设置：

```cpp
SO_REUSEADDR
```

`setReusePort()` 设置：

```cpp
SO_REUSEPORT
```

它们都使用：

```cpp
int option = 1;
```

表示开启这个 socket 选项。

### closeFd() 实现思路

`closeFd()` 对非法 fd 做了保护：

```cpp
if (fd < 0) return;
```

这样调用方在析构或错误清理时，可以安全地调用 `closeFd(-1)`。

有效 fd 会调用 `::close(fd)`。

### getSocketError() 实现思路

核心调用：

```cpp
getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &len)
```

如果成功，返回 `socket_error`。

如果失败，打印并返回 errno。

## 6. 测试讲解

文件：

```text
tests/test_socket_util.cpp
```

这些测试的目的，是确认 `SocketUtil` 真的封装了正确的 Linux socket 行为，而不是只检查函数能不能调用。

测试分四类：

1. 正常创建和设置：验证 socket 能创建，且非阻塞标志正确。
2. socket 选项：验证 `SO_REUSEADDR` 和 `SO_REUSEPORT` 设置后能通过 `getsockopt()` 读回来。
3. 错误读取和关闭：验证 `getSocketError()` 和 `closeFd()` 的基本行为。
4. 失败路径：对无效 fd 调用工具函数，确认失败时返回 false 或非 0。

### createNonBlockingSocket 测试

测试创建出来的 fd：

- 必须是合法 fd。
- 必须带 `O_NONBLOCK`。

这证明后续 listen socket 默认不会阻塞事件循环。

### setNonBlocking 测试

测试先创建一个阻塞 socket，然后调用 `setNonBlocking()`。

通过 `fcntl(F_GETFL)` 检查：

- 调用前没有 `O_NONBLOCK`。
- 调用后有 `O_NONBLOCK`。

这证明后续 `accept()` 得到的新连接 fd 可以被正确改成非阻塞。

### setReuseAddr / setReusePort 测试

测试调用设置函数后，再用 `getsockopt()` 读回选项值。

这证明：

- `setsockopt()` 确实成功。
- 不是只返回 true 但没有真正设置。

### getSocketError 测试

对一个新创建的 socket 调用 `getSocketError()`。

预期结果是 0，表示没有 pending error。

### closeFd 测试

测试先创建 fd，再调用 `closeFd()`，然后用 `fcntl(F_GETFL)` 检查 fd 已不可用。

预期 errno 是 `EBADF`。

这证明 `closeFd()` 真正关闭了 fd。

### invalid fd 测试

对 `-1` 调用：

- `setNonBlocking()`
- `setReuseAddr()`
- `setReusePort()`
- `getSocketError()`
- `closeFd()`

预期：

- 前三个返回 false。
- `getSocketError()` 返回非 0。
- `closeFd(-1)` 安全 no-op。

注意：这个测试会故意触发几行 errno 日志，例如 `errno=9 (Bad file descriptor)`。这是测试失败路径的预期输出，不代表测试失败。

## 7. 编译和测试

在 `LiteIM/` 根目录执行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
./build/server/liteim_server
```

预期测试包含：

```text
[PASS] socket util create nonblocking socket
[PASS] socket util set nonblocking
[PASS] socket util set reuse addr
[PASS] socket util set reuse port
[PASS] socket util get socket error
[PASS] socket util close fd
[PASS] socket util invalid fd operations fail
```

如果这些测试通过，说明：

- `SocketUtil` 能创建非阻塞 TCP socket。
- 能正确设置非阻塞和端口复用选项。
- 能正确读取 socket error。
- 能安全关闭 fd。
- 无效 fd 的失败路径有返回值和 errno 日志。

## 8. 面试时怎么讲

可以这样说：

> 我把 Linux socket 常用操作封装成了 `SocketUtil`，包括创建非阻塞 TCP socket、设置 `O_NONBLOCK`、设置 `SO_REUSEADDR` / `SO_REUSEPORT`、关闭 fd 和读取 `SO_ERROR`。后续 `Acceptor` 和 `Session` 会复用这些函数。这样底层系统调用和错误处理不会散落在业务代码里，也能保证所有进入 epoll 的 fd 都是非阻塞的。

讲解思路：

1. 先讲为什么要封装：socket 系统调用会在多个模块重复出现，统一封装能减少重复和错误处理分散。
2. 再讲为什么必须非阻塞：epoll Reactor 不能让某个 fd 的 read/write 阻塞整个事件循环。
3. 再讲端口复用：`SO_REUSEADDR` 用于服务端重启体验，`SO_REUSEPORT` 为后续多进程扩展预留。
4. 最后讲错误处理：所有系统调用失败都打印 errno，便于定位 Linux API 调用问题。

## 9. 面试中容易被问到的问题

**Q1：为什么 epoll 服务端一般要求 fd 是非阻塞的？**

因为 epoll 只是通知 fd 可能可读或可写。如果 fd 是阻塞的，真正调用 `read()` / `write()` 时仍可能卡住当前线程，导致整个事件循环无法处理其他连接。

**Q2：`SOCK_NONBLOCK` 和 `fcntl(O_NONBLOCK)` 有什么区别？**

`SOCK_NONBLOCK` 是创建 socket 时直接设置非阻塞；`fcntl(O_NONBLOCK)` 可以给已经存在的 fd 设置非阻塞。后续 `accept()` 得到的新连接 fd 就需要类似 `setNonBlocking()` 的处理。

**Q3：为什么要设置 `SOCK_CLOEXEC`？**

它能避免进程执行 `exec` 后 fd 泄漏到子进程。虽然 LiteIM 第一版不 fork 子进程，但这是 Linux 服务端常见的稳妥写法。

**Q4：`SO_REUSEADDR` 和 `SO_REUSEPORT` 有什么区别？**

`SO_REUSEADDR` 常用于服务端重启时复用地址；`SO_REUSEPORT` 允许多个 socket 绑定同一个地址端口，常用于多进程负载分摊。两者不是同一个语义。

**Q5：`getSocketError()` 为什么要用 `SO_ERROR`？**

当 fd 出现错误事件时，内核会记录 socket error。通过 `getsockopt(SO_ERROR)` 可以取出具体错误原因。

**Q6：为什么 `closeFd(-1)` 不报错？**

很多资源管理类会用 `-1` 表示“当前没有有效 fd”。析构或错误清理时允许安全调用 `closeFd(-1)`，代码会更简单。

**Q7：为什么不在 `SocketUtil` 里直接实现 bind/listen/accept？**

因为这一步只封装底层工具函数。`bind/listen/accept` 涉及监听器生命周期和回调，会在后续 `Acceptor` 中实现。

## 10. 下一步预告

Step 6 会定义：

```text
server/net/Epoller.hpp
server/net/Channel.hpp
server/net/EventLoop.hpp
```

Step 6 只定义 Reactor 核心接口，先不实现复杂逻辑，目的是提前理清 `Epoller`、`Channel`、`EventLoop` 之间的依赖关系。
