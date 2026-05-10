# Step 14: Session

本 Step 开始把前面的网络积木串到一个真实连接上。

`Acceptor` 只负责监听和 `accept4()`，它拿到的是“新连接 fd”。`Session` 负责通过 `UniqueFd` 接管这个已连接 fd，并在所属 `EventLoop` 中完成后续读写：

```text
connected fd
    -> Session
        -> Channel
        -> FrameDecoder -> Packet callback
        -> output Buffer -> write()
```

## 1. 为什么需要 Session

TCP 连接不是一次函数调用，而是一段持续存在的生命周期：

- 连接 fd 需要被关闭，不能泄漏，也不能重复关闭。
- 读事件来了要循环 `read()` 到 `EAGAIN`。
- TCP 是字节流，读到的数据可能是半包，也可能是多个包粘在一起。
- 业务层要看到完整 `Packet`，不能直接处理裸字节。
- 发送时可能一次写不完，剩余数据必须留在 output buffer，等下一次 `EPOLLOUT`。
- 其他线程发消息时不能直接写 fd，必须投递回连接所属 I/O loop。

所以 `Session` 是“一个连接的 owner”。它不是业务服务，不做登录、聊天、MySQL 或 Redis。

## 2. 新增文件

```text
include/liteim/net/Session.hpp
src/net/Session.cpp
tests/net/session_header_test.cpp
tests/net/session_test.cpp
tutorials/step14_session.md
```

同时更新：

- `src/net/CMakeLists.txt`：把 `Session.cpp` 加入 `liteim_net`，并让 `liteim_net` 链接 `liteim_protocol`。
- `src/CMakeLists.txt`：构建顺序改成 `base -> protocol -> net`，因为 `Session` 需要 `Packet` / `FrameDecoder`。
- `tests/CMakeLists.txt`：加入 Session 测试。

## Session.hpp 接口说明

`Session.hpp` 定义单个已连接 TCP fd 的 owner。

类型、常量和构造：

- `kSessionDefaultOutputHighWaterMark = 4 * 1024 * 1024`，默认输出缓冲高水位。
- `SessionState` 表达连接生命周期：`kNew -> kStarted -> kClosing -> kClosed`。
- `Session::Ptr = std::shared_ptr<Session>`，连接生命周期由 shared_ptr 管理。
- `MessageCallback` 在完整 Packet 到达时执行。
- `CloseCallback` 在连接关闭时执行。
- `Session(EventLoop* loop, UniqueFd fd, std::uint64_t id = 0)` 在指定 owner loop 中接管已连接 fd。`loop == nullptr` 或 fd 无效会抛异常；构造中会确保 fd 非阻塞。

状态查询：

- `id()` 返回逻辑 session id，避免 fd 复用影响连接表。
- `ownerLoop()` 返回连接归属的 EventLoop。
- `closed()` 查询是否已经关闭。
- `outputHighWaterMark()` 返回当前输出高水位。
- `pendingOutputBytes()` 返回输出缓冲待写字节，必须在 owner loop 线程调用。
- `lastActiveTimeMilliseconds()` 返回最近一次完整入站 Packet 的时间。

配置和回调：

- `setMessageCallback()` 安装上层消息处理入口。
- `setCloseCallback()` 安装关闭通知入口。
- `setOutputHighWaterMark()` 设置输出高水位，要求 owner loop 线程，拒绝 0。

运行接口：

- `start()` 通过 `runInLoop()` 回到 owner loop，设置 Channel 回调并开启读事件。
- `sendPacket(packet)` 先编码 Packet；编码失败返回错误；成功后同线程直接追加输出缓冲，跨线程投递到 owner loop。
- `close()` 通过 `runInLoop()` 回到 owner loop 执行关闭。

关键 private 成员：

- `loop_` 是线程归属边界。
- `fd_` 是 socket fd owner。
- `channel_` 是 epoll 事件代理，不拥有 fd。
- `decoder_` 处理 TCP 半包和粘包。
- `output_buffer_` 保存待写出字节。
- `output_high_water_mark_` 支撑慢客户端保护。
- `state_` 管理启动/关闭状态，`closed_` 保留给跨线程只读判断，`channel_registered_` 只表示是否已注册到 epoll。
- `last_active_time_ms_` 支撑 heartbeat timeout。

