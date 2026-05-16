# Step 16：实现多 Reactor TcpServer

## 0. 本 Step 结论

- 目标：本 Step 的目标是把前面已经完成的网络组件串成第一个可工作的多 Reactor echo server。
- 前置依赖：依赖 Step 0-15 已建立的工程、协议或运行时基础。
- 主要交付：`实现多 Reactor TcpServer` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

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

### 为什么需要 TcpServer

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
- 对外提供按逻辑连接 id 发送的 `sendToSession()` 接口。

核心边界是：连接一旦分配给某个 I/O loop，后续 socket 读写都必须回到这个 loop。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `实现多 Reactor TcpServer` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/net/TcpServer.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/TcpServer.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/tcp_server_header_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/tcp_server_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/tutorials/step16_tcp_server.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `PROJECT_MEMORY.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

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

`TcpServer` 是 base loop 拥有的 Reactor 对象，`stop()` 必须在 base loop 线程调用。非 owner 线程调用会 `std::terminate()`，避免把捕获裸 `this` 的关闭任务排进 base loop 后对象先析构，造成 use-after-free 风险。需要跨线程停止时，上层应先 `queueInLoop()`，让 `server->stop()` 真正在 base loop 线程执行。

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
- 查到空指针或调用当下 session 已经关闭时返回 `ErrorCode::NotFound`。
- Packet 编码失败时返回编码层的错误。

注意：`sendToSession()` 只能判断“查表这一刻已知连接存在且未关闭”。真正发送仍由 `Session::sendPacket()` 异步投递到连接所属 I/O loop；如果投递后连接又关闭，底层发送任务会被丢弃，调用方不能把 `Status::ok()` 理解成字节一定已经写出。

### 关键成员变量和内部 helper

`TcpServer` 的关键成员包括：

- `base_loop_`：主 Reactor，拥有 `Acceptor`、heartbeat timer 和 server 生命周期。
- `listen_ip_` / `requested_port_` / `port_`：监听地址、请求端口和实际端口。
- `io_threads_`：I/O `EventLoopThreadPool`。
- `acceptor_`：listen socket owner，只在 base loop 创建和关闭。
- `heartbeat_timer_`：Step 18 接入的 base-loop timerfd 管理器。
- `mutex_` 和 `sessions_`：保护逻辑 session id 到 `Session::Ptr` 的连接表。
- `message_callback_`：上层业务入口，未设置时默认 echo。
- `heartbeat_interval_` / `heartbeat_timeout_`：空闲连接扫描配置。
- `session_output_high_water_mark_`：Step 20 传给每个 Session 的输出高水位。
- `next_session_id_`：生成不会被 fd 复用影响的逻辑连接 id。
- `started_`：启动状态。

内部 helper：

- `stopInLoop()` 只在 base loop 清理 `Acceptor`、timer、session 表和 I/O pool。
- `handleNewConnection()` 从 `Acceptor` 接收 `UniqueFd`，选择 I/O loop。
- `createSessionInLoop()` 在目标 I/O loop 创建 Session、设置 callback 和高水位、写入 session 表。
- `handleMessage()` 调用用户 callback，未设置时 echo。
- `removeSession()` 在 close callback 中删除 session 表项。
- `startHeartbeatTimer()`、`scheduleHeartbeatCheck()`、`closeIdleSessions()` 支撑 Step 18 heartbeat timeout。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

`TcpServer` 是当前网络层协调器：它把 base loop、`Acceptor`、I/O `EventLoopThreadPool`、`Session`、heartbeat `TimerManager` 和上层 message callback 串起来。它不处理登录、好友、群聊、MySQL 或 Redis。

### 2. 上下层调用连接

```text
server/main.cpp
    -> EventLoop base_loop
    -> TcpServer(base_loop, host, port, io_threads)
    -> SignalWatcher / Config options
    -> TcpServer::start()
        -> EventLoopThreadPool
        -> Acceptor
        -> TimerManager heartbeat
    -> Session callbacks
    -> business MessageCallback or echo fallback
```

新连接链路：

```text
Acceptor callback(UniqueFd)
    -> TcpServer::handleNewConnection()
    -> io_threads_.getNextLoop()
    -> io_loop->queueInLoop(createSessionInLoop)
    -> Session::start()
