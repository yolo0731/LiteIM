# Step 12：实现 TcpServer

本步骤目标：实现服务端主体 `TcpServer`，把前面已经完成的 `EventLoop`、`Acceptor` 和 `Session` 组合起来，形成可以真正监听端口、接收连接、管理连接集合、发送消息并响应退出信号的网络层入口。

Step 12 仍然只做网络层组合，不做业务逻辑。登录、心跳响应、私聊、群聊、数据库存储都不在本 Step。

## 1. 为什么需要 TcpServer

前面几个模块的职责已经拆开：

- `EventLoop` 负责事件循环和事件派发。
- `Acceptor` 负责 listen fd，接收新连接。
- `Session` 负责一个 connected fd 的读、写、解包和关闭。

但是还缺一个总控对象：

- 谁创建 `Acceptor`？
- `Acceptor` accept 到新连接后，谁创建 `Session`？
- 多个 `Session` 放在哪里？
- 某个连接关闭时，谁从连接表删除它？
- 上层想给某个连接发送消息时，应该找谁？
- Ctrl+C / SIGTERM 到来时，谁负责关闭所有连接并退出事件循环？

这些都不应该塞进 `Acceptor` 或 `Session`。

所以 Step 12 新增 `TcpServer`：

```text
TcpServer
    ├── EventLoop*
    ├── Acceptor
    ├── sessions map
    ├── user -> session fd map
    └── signalfd Channel
```

它负责组合网络层对象，但不直接处理具体业务消息。

## 2. 本 Step 新增和修改的文件

新增：

```text
include/liteim/net/TcpServer.hpp
src/net/TcpServer.cpp
tests/test_tcp_server.cpp
tutorials/step12_tcp_server.md
```

修改：

```text
include/liteim/net/Acceptor.hpp
src/net/Acceptor.cpp
src/CMakeLists.txt
tests/CMakeLists.txt
tests/test_main.cpp
server/main.cpp
README.md
docs/architecture.md
docs/project_layout.md
docs/interview_notes.md
tutorials/README.md
```

`TcpServer` 放在 `net` 模块中，因为它仍然是网络层总控对象。它不属于业务层，也不属于存储层。

## 3. 整体运行流程

服务端入口现在大致是：

```cpp
liteim::net::EventLoop loop;
liteim::net::TcpServer server(&loop, "0.0.0.0", 9000);

server.start();
loop.loop();
```

运行后：

```text
server.start()
    ↓
Acceptor 开始 listen
listen fd 注册到 EventLoop
signalfd 注册到 EventLoop
    ↓
loop.loop()
    ↓
epoll_wait() 等待事件
```

当新客户端连接到来：

```text
listen fd 可读
    ↓
Acceptor::handleRead()
    ↓
accept4() 得到 conn fd
    ↓
TcpServer::handleNewConnection()
    ↓
创建 Session
    ↓
Session::start()
    ↓
conn fd 注册读事件
```

当客户端发消息：

```text
conn fd 可读
    ↓
Session::handleRead()
    ↓
FrameDecoder 解出 Packet
    ↓
TcpServer 的 message callback
    ↓
后续 MessageRouter 处理业务
```

当 Ctrl+C 或 SIGTERM 到来：

```text
signalfd 可读
    ↓
TcpServer::handleSignal()
    ↓
TcpServer::stop()
    ↓
关闭 sessions
关闭 listen socket
关闭 signal fd
EventLoop::quit()
```

## 4. Acceptor 的 close()

Step 12 给 `Acceptor` 补了一个显式关闭接口：

```cpp
void close();
```

它的作用是关闭监听器：

- 如果 listen fd 已经关闭，直接返回。
- 先调用 `accept_channel_.disableAll()`，从 `EventLoop` / epoll 中移除监听事件。
- 再调用 `closeFd(listen_fd_)` 关闭 listen socket。
- 把 `listen_fd_` 置为 `-1`。
- 把 `listening_` 置为 `false`。

为什么需要它？

之前 `Acceptor` 只在析构函数中关闭 listen fd。Step 12 有了优雅关闭之后，`TcpServer::stop()` 需要在对象析构前主动停止接收新连接，所以需要一个显式 `close()`。

`listen()` 也补了边界检查：

- 如果 `Acceptor` 已经关闭，再调用 `listen()` 会抛出 `std::runtime_error`。

`port()` 在关闭后返回 `0`。这表示当前没有有效 listen fd。

## 5. TcpServer.hpp 讲解

### TcpServer()

