# Step 16：实现多 Reactor TcpServer

本 Step 的目标是把前面已经完成的网络组件串成第一个可工作的多 Reactor echo server。

到 Step 15 为止，项目已经有：

```text
Acceptor              -> 只负责监听和 accept
EventLoopThreadPool   -> 启动多个 I/O loops
Session               -> 管理一个已连接 fd 的读写和 Packet 编解码
```

Step 16 新增 `TcpServer`，它负责把这三部分连接起来：

```text
base EventLoop
  -> Acceptor accepts UniqueFd
  -> TcpServer chooses one I/O EventLoop
  -> queueInLoop(create Session)

child EventLoop
  -> owns Session
  -> handles read/write
  -> decodes Packet
  -> default echo or user callback
```

第一版业务只做 echo。它不做登录、好友、私聊、群聊、MySQL、Redis 或业务线程池。

## 1. 为什么需要 TcpServer

`Acceptor` 只知道“有新 TCP 连接进来”，但它不应该创建业务连接对象，也不应该处理 Packet。

`Session` 只管理一个已连接 fd，但它不知道整个服务端有多少连接，也不知道新连接应该分到哪个 I/O 线程。

`EventLoopThreadPool` 只管理多个子 loops，但它不知道什么时候有新连接，也不知道如何创建 `Session`。

所以需要 `TcpServer` 做网络层协调：

- base loop 持有 `Acceptor`。
- `Acceptor` accept 到 fd 后把 `UniqueFd` 交给 `TcpServer`。
- `TcpServer` 通过 `EventLoopThreadPool::getNextLoop()` 选择一个 I/O loop。
- `TcpServer` 把创建 `Session` 的任务投递到目标 I/O loop。
- `TcpServer` 维护线程安全的 `sessions_` 表。
- 连接关闭时从 `sessions_` 删除。
- 对外提供 `sendToSession()` 和基础 `sendToUser()` 接口。

核心边界是：连接一旦分配给某个 I/O loop，后续 socket 读写都必须回到这个 loop。

## 2. 本 Step 新增文件

```text
include/liteim/net/TcpServer.hpp
src/net/TcpServer.cpp
tests/net/tcp_server_header_test.cpp
tests/net/tcp_server_test.cpp
tutorials/step16_tcp_server.md
```

同时更新：

```text
src/net/CMakeLists.txt
tests/CMakeLists.txt
README.md
docs/architecture.md
docs/project_layout.md
docs/roadmap.md
tutorials/README.md
task_plan.md
findings.md
progress.md
PROJECT_MEMORY.md
```

## 3. TcpServer.hpp 接口说明

### 构造函数

```cpp
TcpServer(EventLoop* base_loop, std::string listen_ip, std::uint16_t port, std::size_t io_thread_count);
```

含义：

- `base_loop` 是主 Reactor loop，负责监听 fd 和 accept。
- `listen_ip` / `port` 是监听地址。端口传 `0` 时由系统分配临时端口，测试里会这样用。
- `io_thread_count` 是子 Reactor 线程数。为 `0` 时，`EventLoopThreadPool` 返回 base loop，形成单 Reactor fallback。

失败场景：

- `base_loop == nullptr` 会抛 `std::invalid_argument`。

### `setMessageCallback()`

```cpp
void setMessageCallback(MessageCallback callback);
```

设置收到完整 `Packet` 后的处理逻辑。

如果不设置 callback，`TcpServer` 默认把收到的 `Packet` 原样发回客户端，也就是 echo。

这个设计让 Step 16 可以先验证网络底座；后续业务 Step 再把 callback 接到登录、聊天、群组和 Bot 路由。

### `start()`

```cpp
void start();
```

启动服务端。它必须在 `base_loop` 所属线程调用。

主要动作：

1. 启动 `EventLoopThreadPool`。
2. 创建 `Acceptor`。
3. 把 `Acceptor::NewConnectionCallback` 绑定到 `TcpServer::handleNewConnection()`。
4. 保存实际监听端口。
5. 标记 `started_ = true`。

如果 `Acceptor` 创建失败，会停止已经启动的 I/O 线程池，避免留下后台线程。

### `stop()`

```cpp
void stop() noexcept;
```

停止服务端。

在 base loop 线程调用时，它直接执行关闭逻辑；从其他线程调用时，它把关闭任务投递回 base loop。