```

### 3. 整体运行链路

1. `server/main.cpp` 创建 base `EventLoop` 和 `TcpServer`。
2. 上层可在启动前设置 message callback、heartbeat options 和 session 输出高水位。
3. [TcpServer::start()](../src/net/TcpServer.cpp) 在 base loop 启动 I/O 线程池。
4. `start()` 创建 Acceptor 并设置 new connection callback。
5. `start()` 标记 started 后启动 heartbeat timer。
6. 新连接进入 [handleNewConnection()](../src/net/TcpServer.cpp)，选择 I/O loop 并排队创建 Session。
7. [createSessionInLoop()](../src/net/TcpServer.cpp) 在目标 I/O loop 创建 `Session`、设置回调、加入 `sessions_`、启动读事件。
8. Packet 到来后，`Session` 回调 [handleMessage()](../src/net/TcpServer.cpp)。
9. 如果业务 callback 存在，交给业务；否则第一版 echo 原 Packet。
10. 连接关闭时，[removeSession()](../src/net/TcpServer.cpp) 删除 session 表项。
11. [stopInLoop()](../src/net/TcpServer.cpp) 停止接入、timer、sessions 和 I/O 线程池。

### 4. 自身内部运行流程

整体可以看成 5 步：启动、接入、建连接、收发消息、关闭和心跳。

核心成员职责：

- `base_loop_` 是 owner loop，`TcpServer` start/stop/destruct 都在这里。
- `acceptor_` 负责 listen fd 和 accept。
- `io_threads_` 负责连接 I/O loop 分配。
- `sessions_` 保存逻辑 session id 到 `Session::Ptr` 的映射。
- `message_callback_` 是后续业务层入口。
- `heartbeat_timer_` 是 base loop 上的 `TimerManager`。
- `session_output_high_water_mark_` 是新 Session 的输出缓冲阈值快照。

核心函数流程：

- `start()`：启动 I/O pool、创建 Acceptor、启动 heartbeat；失败时回滚 timer、acceptor 和线程池。
- `handleNewConnection()`：只在 base loop 接收 `UniqueFd`，选择 I/O loop，把 fd 放进 shared holder 后排队。
- `createSessionInLoop()`：目标 I/O loop 中消费 `UniqueFd`，创建并启动 Session。
- `handleMessage()`：拿 callback 快照，有业务 callback 则调用，否则 echo。
- `removeSession()`：按逻辑 id 从 `sessions_` 删除，避免 fd 复用误删。
- [scheduleHeartbeatCheck()](../src/net/TcpServer.cpp)：one-shot 续排 heartbeat 检查。
- [closeIdleSessions()](../src/net/TcpServer.cpp)：扫描 session 快照，关闭超时连接。

`handleNewConnection()` 可以理解成“base loop 收连接，I/O loop 建 Session”：

```text
Acceptor 交出 UniqueFd
    ↓
TcpServer::handleNewConnection() 在 base loop 执行
    ↓
EventLoopThreadPool::getNextLoop() 选择目标 I/O loop
    ↓
把 UniqueFd 所有权随任务投递到目标 loop
    ↓
createSessionInLoop() 创建 Session 并绑定 Channel
    ↓
Session::start() 开启读事件
```

这样 fd 所有权从 `Acceptor` 到 `TcpServer` 再到 `Session` 全程清楚，业务 callback 也不会在 base loop 里直接操作连接内部状态。

### 5. 该项目代码在实际应用中的具体数据例子

Alice 连接建立后，`TcpServer` 分配 `session_id=42` 并把 Session 存入 `sessions_`；Bob 连接建立后得到 `session_id=43`。业务线程保存 `message_id=5001` 后调用 `sendToSession(43, packet)`，TcpServer 只查逻辑 session id 并投递发送任务，不用 fd 当用户身份，也不假设 fd 不会复用。

## 6. 关键实现点

### TcpServer.cpp 实现思路

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

1. 从 `next_session_id_` 取一个自增逻辑 id。
2. 把 accepted `UniqueFd` move 给 `Session(EventLoop*, UniqueFd, id)`。
3. 创建 `std::shared_ptr<Session>`。
4. 设置 message callback。
5. 设置 close callback。
6. 写入 `sessions_` 表。
7. 调用 `session->start()` 注册读事件。

`Session` 创建和启动都在它所属的 I/O loop 中完成，这样 `Channel` 注册不会跨 loop。accepted fd 的所有权一直由 `UniqueFd` 表达，即使 `Session` 构造过程中抛异常，fd 也会自动关闭。

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

Step 16 的目标只是把多 Reactor 网络底座打通，所以当时不实现：

- business `ThreadPool`
- 登录态
- `user_id -> session` 绑定
- `MessageRouter`
- MySQL
- Redis
- 心跳超时；后续 Step 18 已经通过 `timerfd` / `TimerManager` 接入 idle session cleanup。
- 输出缓冲区高水位回压；后续 review hardening 和 Step 20 已经实现默认 4MB、配置键 `server.output_high_water_mark_bytes` 和超限关闭。
- CLI / Qt / benchmark
- `sendToUser()`；当前 public API 仍不暴露这个占位接口，等登录态和真实 user-session 绑定表实现后再引入。

这些留给后续 Step。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `实现多 Reactor TcpServer` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

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
TEST(TcpServerTest, StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis)
```

覆盖点：

