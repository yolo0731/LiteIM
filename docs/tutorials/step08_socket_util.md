# Step 8：实现 SocketUtil

## 0. 本 Step 结论

- 目标：本 Step 继续完善 liteim_net 网络模块，但还不进入 epoll、Reactor 或连接生命周期。
- 前置依赖：依赖 Step 0-7 已建立的工程、协议或运行时基础。
- 主要交付：`实现 SocketUtil` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不实现 `bind()` / `listen()` / `accept()` 封装。

## 1. 为什么需要这个 Step

本 Step 继续完善 `liteim_net` 网络模块，但还不进入 `epoll`、Reactor 或连接生命周期。

目标是把 Linux socket 常用系统调用收敛到 `SocketUtil`，让后续 `Acceptor`、`Session`、`TcpServer` 不需要到处手写 `socket()`、`fcntl()`、`setsockopt()` 和 `close()`。

```text
Acceptor / Session
    -> SocketUtil
        -> Linux socket syscalls
```

### 为什么需要 SocketUtil

Linux 网络服务端会反复做几类 fd 操作：

- 创建 TCP socket。
- 把 fd 设置成非阻塞。
- 设置 `SO_REUSEADDR` / `SO_REUSEPORT`。
- 设置 `TCP_NODELAY` / `SO_KEEPALIVE`。
- 查询 socket 上的 pending error。
- 关闭 fd，并避免同一个 fd 变量被重复关闭。

如果这些逻辑分散在 `Acceptor`、`Session`、`TcpServer` 里，后续错误处理会很乱：有的地方忘记设置非阻塞，有的地方直接 `exit()`，有的地方重复 `close()`。所以本 Step 先把这些底层动作集中起来。

### createNonBlockingSocket 为什么直接带 SOCK_NONBLOCK

本项目的网络 I/O 线程不能被单个连接阻塞。后续 `accept` 得到的连接 fd、客户端连接 fd、监听 fd 都应该是非阻塞 fd。

本 Step 的创建函数直接使用：

```cpp
::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)
```

这样 fd 从创建出来的第一刻就是非阻塞，并且子进程执行新程序时不会继承这个 fd。

`setNonBlocking()` 仍然保留，是因为后续可能遇到已有 fd，例如 `accept()` 返回的 fd 或测试里手动创建的 fd，需要补充设置 `O_NONBLOCK`。

### closeFd 为什么接收 int&

`closeFd(int& fd)` 的关键不是只调用 `close(fd)`，而是关闭后把原变量改成：

```cpp
fd = kInvalidFd;
```

这样同一个 fd 变量第二次传入 `closeFd()` 时，会直接按无效 fd 处理并返回成功，避免重复关闭同一个整数值。

需要注意：

