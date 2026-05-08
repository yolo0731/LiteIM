# Step 13: Acceptor

本 Step 实现 `Acceptor`，也就是服务端的监听器。

## 概念

`Acceptor` 只负责一件事：

```text
listen socket 可读
    -> accept 新连接
    -> 把已连接 fd 交给上层 callback
```

它不创建 `Session`，不解析协议，不处理登录，也不访问 MySQL / Redis。后续 `TcpServer` 会接收 `Acceptor` 交出的 fd，再决定把连接分配给哪个 I/O `EventLoop`。

## 这一步新增了什么

新增头文件：

```text
include/liteim/net/Acceptor.hpp
include/liteim/net/UniqueFd.hpp
```

新增实现：

```text
src/net/Acceptor.cpp
src/net/UniqueFd.cpp
```

新增测试：

```text
tests/net/acceptor_header_test.cpp
tests/net/acceptor_test.cpp
tests/net/unique_fd_test.cpp
```

这些文件的职责分工是：

- `Acceptor.hpp` / `Acceptor.cpp`：实现监听器。它创建 listen socket，注册 listen `Channel`，在 listen fd 可读时循环 `accept4()`，然后把新连接 fd 交给上层 callback。
- `UniqueFd.hpp` / `UniqueFd.cpp`：实现轻量 RAII fd owner。它不代表 socket 业务语义，只表达“谁负责关闭这个 fd”。
- `acceptor_header_test.cpp`：验证 `Acceptor.hpp` 可以独立包含，避免头文件依赖不完整。
- `acceptor_test.cpp`：验证监听、连接接入、多连接 accept、关闭监听、跨线程关闭和 callback 异常路径。
- `unique_fd_test.cpp`：验证 fd 析构关闭、`release()` 不关闭、move 转移所有权、`reset()` 关闭旧 fd。

## Acceptor 的职责

构造 `Acceptor` 时会完成：

```text
createNonBlockingSocket()
setReuseAddr()
setReusePort()
bind()
listen()
open idle fd
Channel(listen_fd)
enableReading()
```

这里的 `enableReading()` 会通过 `Channel::update()` 注册到所属 `EventLoop`，所以 `Acceptor` 必须在它所属的 loop 线程里创建。

本次 review hardening 后，`Acceptor::close()` 可以从非 loop 线程发起，但真正的 `removeChannel()` 和 listen fd 关闭仍然回到所属 loop 线程执行。这样 `Epoller` 的 `fd -> Channel*` 映射不会留下旧 fd 对应的旧 `Channel*`。

`UniqueFd` 是一个很小的 RAII 工具：对象析构时关闭自己持有的 fd，move 表示把所有权交给另一个 owner。`Acceptor` 用它保护 listen fd 和 accepted fd，尤其是 callback 抛异常时，accepted fd 不会泄漏。

Step 13 hardening round 2 后，`Acceptor` 还会额外持有一个指向 `/dev/null` 的 idle fd。它平时什么也不做，只在进程 fd 用尽时临时释放一个 fd 槽位，用来 accept 并立即关闭一个 pending connection，让客户端明确感知连接被拒绝，同时避免 listen fd 在 LT 模式下因 `EMFILE` / `ENFILE` 反复唤醒导致 busy loop。

## 为什么 Step 13 需要 UniqueFd

fd 在 Linux 里只是一个整数。裸 `int fd` 本身看不出谁拥有它，也看不出谁应该关闭它。Step 13 开始真正产生网络 fd，所以必须把所有权边界说清楚：

```text
listen fd
    -> Acceptor 长期拥有
    -> Acceptor::close() / 析构时关闭

accepted fd
    -> accept4() 刚返回时由 Acceptor 临时拥有
    -> 通过 NewConnectionCallback(UniqueFd, peer) 移动交给上层 TcpServer / Session
    -> callback 抛异常或没有 callback 时由 UniqueFd 自动关闭
```

`UniqueFd` 的接口正好对应这几个动作：

- 析构：关闭当前持有的 fd。
- move 构造 / move 赋值：把 fd 所有权转给另一个 `UniqueFd`，避免两个对象同时关闭同一个 fd。
- `release()`：少数必须交出裸 fd 的底层场景使用；调用后当前 `UniqueFd` 变成空 owner。
- `reset()`：先关闭旧 fd，再接管新 fd。

所以 `UniqueFd.cpp` 是 Step 13 的必要新增文件，不是额外业务功能。它是 `Acceptor` 的资源安全底座，专门解决异常路径和所有权转移问题。

## 为什么 listen fd 要非阻塞

因为服务端不能在 I/O 线程里被单个系统调用卡住。

listen fd 可读只表示“现在至少可能有连接可以 accept”，不等于无限多连接都已经准备好了。所以处理 read callback 时要循环：

```text
while true:
    fd = accept4()
    if fd >= 0:
        callback(UniqueFd(fd), peer_address)
        continue
    if errno == EINTR:
        continue
    if errno == EAGAIN:
        break
```

`EAGAIN` / `EWOULDBLOCK` 不是错误，它表示当前 pending connection 已经取完。

其他错误要区分处理：

- `ECONNABORTED`：连接在 accept 前已经中止，记录日志后继续 accept 后续 pending connection。
- `EMFILE` / `ENFILE`：进程或系统 fd 用尽，走 idle fd 保护。先关闭 idle fd，accept 一个连接并立即关闭，再重新打开 `/dev/null` 作为新的 idle fd。
- 未知错误：记录 warn 日志并退出本轮 accept，避免静默吞掉真实系统调用问题。

## 为什么使用 accept4

`accept4()` 可以在接收连接的同时设置：

