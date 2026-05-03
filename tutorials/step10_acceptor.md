# Step 10：实现 Acceptor

本步骤目标：实现网络层监听器 `Acceptor`，让 LiteIM 服务端能够监听端口并接收新的 TCP 连接。

前面几个 Step 已经完成 Reactor 的底层三件套：

- `Epoller`：封装 Linux `epoll` 系统调用。
- `EventLoop`：循环等待事件并分发给 `Channel`。
- `Channel`：把一个 fd 和它的事件回调绑定起来。

Step 10 开始把这些能力用到真实服务端入口：创建 listen socket，并在有新连接到来时通过 callback 通知上层。

## 1. 这一步要解决什么问题

服务端要接受客户端连接，必须先有一个监听 socket：

```text
socket()
  ↓
setsockopt()
  ↓
bind()
  ↓
listen()
  ↓
accept()
```

如果直接把这些逻辑写进 `TcpServer`，后续 `TcpServer` 会同时负责 socket 系统调用、事件注册、Session 管理和业务分发，职责会变重。

所以 Step 10 单独实现 `Acceptor`：

- 它只负责监听端口。
- 它只负责接收新连接。
- 它通过 callback 把 accepted fd 交给上层。

后续 Step 12 的 `TcpServer` 会组合 `Acceptor` 和 `Session`，但 Step 10 不实现 `Session`。

## 2. 职责边界

`Acceptor` 负责：

- 创建 listen socket。
- 设置 `SO_REUSEADDR` 和 `SO_REUSEPORT`。
- 绑定 IP 和端口。
- 调用 `listen()`。
- 把 listen fd 注册到 `EventLoop`。
- listen fd 可读时循环 `accept4()` 到 `EAGAIN`。
- 把 accepted fd 和 peer address 交给 callback。

`Acceptor` 不负责：

- 不解析协议包。
- 不管理用户登录状态。
- 不维护在线连接表。
- 不创建 `Session`。
- 不处理读写业务消息。

accepted fd 的所有权规则：

- callback 成功返回后，accepted fd 交给 callback 所在的上层对象。
- 当前还没有 callback 时，`Acceptor` 会直接关闭 accepted fd，避免泄漏。

## 3. 本步骤新增文件

新增文件：

```text
include/liteim/net/Acceptor.hpp
src/net/Acceptor.cpp
tests/test_acceptor.cpp
tutorials/step10_acceptor.md
```

修改文件：

```text
src/CMakeLists.txt
tests/CMakeLists.txt
tests/test_main.cpp
README.md
docs/architecture.md
docs/interview_notes.md
docs/project_layout.md
tutorials/README.md
task_plan.md
findings.md
progress.md
```

## 4. Acceptor.hpp 讲解

文件：

```text
include/liteim/net/Acceptor.hpp
```

### NewConnectionCallback

```cpp
using NewConnectionCallback = std::function<void(int, const sockaddr_in&)>;
```

作用：

- 定义新连接回调类型。

输入：

- 第一个参数是 accepted client fd。
- 第二个参数是客户端地址 `sockaddr_in`。

输出：

- 没有返回值。

所有权：

- callback 被成功调用后，accepted fd 的所有权转交给 callback。
- 后续 `TcpServer` 会在 callback 中创建 `Session` 管理这个 fd。

### Acceptor()

```cpp
Acceptor(EventLoop* loop, const std::string& listen_ip, std::uint16_t port);
```

作用：

- 创建 `Acceptor`。
- 创建非阻塞 listen socket。
- 设置端口复用选项。
- `bind()` 到指定 IP 和端口。
- 创建 listen fd 对应的 `Channel`。
- 给 listen `Channel` 设置 read callback。

输入：

- `loop`：所属事件循环，不能为 `nullptr`。
- `listen_ip`：监听 IP，例如 `"127.0.0.1"` 或 `"0.0.0.0"`。
- `port`：监听端口；传 0 时由系统分配可用端口，测试中会用这个方式避免端口冲突。

失败场景：

- `loop == nullptr` 抛出 `std::invalid_argument`。
- IP 地址非法抛出 `std::invalid_argument`。
- `socket()`、`setsockopt()`、`bind()` 失败时抛出 `std::system_error`。

### ~Acceptor()

```cpp
~Acceptor();
```

作用：

- 从 `EventLoop` 移除 listen `Channel`。
- 关闭 listen fd。

边界：

- 不关闭已经交给 callback 的 accepted fd。
- accepted fd 后续由 `Session` 或测试 callback 关闭。

### listen()

```cpp
void listen();
```

作用：