关闭逻辑包括：

- 关闭 `Acceptor`。
- 清空 `sessions_` 表。
- 调用每个 `Session::close()`。
- 停止 I/O 线程池。

### 状态查询

```cpp
std::uint16_t port() const noexcept;
std::size_t sessionCount() const;
bool started() const noexcept;
```

- `port()` 返回实际监听端口，测试使用临时端口时需要它。
- `sessionCount()` 返回当前连接数量，内部用 mutex 保护 `sessions_`。
- `started()` 表示服务端是否处于启动状态。

### `sendToSession()`

```cpp
Status sendToSession(std::uint64_t session_id, const Packet& packet);
```

按逻辑 session id 查找连接并发送 Packet。

这个函数可以从其他线程调用，但它不会直接操作 socket。它找到 `Session` 后调用 `Session::sendPacket()`，后者会把真正的发送任务投递回 session 所属 I/O loop。

失败场景：

- 找不到 session 时返回 `ErrorCode::NotFound`。
- Packet 编码失败时返回编码层的错误。

### `sendToUser()`

```cpp
Status sendToUser(std::uint64_t user_id, const Packet& packet);
```

这是后续业务层会用到的基础接口。

当前还没有登录态，也没有 `user_id -> session_id` 绑定表，所以本 Step 固定返回 `NotFound`。

## 4. TcpServer.cpp 实现思路

### `handleNewConnection()`

`Acceptor` accept 到新连接后，会把 accepted fd 作为 `UniqueFd` move 给 `TcpServer`。

`TcpServer` 的处理流程是：

```text
handleNewConnection(UniqueFd fd)
  -> getNextLoop()
  -> queueInLoop(createSessionInLoop)
```

这里不在 base loop 里直接创建 `Session`，因为连接 I/O 应该归属于被选中的子 I/O loop。

`EventLoop::Functor` 使用 `std::function`，需要可拷贝 callable；`UniqueFd` 是 move-only 类型，所以实现里用 `std::shared_ptr<UniqueFd>` 暂存 accepted fd，再投递到目标 loop。

### `createSessionInLoop()`

这个函数在目标 I/O loop 中执行。

主要动作：

1. 从 `UniqueFd` 中 release fd。
2. 从 `next_session_id_` 取一个自增逻辑 id。
3. 创建 `std::shared_ptr<Session>`。
4. 设置 message callback。
5. 设置 close callback。
6. 写入 `sessions_` 表。
7. 调用 `session->start()` 注册读事件。

`Session` 创建和启动都在它所属的 I/O loop 中完成，这样 `Channel` 注册不会跨 loop。

这里不要用 fd 当 session id。fd 关闭后内核会快速复用，如果旧连接的关闭删除任务晚于新连接注册，就可能用同一个 fd 把新 session 从表里删掉。Step 17 后的 review hardening 改为 `std::atomic<std::uint64_t> next_session_id_` 自增，fd 只用于 socket I/O，session id 只用于连接身份。

### `handleMessage()`

收到 Packet 后有两种路径：

- 如果用户设置了 `MessageCallback`，调用用户 callback。
- 如果没有设置 callback，执行默认 echo：`session->sendPacket(packet)`。

默认 echo 是 Step 16 的验收重点。它证明：

- 客户端能连上 server。
- server 能读入完整 Packet。
- `FrameDecoder` 能处理 TCP 字节流。
- `Session` 能把 Packet 编码后写回客户端。
- 子 Reactor I/O loop 能正常工作。

### `removeSession()`

`Session` 关闭时会触发 close callback，`TcpServer` 根据逻辑 session id 从 `sessions_` 表删除连接。

`sessions_` 用 mutex 保护，因为：

- I/O loop 可能在关闭连接时删除 session。
- 外部线程可能同时调用 `sessionCount()` 或 `sendToSession()`。

## 5. 本 Step 不做什么

本 Step 不实现：

- business `ThreadPool`
- 登录态
- `user_id -> session` 绑定
- `MessageRouter`
- MySQL
- Redis
- 心跳超时
- 输出缓冲区高水位回压
- CLI / Qt / benchmark / CI

这些留给后续 Step。

## 6. 测试设计

新增测试文件：

```text
tests/net/tcp_server_header_test.cpp
tests/net/tcp_server_test.cpp
```

测试用例：