```text
SOCK_NONBLOCK
SOCK_CLOEXEC
```

这样新连接 fd 从出生开始就是非阻塞且 close-on-exec 的，不需要先 accept 再 fcntl 补设置，减少短暂的状态窗口。

## NewConnectionCallback

`Acceptor` 的 callback 类型是：

```cpp
std::function<void(UniqueFd, const sockaddr_in&)>
```

含义是：

```text
UniqueFd         accepted fd 的独占所有权
sockaddr_in      对端地址
```

如果没有设置 callback，`Acceptor` 会立即关闭 accepted fd，避免文件描述符泄漏。

如果设置了 callback，`Acceptor` 会把局部 `UniqueFd` move 给 callback。callback 抛异常时，`UniqueFd` 会在栈展开过程中自动关闭 accepted fd，避免“上层还没接管所有权就异常退出”造成泄漏。

## close 的边界

`Acceptor::close()` 做两件事：

```text
从 EventLoop 移除 listen Channel
关闭 listen fd
```

关闭后 `listenFd()` 返回 `kInvalidFd`，`listening()` 返回 false，新客户端不能再连接到这个监听 socket。

跨线程调用 `close()` 时，如果 `EventLoop` 仍在运行，`Acceptor` 会通过 `EventLoop::queueInLoop()` 把 `closeInLoop()` 投递给 loop 线程，并等待清理完成。这个设计保留 one-loop-per-thread 边界：`Epoller::removeChannel()` 仍然只在 owner loop 线程执行。

如果 `EventLoop::loop()` 已经退出，`isStopped()` 会返回 true，此时再投递任务没有线程会执行，`Acceptor::close()` 会进入 fallback 路径，在调用线程释放 `Channel`、listen fd 和 idle fd。这个路径只用于 loop 已停止后的确定性清理。

Step 13 hardening round 3 明确了一个容易混淆的状态：`EventLoop` 刚构造但还没进入 `loop()` 时，不算 stopped。这个时候跨线程调用 `Acceptor::close()` 仍然要通过 `queueInLoop()` 等 owner loop 线程执行 `removeChannel()`；否则调用线程直接释放 listen `Channel` 后，`Epoller` 内部的 `fd -> Channel*` 映射会残留，后续同一个 fd 号被复用时会触发 `fd already belongs to a different channel`。

`EMFILE` / `ENFILE` 处理函数保留 `noexcept`，因为 fd 用尽路径不应再向外抛异常。但这些函数内部会写 warn 日志，所以日志调用被包在 no-throw wrapper 里：即使 `spdlog` sink 或格式化抛异常，也只丢弃这次日志，不让服务进程在异常资源状态下 `std::terminate()`。

## 本 Step 不做什么

本 Step 不实现：

- `Session`
- `TcpServer`
- 多 Reactor 线程池
- 登录 / 聊天业务
- MySQL / Redis
- 慢客户端回压

这些会在后续 Step 继续补上。

## 测试

新增测试覆盖：

- `ReactorInterfaceTest.AcceptorHeaderIsSelfContained`
- `AcceptorTest.ServerCanListenOnEphemeralPort`
- `AcceptorTest.ClientConnectionTriggersNewConnectionCallback`
- `AcceptorTest.MultiplePendingConnectionsAreAccepted`
- `AcceptorTest.ClosedListenSocketRejectsNewConnections`
- `AcceptorTest.CloseFromOtherThreadRemovesChannelBeforeClosingFd`
- `AcceptorTest.AcceptedFdIsClosedWhenCallbackThrowsBeforeTakingOwnership`
- `AcceptorTest.CloseFromOtherThreadAfterLoopStopsDoesNotBlock`
- `AcceptorTest.CloseFromOtherThreadBeforeLoopStartsStillRemovesChannel`
- `AcceptorTest.FdExhaustionRejectsPendingConnectionWithoutLaterCallback`
- `UniqueFdTest.DestructorClosesOwnedFd`
- `UniqueFdTest.ReleaseReturnsFdWithoutClosing`
- `UniqueFdTest.MoveTransfersOwnership`
- `UniqueFdTest.ResetClosesPreviousFd`
- `ChannelTest.TiedExpiredOwnerSkipsCallbacks`
- `ChannelTest.TiedOwnerStaysAliveDuringCallback`

运行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build -R Acceptor --output-on-failure
ctest --test-dir build --output-on-failure
```

## 面试讲法

可以这样讲：

> `Acceptor` 是主 Reactor 上的监听器。它持有非阻塞 listen fd 和对应的 `Channel`，listen fd 可读时循环 `accept4()` 到 `EAGAIN`。每个新连接 fd 都带 `SOCK_NONBLOCK` 和 `SOCK_CLOEXEC`，然后通过 callback 交给上层 `TcpServer`。它不负责连接生命周期和业务处理，只负责接收连接这一层边界。

review hardening 后还可以补充：

> listen fd、accepted fd 和 idle fd 都用 RAII 思路兜底。`Acceptor::close()` 即使从其他线程发起，也会把真正的 epoll 删除和 fd 关闭投递回 owner loop；如果 loop 尚未启动但还会启动，close 会等待 owner loop 执行清理；只有 loop 已经显式退出后，才走 fallback 直接释放资源，避免永久等待。fd 用尽时用 idle fd 套路拒绝一个 pending connection，避免 LT 模式下 `EMFILE` 触发 busy loop，且该异常路径中的日志不会从 `noexcept` helper 里继续抛出。`Channel::tie()` 则为下一步 `Session` 的 `shared_ptr` / `weak_ptr` 生命周期模型预留了基础。
