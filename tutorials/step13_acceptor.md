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
```

新增实现：

```text
src/net/Acceptor.cpp
```

新增测试：

```text
tests/net/acceptor_header_test.cpp
tests/net/acceptor_test.cpp
```

## Acceptor 的职责

构造 `Acceptor` 时会完成：

```text
createNonBlockingSocket()
setReuseAddr()
setReusePort()
bind()
listen()
Channel(listen_fd)
enableReading()
```

这里的 `enableReading()` 会通过 `Channel::update()` 注册到所属 `EventLoop`，所以 `Acceptor` 必须在它所属的 loop 线程里创建和关闭。

## 为什么 listen fd 要非阻塞

因为服务端不能在 I/O 线程里被单个系统调用卡住。

listen fd 可读只表示“现在至少可能有连接可以 accept”，不等于无限多连接都已经准备好了。所以处理 read callback 时要循环：

```text
while true:
    fd = accept4()
    if fd >= 0:
        callback(fd)
        continue
    if errno == EINTR:
        continue
    if errno == EAGAIN:
        break
```

`EAGAIN` / `EWOULDBLOCK` 不是错误，它表示当前 pending connection 已经取完。

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
std::function<void(int, const sockaddr_in&)>
```

含义是：

```text
int              accepted fd，所有权交给 callback
sockaddr_in      对端地址
```

如果没有设置 callback，`Acceptor` 会立即关闭 accepted fd，避免文件描述符泄漏。

## close 的边界

`Acceptor::close()` 做两件事：

```text
从 EventLoop 移除 listen Channel
关闭 listen fd
```

关闭后 `listenFd()` 返回 `kInvalidFd`，`listening()` 返回 false，新客户端不能再连接到这个监听 socket。

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