- 调用系统 `listen()`。
- 把 listen fd 的读事件注册到 `EventLoop`。

副作用：

- listen fd 开始接收连接。
- `accept_channel_.enableReading()` 会通过 `Channel` 自动更新 epoll。

边界：

- 多次调用是幂等的，已经 listening 后直接返回。
- `listen()` 失败时抛出 `std::system_error`。

### listening()

```cpp
bool listening() const;
```

作用：

- 返回当前是否已经调用过 `listen()` 并进入监听状态。

输出：

- 已监听返回 true。
- 未监听返回 false。

### listenFd()

```cpp
int listenFd() const;
```

作用：

- 返回内部 listen fd。

使用场景：

- 测试和调试时确认 fd 有效。

边界：

- 调用者不能关闭这个 fd。
- listen fd 由 `Acceptor` 析构时关闭。

### port()

```cpp
std::uint16_t port() const;
```

作用：

- 返回实际绑定的端口。

为什么需要：

- 测试中传入端口 0，让系统自动分配可用端口。
- 测试客户端需要通过 `port()` 获取真实端口再连接。

失败场景：

- `getsockname()` 失败时抛出 `std::system_error`。

### setNewConnectionCallback()

```cpp
void setNewConnectionCallback(NewConnectionCallback callback);
```

作用：

- 设置新连接回调。

输入：

- `callback`：收到新连接时执行的函数。

副作用：

- 保存 callback。

边界：

- 如果没有设置 callback，`Acceptor` 会 close accepted fd。
- 这样即使上层还没接入 `TcpServer`，也不会泄漏连接 fd。

## 5. Acceptor.cpp 讲解

文件：

```text
src/net/Acceptor.cpp
```

### createBoundListenSocket()

作用：

- 创建 listen socket。
- 设置 `SO_REUSEADDR` 和 `SO_REUSEPORT`。
- `bind()` 到指定地址。
- 成功后返回 fd。

为什么还没调用 `listen()`：

- 构造函数负责准备 socket。
- `listen()` 方法显式让 socket 进入监听状态。
- 这样对象状态更清楚，也方便测试 `listening()`。

### makeListenAddress()

作用：

- 把字符串 IP 和端口转换成 `sockaddr_in`。

边界：

- 空字符串和 `"0.0.0.0"` 表示监听所有本地 IPv4 地址。
- 非法 IPv4 字符串会抛出 `std::invalid_argument`。

### listen() 实现

核心逻辑：

```cpp
if (::listen(listen_fd_, SOMAXCONN) < 0) {
    throw makeSystemError("listen");
}

accept_channel_.enableReading();
listening_ = true;
```

含义：

1. 让 listen socket 进入监听状态。
2. 关注 listen fd 的读事件。
3. epoll 注册成功后标记 `listening_`。

为什么 listen fd 关注读事件：

- 对 listen socket 来说，可读不是“有普通数据可读”。
- 它表示有新连接可以 `accept()`。

### handleRead() 实现

核心逻辑：

```cpp
while (true) {
    int conn_fd = ::accept4(..., SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (conn_fd >= 0) {
        new_connection_callback_(conn_fd, peer_address);
        continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
    }
}
```

作用：

- 当 listen fd 可读时，接收所有当前排队的新连接。

为什么要循环到 `EAGAIN`：

- 一次 epoll 可读事件可能对应多个排队连接。
- 只 accept 一个会让剩余连接继续留在内核队列里。
- 循环到 `EAGAIN` 表示当前已经没有更多连接可接收。

为什么用 `accept4()`：

- 可以在 accept 的同时设置 `SOCK_NONBLOCK`。
- 可以同时设置 `SOCK_CLOEXEC`。
- 避免先 accept 出一个阻塞 fd，再单独调用 `fcntl()` 的中间状态。

错误处理：

- `EAGAIN` / `EWOULDBLOCK`：正常结束本轮 accept 循环。
- `EINTR`：被信号打断，继续重试。
- `ECONNABORTED`：客户端在 accept 前断开，跳过继续。
- 其他错误：抛出 `std::system_error`。

### 析构函数实现

作用：

- 调用 `accept_channel_.disableAll()`，从 `EventLoop` 移除 listen fd。
- 调用 `closeFd(listen_fd_)` 关闭 listen fd。

为什么析构里捕获异常：

- 析构函数不应该向外抛异常。
- 如果移除 Channel 时发生异常，析构中吞掉异常，再继续关闭 fd。

## 6. 本步骤测试

新增测试文件：

```text
tests/test_acceptor.cpp
```

接入位置：