```cpp
TEST(ReactorInterfaceTest, TcpServerHeaderIsSelfContained)
TEST(TcpServerTest, EchoesPacketToClient)
TEST(TcpServerTest, DistributesConnectionsAcrossIoLoops)
TEST(TcpServerTest, RemovesSessionAfterClientDisconnects)
TEST(TcpServerTest, SendToSessionFromOtherThreadDeliversPacket)
TEST(TcpServerTest, SendToUnknownUserReturnsNotFound)
```

覆盖点：

- `TcpServer.hpp` 可以独立 include，公开接口签名稳定。
- 客户端发送一个 Packet 后能收到同样的 Packet，验证默认 echo。
- 多个客户端连接会被分配到不同 I/O loops，验证 round-robin 多 Reactor 分发。
- 客户端断开后 `sessionCount()` 会减少，验证 close callback 和 `sessions_` 删除。
- 测试线程调用 `sendToSession()` 后，数据仍能发到客户端，验证跨线程发送路径。
- `sendToSession()` 使用 `Session::id()` 传回的逻辑 id，不再依赖 fd。
- 当前 `sendToUser()` 没有登录态绑定，会返回 `NotFound`。

TDD RED 阶段，新增测试先失败在：

```text
fatal error: liteim/net/TcpServer.hpp: No such file or directory
```

这说明测试确实覆盖了 Step 16 缺失接口。

## 7. 如何运行测试

全量验证：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

只运行本 Step：

```bash
ctest --test-dir build --output-on-failure -R "TcpServer|ReactorInterfaceTest.TcpServer"
```

Step 16 完成后，CTest 应通过 142 个测试，其中 6 个是新增的 Step 16 `TcpServer` 测试。

## 8. 面试时怎么讲

可以这样讲：

> 我实现了一个主从 Reactor 结构的 `TcpServer`。主 Reactor 只负责监听和 accept；每个新连接由 `EventLoopThreadPool` 按 round-robin 分配到一个子 I/O loop；`Session` 在目标 loop 中创建并固定归属该 loop。服务端维护一个线程安全的 session 表，外部线程调用 `sendToSession()` 时不会直接写 socket，而是复用 `Session::sendPacket()` 投递回连接所属 loop。第一版业务默认 echo，用来验证多 Reactor 网络底座。

关键点：

- `Acceptor` 不拥有连接生命周期，只交出 accepted fd。
- `UniqueFd` 表达 accepted fd 所有权，避免裸 fd 泄漏。
- `Session` 必须在所属 I/O loop 创建和启动。
- `sessions_` 表需要 mutex，因为查询和删除可能来自不同线程；表 key 是逻辑 session id，不是 fd。
- 跨线程发送必须回到 session 所属 I/O loop。
- Step 16 只验证网络底座，不把 MySQL / Redis 阻塞调用放进 I/O 线程。

## 9. 面试常见追问

**为什么主 Reactor 不直接处理所有连接读写？**

主 Reactor 如果既 accept 又读写所有连接，很容易成为瓶颈。多 Reactor 模型让主 Reactor 专注接入，子 Reactor 处理连接 I/O。

**为什么 Session 要在子 I/O loop 中创建？**

`Session` 内部有 `Channel`，`Channel` 必须注册到所属 loop 的 `Epoller`。如果在 base loop 创建并注册到子 loop，会破坏 one-loop-per-thread 边界。

**为什么 sendToSession() 可以跨线程调用？**

因为它不直接写 fd。它只根据逻辑 session id 找到 `Session`，然后调用 `Session::sendPacket()`；`sendPacket()` 会通过 `queueInLoop()` 把发送任务投递回连接所属 loop。

**为什么不用 fd 当 session_id？**

fd 是内核资源编号，关闭后可能马上复用。如果旧连接的 remove 回调排队较晚，而新连接刚好拿到同一个 fd，旧 remove 就可能误删新 session。逻辑 session id 单调递增，能把“socket fd”和“连接身份”分开。

**为什么 sendToUser() 现在返回 NotFound？**

当前还没有登录业务，也没有 user 和 session 的绑定关系。先保留接口，等后续登录和会话管理 Step 再补真实映射。

**为什么默认业务是 echo？**

echo 足够验证网络底座：连接接入、Packet 解码、输出编码、跨线程唤醒和写事件都必须正常，才能把同一个 Packet 发回客户端。