- `closeFd()` 只能保护同一个变量不会重复关闭。
- 如果调用方复制了 fd 整数值，另一个副本仍然可能造成误用。
- 后续连接类会通过 RAII 和明确所有权进一步收紧这个边界。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `实现 SocketUtil` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不实现 `bind()` / `listen()` / `accept()` 封装。
- 不实现 `Epoller`
- 不实现 `Channel`
- 不实现 `EventLoop`
- 不实现 `Acceptor`
- 不实现 `Session`
- 不实现 `TcpServer`
- 不实现 慢客户端高水位回压

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/net/SocketUtil.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/SocketUtil.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/socket_util_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `SocketUtil.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

`SocketUtil.hpp` 是 Linux socket 系统调用的薄封装。

常量：

- `kInvalidFd = -1` 统一表示无效 fd。

创建和模式：

- `createNonBlockingSocket(int& fd)` 创建 `AF_INET` / `SOCK_STREAM` socket，同时带 `SOCK_NONBLOCK` 和 `SOCK_CLOEXEC`。成功后写入 fd；失败时把 fd 设为 `kInvalidFd` 并返回 `IoError`。
- `setNonBlocking(fd)` 对已有 fd 设置 `O_NONBLOCK`。fd 无效返回 `InvalidArgument`；`fcntl` 失败返回 `IoError`。

socket options：

- `setReuseAddr(fd, enabled)` 设置 `SO_REUSEADDR`。
- `setReusePort(fd, enabled)` 设置 `SO_REUSEPORT`，平台不支持时返回 `InternalError`。
- `setTcpNoDelay(fd, enabled)` 设置 `TCP_NODELAY`。
- `setKeepAlive(fd, enabled)` 设置 `SO_KEEPALIVE`。
- 这些函数都通过统一内部 helper 检查 fd 并调用 `setsockopt()`。

关闭和错误查询：

- `closeFd(int& fd)` 关闭 fd，并先把变量置为 `kInvalidFd`，降低重复关闭风险。无效 fd 视为成功。
- `getSocketError(fd, error_code)` 读取 `SO_ERROR`，用于非阻塞连接完成判断和异常诊断。

关键 private helper 在 `.cpp` 中：

- `invalidFdStatus()` 统一生成无效 fd 错误。
- `errnoStatus(action, errno)` 统一把系统调用失败转成 `Status`。
- `setSocketOption()` 复用 `setsockopt()` 参数和错误处理。

线程和所有权边界：

- `SocketUtil` 函数不拥有 fd，只操作传入的 fd 值或 fd 引用。
- fd 的长期所有权后续交给 `UniqueFd`、`Acceptor`、`Session`。
- `closeFd(int&)` 只能保护同一个变量，不能保护被复制出去的裸 fd。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

`SocketUtil` 是 Linux socket 系统调用的小工具层：`Acceptor` 用它创建和配置 listen fd，`Session` 用它确保 accepted fd 非阻塞，`UniqueFd` 用它关闭 fd，后续非阻塞 connect 或错误诊断用 `getSocketError()` 判断真实连接结果。

### 2. 上下层调用连接

```text
Acceptor
    -> createNonBlockingSocket()
    -> setReuseAddr() / setReusePort()
    -> bind() / listen()
    -> Channel(listen fd)

Session
    -> setNonBlocking(accepted fd)
    -> read/write nonblocking socket

UniqueFd
    -> closeFd(fd)
    -> fd = kInvalidFd
```

上游是持有 fd 的网络对象，下游是 Linux `socket`、`fcntl`、`setsockopt`、`getsockopt` 和 `close`。

### 3. 整体运行链路

1. `Acceptor` 构造时调用 [createNonBlockingSocket()](../src/net/SocketUtil.cpp) 得到 listen fd。
2. `Acceptor` 调用 [setReuseAddr()](../src/net/SocketUtil.cpp) 和 [setReusePort()](../src/net/SocketUtil.cpp)。
3. `Acceptor` 自己完成 bind/listen 并把 fd 注册给 Channel。
4. `accept4()` 返回 accepted fd 后，`Session` 构造里调用 [setNonBlocking()](../src/net/SocketUtil.cpp) 做兜底。
5. 资源释放时，`UniqueFd::reset()` 调用 [closeFd()](../src/net/SocketUtil.cpp)。
6. 如果后续实现非阻塞 connect，fd 可写后调用 [getSocketError()](../src/net/SocketUtil.cpp) 判断连接是否真的成功。

### 4. 自身内部运行流程

整体可以看成 4 步：创建 fd、设置 option、关闭 fd、查询 pending error。

核心数据职责：

- `int fd` 是内核资源句柄。
- `kInvalidFd` 是无效 fd 哨兵。
- `Status` 把系统调用错误变成上层可处理结果。

核心函数流程：

- `createNonBlockingSocket()`：直接用 `SOCK_NONBLOCK | SOCK_CLOEXEC` 创建 socket，减少创建后再设置的竞态窗口。
- `setNonBlocking()`：先 `F_GETFL`，如果已经非阻塞直接成功，否则 `F_SETFL` 加 `O_NONBLOCK`。
- `setReuseAddr()` / `setReusePort()` / `setTcpNoDelay()` / `setKeepAlive()`：统一走内部 `setSocketOption()`。
- `closeFd(int&)`：先保存当前 fd，再把引用置为 `kInvalidFd`，最后调用 `close()`。
- `getSocketError()`：通过 `getsockopt(SO_ERROR)` 读取内核保存的 socket 错误。

`closeFd(fd)` 可以理解成“先断开所有权，再关闭旧 fd”：

```text
调用方传入 fd 引用
    ↓