```cpp
TcpServer(EventLoop* loop, const std::string& listen_ip, std::uint16_t port);
```

作用：

- 创建一个服务端主体对象。
- 保存外部传入的 `EventLoop*`。
- 创建 `Acceptor`，绑定指定 IP 和端口。
- 创建 `signalfd`，用于接收 `SIGINT` / `SIGTERM`。
- 给 `Acceptor` 设置新连接回调。
- 给 signal `Channel` 设置读回调。

输入：

- `loop`：服务端要注册事件的 `EventLoop`。
- `listen_ip`：监听 IP，例如 `"127.0.0.1"` 或 `"0.0.0.0"`。
- `port`：监听端口。传 `0` 时由系统自动选择端口，测试里常用。

输出：

- 没有返回值。

副作用：

- 创建 listen socket。
- 阻塞当前进程的 `SIGINT` / `SIGTERM`，让它们后续通过 `signalfd` 读取。
- 创建 signal fd。

边界情况：

- 如果 `loop == nullptr`，抛出 `std::invalid_argument`。
- 如果 bind、socket 或 signalfd 创建失败，会抛出异常。

### ~TcpServer()

```cpp
~TcpServer();
```

作用：

- 兜底调用 `stop()`。
- 确保 sessions、listen fd、signal fd 都被关闭。
- 恢复构造时保存的 signal mask。

它保证即使上层忘记手动调用 `stop()`，也不会留下打开的 fd。

### start()

```cpp
void start();
```

作用：

- 调用 `Acceptor::listen()`，开始监听端口。
- 把 signal fd 对应的 `Channel` 注册到 `EventLoop`。
- 把 `started_` 置为 `true`。

注意：

`start()` 不会自己调用 `loop.loop()`。事件循环仍然由服务端入口控制：

```cpp
server.start();
loop.loop();
```

这样设计是为了让 `TcpServer` 不隐藏事件循环，也方便测试中手动控制 `loop.quit()`。

边界情况：

- 如果已经 `start()`，再次调用直接返回。
- 如果已经 `stop()`，再次调用会抛出 `std::runtime_error`。

### stop()

```cpp
void stop();
```

作用：

- 关闭所有 active sessions。
- 关闭 `Acceptor`，停止接受新连接。
- 从 `EventLoop` 移除 signal `Channel`。
- 关闭 signal fd。
- 恢复 signal mask。
- 调用 `EventLoop::quit()`。

边界情况：

- 如果已经停止，再次调用直接返回。

`stop()` 是幂等的，这样析构函数和信号回调都可以安全调用它。

### started()

```cpp
bool started() const;
```

作用：

- 查询服务端是否已经启动过监听和 signal 事件注册。

返回：

- `true` 表示已经执行过 `start()` 且尚未停止。
- `false` 表示未启动或已经停止。

### stopped()

```cpp
bool stopped() const;
```

作用：

- 查询服务端是否已经进入停止状态。

这个接口主要用于测试和运行状态观察。

### listening()

```cpp
bool listening() const;
```

作用：

- 查询内部 `Acceptor` 是否正在监听。

它反映的是 listen socket 状态，不等同于是否还有 active sessions。

### port()

```cpp
std::uint16_t port() const;
```

作用：

- 返回实际监听端口。

测试里经常传入端口 `0`，让系统自动分配可用端口。随后通过 `port()` 得到真实端口，再让客户端连接。

边界情况：

- 如果 listen fd 已经关闭，返回 `0`。

### sessionCount()

```cpp
std::size_t sessionCount() const;
```

作用：

- 返回当前 active session 数量。

它只统计还在 `sessions_` map 中的连接。已经关闭并移入 retired 列表的 Session 不算 active。

### sendToSession()

```cpp
bool sendToSession(int session_fd, const protocol::Packet& packet);
```

作用：

- 按 connected fd 查找 active `Session`。
- 找到后调用 `Session::sendPacket(packet)`。

输入：

- `session_fd`：连接 fd。
- `packet`：要发送的协议包。

返回：

- `true`：找到 active session，并成功把 packet 放入该 session 的输出缓冲区。
- `false`：session 不存在或已经关闭。

副作用：

- 会让目标 `Session` 编码 packet，并开启写事件监听。

### bindUserToSession()

```cpp
bool bindUserToSession(UserId user_id, int session_fd);
```

作用：

- 建立一个基础的 `user_id -> session_fd` 映射。

为什么说是“基础”？

Step 12 还没有登录流程，所以 `TcpServer` 不知道哪个连接属于哪个用户。这个函数只是给后续 `AuthService` 或 `MessageRouter` 留接口：