关键 private helper：

- `startInLoop()` 安装 Channel 回调、tie owner 并注册读事件。
- `sendEncodedInLoop()` 检查高水位、追加 output buffer、开启写事件。
- `handleRead()` 循环读 socket、喂给 `FrameDecoder`、派发 Packet。
- `handleWrite()` 尽量写出 output buffer，写空后关闭写关注。
- `closeInLoop()` 移除 Channel、关闭 fd、清空输出缓冲并触发 close callback。
- `updateLastActiveTime()` 只在完整入站 Packet 后刷新。

## Session 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

`Session` 表示一个已经 accept 的 TCP 连接。`TcpServer` 在目标 I/O loop 创建它；它负责连接 fd 的读写、Packet 编解码、输出缓冲、高水位保护、活跃时间和关闭回调。业务线程不能直接操作它的 fd 或 Buffer，只能通过 `sendPacket()` 投递回 owner loop。

### 2. 上下层调用连接

```text
Acceptor
    -> TcpServer::handleNewConnection(UniqueFd)
    -> TcpServer::createSessionInLoop(io_loop)
    -> Session(owner_loop, UniqueFd, id)
    -> Channel / FrameDecoder / output Buffer
    -> MessageCallback(Session, Packet)
    -> TcpServer / business service
```

发送方向：

```text
business thread or I/O thread
    -> Session::sendPacket()
    -> encodePacket()
    -> runInLoop / queueInLoop
    -> sendEncodedInLoop()
    -> output_buffer_ / Channel EPOLLOUT
    -> handleWrite()
```

### 3. 整体运行链路