把外部 fd 值先改成 kInvalidFd
    ↓
对旧 fd 调用 close()
    ↓
把 close 结果包装成 Status
```

这种顺序的价值是避免重复关闭：哪怕 `close()` 期间出现错误，调用方手里的 fd 也已经失效，不会在后续清理路径里再次关闭同一个数字。

### 5. 该项目代码在实际应用中的具体数据例子

服务器监听 `0.0.0.0:9000` 时，`createNonBlockingSocket()` 创建 listen fd，`setReuseAddr()` 方便本地重启，`bindAndListen()` 进入监听状态。Alice 客户端连上后，后续 accepted fd 会保持非阻塞，交给 `Session` 管理；如果某次 `write()` 返回错误，`getSocketError(fd)` 可以把内核错误转成日志中的具体原因。

## 6. 关键实现点

### SocketUtil 的公开接口

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

### getSocketError 的用途

`getSocketError()` 读取的是：

```cpp
getsockopt(fd, SOL_SOCKET, SO_ERROR, ...)
```

它的作用是拿到 socket 当前挂起的错误。后续做非阻塞 `connect()`、写事件处理、连接异常诊断时，常用 `SO_ERROR` 判断连接是否真正成功或失败。

当前 Step 只提供这个薄封装，不实现客户端连接流程。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `实现 SocketUtil` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

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

## 8. 验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 9. 面试表达

### 一句话

我把 Linux socket 常用操作封装成 SocketUtil，包括创建非阻塞 TCP socket、设置非阻塞、设置常见 socket option、读取 SO_ERROR 和关闭 fd。

### 展开说

可以这样讲：

> 我把 Linux socket 常用操作封装成 `SocketUtil`，包括创建非阻塞 TCP socket、设置非阻塞、设置常见 socket option、读取 `SO_ERROR` 和关闭 fd。这个模块不拥有连接生命周期，也不做 epoll 或业务逻辑，只保证后续 `Acceptor`、`Session` 和 `TcpServer` 对 fd 的基础操作一致、可测试、不会在底层工具函数里直接退出进程。

重点边界：

- `SocketUtil` 是系统调用薄封装，不是连接类。
- 非阻塞是网络线程的基本前提。
- fd 关闭后的所有权问题不能只靠整数值解决，后续还要靠 RAII 和连接生命周期管理。

### 容易被追问

- 为什么不用 blocking socket？
- `SO_REUSEADDR` 和 `SO_REUSEPORT` 有什么区别？
- 为什么 `closeFd()` 不能完全防止 fd 误用？
- 为什么 `getSocketError()` 重要？

## 10. 面试常见追问

### 为什么不用 blocking socket？

因为 I/O 线程要同时处理多个连接。如果某个 fd 的 `read()`、`write()` 或 `accept()` 阻塞，整个事件循环都会停住。

### `SO_REUSEADDR` 和 `SO_REUSEPORT` 有什么区别？

`SO_REUSEADDR` 主要解决地址快速复用问题，常见于服务重启后端口仍处于 `TIME_WAIT` 相关状态。`SO_REUSEPORT` 允许多个 socket 绑定同一地址端口，后续可以用于多进程或多监听扩展；当前 Step 只是提供工具函数。

### 为什么 `closeFd()` 不能完全防止 fd 误用？

因为 fd 本质是一个整数。如果调用方复制了这个整数，关闭原变量只能把原变量置为 `-1`，不能自动修改所有副本。后续连接对象需要用 RAII 明确 fd 所有权。

### 为什么 `getSocketError()` 重要？

非阻塞连接和某些写事件里，事件到来并不等于操作一定成功。读取 `SO_ERROR` 可以拿到 socket 上真正的错误状态。