- `TcpServer.hpp` 可以独立 include，公开接口签名稳定。
- 客户端发送一个 Packet 后能收到同样的 Packet，验证默认 echo。
- 多个客户端连接会被分配到不同 I/O loops，验证 round-robin 多 Reactor 分发。
- 客户端断开后 `sessionCount()` 会减少，验证 close callback 和 `sessions_` 删除。
- 测试线程调用 `sendToSession()` 后，数据仍能发到客户端，验证跨线程发送路径。
- `sendToSession()` 使用 `Session::id()` 传回的逻辑 id，不再依赖 fd。
- 非 owner 线程直接调用 `stop()` 会终止进程，验证生命周期契约不再用裸 `this` 异步排队。

TDD RED 阶段，新增测试先失败在：

```text
fatal error: liteim/net/TcpServer.hpp: No such file or directory
```

这说明测试确实覆盖了 Step 16 缺失接口。

全量验证：

```bash
cmake -S . -B build
cmake --build build
timeout 1s ./build/server/liteim_server || test $? -eq 124
ctest --test-dir build --output-on-failure
```

只运行本 Step：

```bash
ctest --test-dir build --output-on-failure -R "TcpServer|ReactorInterfaceTest.TcpServer"
```

## 8. 验证命令

```bash
cmake -S . -B build
cmake --build build
timeout 1s ./build/server/liteim_server || test $? -eq 124
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R "TcpServer|ReactorInterfaceTest.TcpServer"
```

## 9. 面试表达

### 一句话

我实现了一个主从 Reactor 结构的 TcpServer。

### 展开说

可以这样讲：

> 我实现了一个主从 Reactor 结构的 `TcpServer`。主 Reactor 只负责监听和 accept；每个新连接由 `EventLoopThreadPool` 按 round-robin 分配到一个子 I/O loop；`Session` 在目标 loop 中创建并固定归属该 loop。服务端维护一个线程安全的 session 表，外部线程调用 `sendToSession()` 时不会直接写 socket，而是复用 `Session::sendPacket()` 投递回连接所属 loop。第一版业务默认 echo，用来验证多 Reactor 网络底座。

生命周期边界可以这样补充：

> `TcpServer` 的启动、停止和析构都收紧到 base loop 线程。跨线程只允许投递“请 base loop 调用 stop”这个上层动作，而不是让 `TcpServer::stop()` 自己捕获裸 `this` 异步排队。这样对象在哪个 loop 拥有，就在哪个 loop 停止和析构，生命周期更接近 muduo 的硬边界。

关键点：

- `Acceptor` 不拥有连接生命周期，只交出 accepted fd。
- `UniqueFd` 表达 accepted fd 所有权，避免裸 fd 泄漏。
- `Session` 必须在所属 I/O loop 创建和启动。
- `sessions_` 表需要 mutex，因为查询和删除可能来自不同线程；表 key 是逻辑 session id，不是 fd。
- 跨线程发送必须回到 session 所属 I/O loop。
- Step 16 只验证网络底座，不把 MySQL / Redis 阻塞调用放进 I/O 线程。

### 容易被追问

- 为什么主 Reactor 不直接处理所有连接读写？
- 为什么 Session 要在子 I/O loop 中创建？
- 为什么 sendToSession() 可以跨线程调用？
- 为什么不用 fd 当 session_id？
- 为什么现在不暴露 sendToUser()？
- 为什么默认业务是 echo？

## 10. 面试常见追问

### 为什么主 Reactor 不直接处理所有连接读写？

主 Reactor 如果既 accept 又读写所有连接，很容易成为瓶颈。多 Reactor 模型让主 Reactor 专注接入，子 Reactor 处理连接 I/O。

### 为什么 Session 要在子 I/O loop 中创建？

`Session` 内部有 `Channel`，`Channel` 必须注册到所属 loop 的 `Epoller`。如果在 base loop 创建并注册到子 loop，会破坏 one-loop-per-thread 边界。

### 为什么 sendToSession() 可以跨线程调用？

因为它不直接写 fd。它只根据逻辑 session id 找到 `Session`，然后调用 `Session::sendPacket()`；`sendPacket()` 会通过 `queueInLoop()` 把发送任务投递回连接所属 loop。

### 为什么不用 fd 当 session_id？

fd 是内核资源编号，关闭后可能马上复用。如果旧连接的 remove 回调排队较晚，而新连接刚好拿到同一个 fd，旧 remove 就可能误删新 session。逻辑 session id 单调递增，能把“socket fd”和“连接身份”分开。

### 为什么现在不暴露 sendToUser()？

当前还没有登录业务，也没有 user 和 session 的绑定关系。保留一个永远返回 `NotFound` 的占位 public API 会误导调用方以为用户路由已经存在，所以先删除；等登录态和绑定表实现后，再引入真实 `sendToUser()`。

### 为什么默认业务是 echo？

echo 足够验证网络底座：连接接入、Packet 解码、输出编码、跨线程唤醒和写事件都必须正常，才能把同一个 Packet 发回客户端。