```text
登录成功
    ↓
AuthService 知道 user_id 和 Session
    ↓
调用 bindUserToSession()
```

返回：

- `true`：目标 session 存在且未关闭，绑定成功。
- `false`：目标 session 不存在或已经关闭。

### unbindUser()

```cpp
void unbindUser(UserId user_id);
```

作用：

- 删除某个用户 ID 的绑定。

后续用户登出、连接断开或重新登录时会用到。

### sendToUser()

```cpp
bool sendToUser(UserId user_id, const protocol::Packet& packet);
```

作用：

- 先通过 `user_id -> session_fd` 找到连接 fd。
- 再调用 `sendToSession()` 发送。

返回：

- `true`：用户已绑定到 active session，发送成功入队。
- `false`：没有用户绑定，或绑定的 session 已关闭。

注意：

这个函数不判断用户是否登录、不检查好友关系、不处理离线消息。那些是后续业务层职责。

### setMessageCallback()

```cpp
void setMessageCallback(MessageCallback callback);
```

作用：

- 设置 `TcpServer` 收到完整 `Packet` 后交给上层的回调。

调用链是：

```text
Session::handleRead()
    ↓
FrameDecoder 解包
    ↓
Session MessageCallback
    ↓
TcpServer::handleSessionMessage()
    ↓
TcpServer MessageCallback
```

Step 12 的测试会使用这个 callback 验证 `TcpServer` 能把客户端消息从 `Session` 转发出来。后续 Step 13 会把它接到 `MessageRouter`。

### loop()

```cpp
EventLoop* loop() const;
```

作用：

- 返回 `TcpServer` 使用的 `EventLoop*`。

它主要用于测试或上层观察，不转移所有权。`TcpServer` 不拥有这个 `EventLoop`，所以外部必须保证 `EventLoop` 生命周期长于 `TcpServer`。

## 6. TcpServer.cpp 关键实现

### 新连接处理

`Acceptor` 接收到新连接后，会调用：

```cpp
handleNewConnection(conn_fd, peer_address);
```

它做的事情是：

1. 清理上一次已经安全退休的 closed session。
2. 用 accepted fd 创建 `Session`。
3. 给 `Session` 设置 message callback。
4. 给 `Session` 设置 close callback。
5. 把 `Session` 放进 `sessions_`。
6. 调用 `session->start()` 注册读事件。

这里的顺序很重要：

```text
先设置 callback
再放入 sessions_
最后 start()
```

这样即使客户端连接后立刻发送数据，读事件触发时 callback 也已经准备好了。

### close callback 为什么不能直接销毁当前 Session

`Session` 关闭时会调用 close callback。

如果 `TcpServer` 在 callback 中直接把 `shared_ptr<Session>` 从 map 里删除，可能出现一个微妙问题：

```text
Session::handleRead()
    ↓
Session::closeConnection()
    ↓
close_callback_
    ↓
TcpServer 删除最后一个 shared_ptr
    ↓
Session 对象析构
    ↓
但 Session::closeConnection() 还没真正返回
```

这就是“对象还在执行成员函数时被销毁”的生命周期风险。

所以 Step 12 采用 retired 列表：

```text
active sessions_ map
    ↓ 关闭时移出
retired_sessions_
    ↓ 下次安全时清理
```

这样 active map 能立刻反映连接已经关闭，但对象不会在自己的回调栈上被立即释放。

### signalfd 优雅关闭

构造 `TcpServer` 时会：

```text
1. sigemptyset()
2. sigaddset(SIGINT)
3. sigaddset(SIGTERM)
4. sigprocmask(SIG_BLOCK)
5. signalfd()
```

为什么要先 `sigprocmask(SIG_BLOCK)`？

如果不阻塞信号，`SIGINT` / `SIGTERM` 会按默认行为直接终止进程，`signalfd` 不一定有机会读到它们。

阻塞后，这些信号会变成 pending signal，随后可以从 `signalfd` 读取。

`handleSignal()` 读到 `SIGINT` 或 `SIGTERM` 后调用：

```cpp
stop();
```

然后 `stop()` 关闭网络资源并调用：

```cpp
loop_->quit();
```

这样事件循环会退出，`main()` 继续往下走，服务端有序结束。

## 7. 测试说明

新增测试文件：

```text
tests/test_tcp_server.cpp
```

本 Step 测试了四类行为。

第一类：空 `EventLoop` 防御。

测试 `TcpServer(nullptr, ...)` 会抛出 `std::invalid_argument`。这是为了保证 `TcpServer` 不会拿空 loop 创建 `Acceptor`、`Channel` 或注册 epoll 事件。