1. `TcpServer` 把 accepted `UniqueFd` move 给 [Session 构造函数](../src/net/Session.cpp#L24)。
2. `Session` 兜底设置 fd 非阻塞，创建连接 `Channel`。
3. [start()](../src/net/Session.cpp#L87) 捕获 `shared_from_this()`，把 [startInLoop()](../src/net/Session.cpp#L114) 放到 owner loop。
4. `startInLoop()` 设置 Channel 回调、`tie()` 生命周期保护并开启读事件。
5. fd 可读时，[handleRead()](../src/net/Session.cpp#L181) 循环 read 到 `EAGAIN`。
6. `handleRead()` 调用 `FrameDecoder::feed()` 得到完整 packets。
7. 每个完整入站 Packet 刷新 `last_active_time_ms_` 并调用 message callback。
8. 发送时，[sendPacket()](../src/net/Session.cpp#L92) 先调用 `encodePacket()`，再根据线程决定立即执行或排队。
9. [sendEncodedInLoop()](../src/net/Session.cpp#L152) 检查高水位、append 输出 Buffer、开启写事件。
10. fd 可写时，[handleWrite()](../src/net/Session.cpp#L227) 尽量 write 并 retrieve 已写字节。
11. 关闭时，[closeInLoop()](../src/net/Session.cpp#L263) 移除 Channel、释放 fd、清输出 Buffer、通知 close callback。

### 4. 自身内部运行流程

整体可以看成 5 步：启动、读、发、写、关。

核心成员职责：

- `loop_` 是 owner I/O loop。
- `fd_` 是连接 fd 的 `UniqueFd` owner。
- `channel_` 代理 fd 事件，不拥有 fd。
- `decoder_` 保存输入半包/粘包状态。
- `output_buffer_` 保存非阻塞写未完成的 bytes。
- `output_high_water_mark_` 是单连接输出缓冲上限。
- `last_active_time_ms_` 只表示完整入站 Packet 活跃时间。
- `state_` 表达 `kNew`、`kStarted`、`kClosing`、`kClosed` 生命周期。
- `closed_` 用于 `TcpServer` heartbeat/base loop 的跨线程关闭判断。
- `channel_registered_` 只跟踪 epoll 注册状态，避免重复 remove。

核心函数流程：

- `start()`：要求对象已由 `shared_ptr` 管理，投递到 owner loop。
- `handleRead()`：循环 read，交给 decoder，逐个派发 Packet。
- `sendPacket()`：编码 Packet，并通过 `runInLoop()` / `queueInLoop()` 回到 owner loop。
- `sendEncodedInLoop()`：append 前做 `pending + incoming` 高水位检查，超限直接关闭。
- `handleWrite()`：write 可读区，成功后 retrieve，清空后 disable writing。
- `closeInLoop()`：用 `closed_.exchange(true)` 保证一次性关闭，把 `state_` 推到 closing/closed，移除 Channel、释放 fd、延迟 reset Channel、触发 close callback。

读路径可以理解成：

```text
Channel 读事件
    ↓
Session::handleRead()
    ↓
非阻塞 read() 把字节读入临时缓冲
    ↓
FrameDecoder::feed() 处理半包 / 粘包
    ↓
完整且合法 Packet 刷新 last_active_time
    ↓
MessageCallback(shared_from_this(), packet)
```

边界处理是：peer close 进入 `closeInLoop()`；`EINTR` 继续读；`EAGAIN` 表示本轮读完；协议解析失败关闭连接；callback 关闭当前 Session 后不继续派发剩余 packet。

写路径可以理解成：

```text
业务调用 Session::sendPacket()
    ↓
encodePacket() 生成 Bytes
    ↓
runInLoop() / queueInLoop() 回到 owner loop
    ↓
Session::sendEncodedInLoop()
    ↓
append 到 output_buffer_
    ↓
Channel 开启写事件，由 handleWrite() 慢慢写出
```

回压路径可以理解成：

```text
sendEncodedInLoop() 准备追加 encoded bytes
    ↓
读取 output_buffer_ 当前 pending bytes
    ↓
与 output_high_water_mark_ 比较
    ↓
超过高水位时记录 warning
    ↓
closeInLoop() 关闭慢连接
```

这里最重要的线程边界是：`output_buffer_`、`Channel` 和 fd 都只在 Session owner loop 中直接操作，业务线程只能通过 `sendPacket()` 投递。

### 5. 小例子和边界

小例子：业务线程拿到一个 `Session::Ptr` 后调用 `sendPacket()`。如果它不在 I/O loop 线程，`sendPacket()` 不会直接 `write(fd)`，而是把 `sendEncodedInLoop()` 投递回 owner loop，避免跨线程操作 `output_buffer_` 和 `Channel`。

边界：`shared_from_this()` 要求 `Session` 已经由 `shared_ptr` 管理；`pendingOutputBytes()` 只能 owner-loop 查询；服务端出站写不刷新活跃时间，只有完整入站 Packet 才算客户端活跃；Channel 不拥有 fd，关闭顺序是先从 EventLoop 移除 Channel，再释放 `UniqueFd`。

## 3. Session 的职责

`Session` 持有：

- `EventLoop* loop_`：连接所属的 I/O loop。
- `UniqueFd fd_`：已连接 socket fd 的 RAII owner。
- `std::uint64_t id_`：由 `TcpServer` 分配的逻辑连接 id。
- `std::unique_ptr<Channel> channel_`：fd 的 epoll 事件代理。
- `FrameDecoder decoder_`：把 TCP 字节流解成完整 Packet。
- `Buffer output_buffer_`：保存还没写完的待发送字节。
- `SessionState state_`：收敛启动/关闭状态。
- `message_callback_`：完整 Packet 到达时通知上层。
- `close_callback_`：连接关闭时通知上层。
- `last_active_time_ms_`：记录最近一次完整入站 Packet 活跃时间。

这几个成员的边界很重要：

- `Channel` 不拥有 fd。
- `UniqueFd` 拥有 fd。
- `Session(EventLoop*, UniqueFd, id)` 通过 move 接管 fd；构造失败时 `UniqueFd` 仍会按 RAII 关闭 fd。
- `id_` 只表达连接身份，不参与 socket I/O，避免 fd 复用影响 session 表。
- `FrameDecoder` 不读 socket。
- `Buffer` 不解析协议。
- `Session` 把这些组件组合成一个连接生命周期。

## 4. 生命周期模型

`Session` 继承 `std::enable_shared_from_this<Session>`。调用方必须用 `std::shared_ptr<Session>` 管理它，然后再调用 `start()`。

`start()` 会在所属 loop 中执行 `startInLoop()`：

```cpp
auto self = shared_from_this();
channel_->tie(self);
channel_->setReadCallback(...);
channel_->setWriteCallback(...);
channel_->enableReading();
```

`tie()` 的作用是：事件分发时先锁住 `Session` owner。如果外部引用已经释放，回调不会继续执行；如果 owner 还在，回调期间会有局部 `shared_ptr` 保住它。

`pendingOutputBytes()` 也属于连接内部状态查询，只能在 owner loop 线程调用。它会先执行 `loop_->assertInLoopThread()`，避免测试或业务线程直接跨线程读取 `output_buffer_`。

## 5. 读路径

读路径是：

```text
EPOLLIN
    -> Channel read callback
    -> Session::handleRead()
    -> read(fd) until EAGAIN
    -> FrameDecoder::feed()
    -> messageCallback(Session, Packet)
```

`handleRead()` 的规则：

- `read() > 0`：把本轮读到的临时数组直接喂给 `FrameDecoder`；只有成功解出完整入站 Packet 后才更新活跃时间。
- `read() == 0`：对端关闭，进入 `closeInLoop()`。
- `errno == EINTR`：继续读。
- `errno == EAGAIN || errno == EWOULDBLOCK`：本轮读完，返回。
- 其他错误：关闭连接。

半包时，`FrameDecoder` 内部会缓存不足的字节，不触发 message callback。

粘包时，`FrameDecoder` 会一次输出多个 `Packet`，`Session` 按顺序逐个触发 callback。

## 6. 写路径

发送入口是 `sendPacket()`：

```text
sendPacket(Packet)
    -> encodePacket()
    -> owner EventLoop
    -> output Buffer
    -> enableWriting()
```

如果调用线程不是连接所属 I/O 线程，`sendPacket()` 不直接操作 fd，而是：

```cpp
loop_->queueInLoop([self, encoded = std::move(encoded)]() mutable {
    self->sendEncodedInLoop(std::move(encoded));
});
```

真正写 fd 的地方是 `handleWrite()`：

```text
EPOLLOUT
    -> write(output_buffer_.peek(), output_buffer_.readableBytes())
    -> retrieve(written)
    -> still has bytes: wait for next EPOLLOUT
    -> empty: disableWriting()
```

这就是输出缓冲区存在的原因：非阻塞 socket 写不完是正常情况，不能阻塞 I/O 线程等待客户端慢慢收。

Step 17 后的 review hardening 已补上第一版高水位保护；Step 20 把它收敛成可配置策略：`sendEncodedInLoop()` 在 append 前检查 `output_buffer_.readableBytes() + encoded.size()`，超过 `output_high_water_mark_` 时记录日志并 `closeInLoop()`。默认值仍是 4MB，对应 `kSessionDefaultOutputHighWaterMark`。

## 7. 关闭路径

`close()` 会回到所属 loop 执行 `closeInLoop()`：

```text
removeChannel()
close fd
clear output buffer
close callback
defer Channel destruction
```

注意：`Channel` 对象不能在 `Channel::handleEvent()` 正在执行时被析构。  
因此 `Session` 在关闭时先从 `Epoller` 删除 channel 并关闭 fd，但把 `channel_.reset()` 延迟到当前事件回调栈帧之后执行。

这个细节对应 Step 13 hardening 里的 callback 契约：callback 不能在执行中直接销毁正在运行的 `Channel`。

## 8. 本 Step 不做什么

本 Step 不实现：

- `TcpServer`。
- `EventLoopThread` / `EventLoopThreadPool`。
- session 表。
- 登录、聊天、业务路由。
- MySQL / Redis。
- 心跳超时。
- 输出缓冲区高水位关闭策略。

其中输出缓冲区高水位关闭策略已在 Step 17 后 review hardening 中补齐基础版本，并在 Step 20 扩展为可配置阈值；心跳、业务路由和更细的限流策略仍属于后续 Step。

## 9. 测试

新增测试：

- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `SessionTest.CompletePacketInvokesMessageCallback`
- `SessionTest.HalfPacketDoesNotInvokeMessageCallback`
- `SessionTest.SplitPacketAcrossReadsInvokesCallbackAfterSecondRead`
- `SessionTest.StickyPacketsInvokeCallbackForEachPacket`
- `SessionTest.PeerCloseInvokesCloseCallback`
- `SessionTest.MalformedPacketClosesSession`
- `SessionTest.SendPacketFromOtherThreadDeliversEncodedPacket`
- `SessionTest.LargePacketLeavesPendingOutputWhenPeerDoesNotRead`
- `SessionTest.CloseWhenPendingOutputExceedsHighWaterMark`
- `SessionTest.LastActiveTimeIsInitialized`
- `SessionTest.PendingOutputBytesRequiresOwnerLoopThread`
- header static assertions 覆盖 `SessionState` 存在，并确认 public `fd()` 已删除。

测试重点：

- 完整包能触发 message callback。
- 半包不会触发 callback。
- 分两次 read 收到的同一包能在第二段到达后触发 callback。
- 粘包会按顺序触发多次 callback。
- 对端关闭能触发 close callback。
- malformed Packet 会触发关闭路径。
- 其他线程调用 `sendPacket()` 能投递回 owner loop 并成功写出。
- 大包在 peer 不读取时会留下 pending output，证明 output buffer 能接住未写完数据。
- pending output 超过配置的输出高水位时会关闭连接，证明慢客户端不会无限堆积内存。
- `last_active_time` 初始化为有效时间戳。
- 服务端持续写出不会刷新 heartbeat 活跃时间；只有客户端入站完整 Packet 会续期。
- 非 owner 线程直接调用 `pendingOutputBytes()` 会触发线程归属错误，证明 `Buffer` 不被跨线程读取。

运行：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Session"
ctest --test-dir build --output-on-failure
```

## 10. 面试时怎么讲

可以这样说：

> 我把 `Session` 作为单连接 owner。`Acceptor` 只负责接收新连接，拿到 connected fd 后交给后续 `TcpServer`，再由 `Session(EventLoop*, UniqueFd, id)` 通过 RAII 类型接管 fd 生命周期。`Session` 持有 `Channel`、`FrameDecoder` 和输出 `Buffer`。读事件触发时循环 read 到 `EAGAIN`，把本轮读到的字节直接交给 `FrameDecoder` 解析完整 Packet；写事件触发时尽量写 output buffer，没写完就继续等下一次 `EPOLLOUT`。跨线程发送不会直接写 fd，而是通过 `EventLoop::queueInLoop()` 回到连接所属 I/O 线程。

生命周期可以这样补充：

> `Session` 用 `shared_ptr` / `weak_ptr` 管理。启动时调用 `Channel::tie()`，事件回调期间会锁住 owner，避免连接对象在回调中被释放。关闭时先从 epoll 删除 channel、关闭 fd、清空输出缓冲区，然后延迟释放 `Channel` 对象，避免在 `Channel::handleEvent()` 的栈帧里销毁当前 Channel。

慢客户端保护可以这样补充：

> 发送路径不会无限追加 output buffer。每次 append 前先检查当前待发送字节加本次编码包是否超过配置的输出高水位，默认是 4MB；高于上限就记录日志并关闭连接。这是第一版高水位保护，简单直接，能防止慢客户端或异常网络把服务端内存耗尽。

## 11. 常见追问

**为什么 `sendPacket()` 要支持跨线程？**  
后续业务线程完成 MySQL / Redis 操作后，需要给客户端回包。业务线程不能直接写 socket，必须把发送任务投递回连接所属 I/O loop。

**为什么有 output buffer？**  
非阻塞 socket 可能一次写不完。如果阻塞等待，会卡住整个 I/O 线程；如果直接丢弃，会丢消息。所以剩余字节要留在 output buffer，等下一次可写事件继续发送。

**为什么关闭时不立刻析构 `Channel`？**  
因为关闭可能发生在 `Channel::handleEvent()` 调用的 read/write callback 里。此时当前 `Channel` 的成员函数还没返回，立刻析构它会造成未定义行为。先从 epoll 删除并关闭 fd，再延迟释放对象更安全。

**`FrameDecoder` 和 `Session` 的边界是什么？**  
`FrameDecoder` 只负责“字节流到 Packet”的解析，不读 socket、不管理连接生命周期。`Session` 负责 socket I/O 和连接关闭，并把读到的字节交给 decoder。

**为什么 Step 14 当时不做心跳和完整高水位配置？**
Step 14 先把连接读写生命周期打通。心跳依赖 `timerfd` / 定时器管理，仍然留到后续 Step；高水位保护先在 Step 17 后 review hardening 中补齐为默认 4MB 超限关闭，再在 Step 20 扩展为可配置阈值、超限日志和 TcpServer 级测试。