```text
tests/CMakeLists.txt
tests/test_main.cpp
```

测试使用真实 localhost TCP 连接，而不是 mock。这样可以验证 `socket()`、`bind()`、`listen()`、`accept4()`、`EventLoop` 和 `Channel` 的完整集成路径。

### acceptor listen is idempotent

验证：

- 构造 `Acceptor` 后还没有 listening。
- `listenFd()` 是有效 fd。
- 端口 0 会被系统解析成真实端口。
- 连续调用两次 `listen()` 不会重复注册或报错。

为什么要测：

- `listen()` 是启动监听的关键状态切换。
- 幂等行为可以避免上层重复调用造成异常。

### acceptor accepts connection and invokes callback

验证：

- 监听 `127.0.0.1:0`。
- 测试客户端连接真实端口。
- `EventLoop` 分发 listen fd 可读事件。
- `Acceptor` 调用 callback。
- callback 收到的 accepted fd 是非阻塞的。
- callback 能收到 peer address。

为什么要测：

- 这是 Step 10 的主路径。
- 通过它证明 `Acceptor -> Channel -> EventLoop -> accept4 -> callback` 路径真实可用。

### acceptor accepts all pending connections

验证：

- 在 `loop()` 前先建立 3 个客户端连接。
- `Acceptor::handleRead()` 会连续 accept。
- callback 最终收到 3 个连接。

为什么要测：

- 证明 accept 循环没有只接收一个连接就返回。
- 这是非阻塞监听器的关键正确性。

### acceptor rejects null loop

验证：

- `Acceptor(nullptr, "127.0.0.1", 0)` 抛出 `std::invalid_argument`。

为什么要测：

- `Acceptor` 必须注册 listen `Channel` 到事件循环。
- 没有 `EventLoop` 时对象无法正常工作，应尽早拒绝。

运行测试：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

测试通过说明：

- `Acceptor` 能完成 bind/listen。
- listen fd 能通过 `EventLoop` 收到新连接事件。
- accepted fd 是非阻塞的。
- 多个 pending connection 能被 drain 到 `EAGAIN`。
- 无效构造参数会被拒绝。

## 7. 面试时怎么讲

可以这样说：

> 我把服务端监听 socket 封装成了 `Acceptor`。它负责创建非阻塞 listen fd，设置端口复用，完成 `bind()` 和 `listen()`，然后把 listen fd 注册成 `Channel`。当 listen fd 可读时，说明有新连接到来，`Acceptor` 会循环 `accept4()` 到 `EAGAIN`，并通过 callback 把 accepted fd 交给上层。它不创建 Session，也不处理业务，只负责接新连接。

为什么 listen fd 可读表示有新连接：

- listen socket 不传输业务数据。
- 内核连接队列里有完成握手的连接时，listen fd 会变成可读。
- 此时调用 `accept()` 可以取出一个新连接 fd。

为什么 accepted fd 必须非阻塞：

- 后续 `Session` 会放进 epoll 事件循环。
- 如果 fd 是阻塞的，某次 `read()` 或 `write()` 可能卡住整个事件循环。
- 非阻塞 fd 配合 `EAGAIN` 才能让一个线程管理多个连接。

为什么 `Acceptor` 不创建 `Session`：

- 单一职责更清楚。
- `Acceptor` 只知道新连接 fd。
- `Session` 的生命周期、保存位置、关闭回调由 `TcpServer` 统一管理更合适。

## 8. 面试常见追问

**Q：`bind()` 和 `listen()` 的区别是什么？**

A：`bind()` 是把 socket 绑定到本地 IP 和端口；`listen()` 是让这个 socket 进入监听状态，开始接收 TCP 连接队列。

**Q：为什么 accept 要循环到 `EAGAIN`？**

A：一次可读事件可能对应多个待 accept 连接。循环到 `EAGAIN` 才表示当前队列已经取空，避免连接积压。

**Q：`accept4()` 比 `accept()` 好在哪里？**

A：`accept4()` 可以一次性设置 `SOCK_NONBLOCK` 和 `SOCK_CLOEXEC`，避免先得到一个阻塞 fd 再补 `fcntl()` 的中间状态。

**Q：callback 收到的 fd 谁负责关闭？**

A：callback 成功返回后，上层获得 fd 所有权，后续由 `Session` 关闭。如果没有 callback，`Acceptor` 会直接关闭 fd。

**Q：为什么 `Acceptor` 析构前要从 EventLoop 移除 Channel？**

A：`Epoller` 保存的是 `Channel*`。如果对象销毁后没有移除，epoll 后续可能返回悬空指针，造成未定义行为。