第二类：新连接和消息分发。

测试会：

```text
创建 EventLoop
创建 TcpServer
server.start()
客户端连接 server.port()
客户端发送一个 Packet
loop.loop()
```

期望：

- `TcpServer` 能 accept 客户端。
- 能创建并保存一个 `Session`。
- `Session` 能读取并解码 Packet。
- `TcpServer` 的 message callback 能收到正确的 `msg_type`、`seq_id` 和 body。

第三类：发送接口。

测试在收到客户端请求后调用：

```cpp
sendToSession(...)
bindUserToSession(...)
sendToUser(...)
```

客户端侧用 `FrameDecoder` 解码服务端响应，确认：

- 按 session fd 可以发送。
- 绑定 user 后可以按 user id 发送。
- 未绑定 user 时 `sendToUser()` 返回 false。

第四类：`signalfd` 退出。

测试会：

```cpp
server.start();
raise(SIGTERM);
loop.loop();
```

因为 `TcpServer` 已经用 `sigprocmask` 阻塞 `SIGTERM` 并注册了 `signalfd`，所以测试进程不会被 SIGTERM 直接杀掉，而是通过 signal fd 事件进入 `TcpServer::handleSignal()`。

期望：

- `server.stopped() == true`
- `server.started() == false`
- `server.listening() == false`
- `server.sessionCount() == 0`

运行测试：

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

这些测试通过说明：`TcpServer` 已经能把监听、新连接、单连接读写、发送接口和信号退出串成一条完整网络层链路。

## 8. 运行服务端

构建后运行：

```bash
./build/server/liteim_server
```

期望输出：

```text
LiteIM server listening on port 9000
```

按 `Ctrl+C` 后，`SIGINT` 会进入 `signalfd`，然后触发 `TcpServer::stop()`。

期望输出：

```text
LiteIM server stopped
```

## 9. 本 Step 不做什么

Step 12 不做：

- 不实现 `MessageRouter`。
- 不处理 `HEARTBEAT_REQ`。
- 不实现登录。
- 不绑定真实用户身份。
- 不实现私聊、群聊。
- 不写 SQLite。
- 不做数据库 flush。
- 不做心跳 timer。
- 不切换 ET 模式。

这些都留到后续 Step。

## 10. 面试时怎么讲

可以这样讲：

> 我在网络层实现了 `TcpServer` 作为服务端主体。它组合 `EventLoop`、`Acceptor` 和 `Session`：`Acceptor` 负责监听和 accept，`Session` 负责单连接读写，`TcpServer` 负责创建 Session、维护连接表、处理连接关闭和提供发送入口。这样网络层职责比较清楚，后续 `MessageRouter` 只需要处理 Packet 语义，不需要直接操作 fd。

关于 `signalfd` 可以这样讲：

> 我没有在异步 signal handler 里直接关闭连接，而是用 `sigprocmask` 阻塞 SIGINT/SIGTERM，再用 `signalfd` 把它们转成普通 fd 事件，注册进同一个 epoll。这样 Ctrl+C 和 socket 读写一样都走 EventLoop 派发，关闭逻辑可以在正常 C++ 回调里执行，更容易控制资源释放顺序。

常见追问：

**Q：为什么 `TcpServer` 不直接读写 socket？**

A：读写 socket 属于单连接生命周期，放在 `Session` 更清楚。`TcpServer` 管连接集合和 server 生命周期。

**Q：为什么 `Acceptor` 不创建 `Session`？**

A：`Acceptor` 只应该负责 listen fd 和 accept。是否创建 `Session`、保存在哪里、close 后怎么删除，都属于更上层的 server 管理逻辑。

**Q：为什么用 `shared_ptr<Session>`？**

A：`Channel` 回调进入 `Session` 成员函数时，close callback 可能会让 `TcpServer` 从 map 中移除它。用 `shared_ptr` 配合 retired 列表，可以避免当前回调栈还没结束就销毁对象。

**Q：当前 `sendToUser()` 是否等于完整用户在线系统？**

A：不是。它只是基础发送接口，依赖显式 `user_id -> session_fd` 绑定。真正登录成功后什么时候绑定、断线后如何清理、离线消息怎么处理，属于后续业务层。

**Q：为什么 signal fd 也能放进 epoll？**

A：因为 Linux 下 socket、timerfd、signalfd 都是 fd。只要它们有可读/可写事件，就可以注册进 epoll，让 EventLoop 统一调度。
