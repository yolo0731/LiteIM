# Step 14: Session

本 Step 开始把前面的网络积木串到一个真实连接上。

`Acceptor` 只负责监听和 `accept4()`，它拿到的是“新连接 fd”。`Session` 负责通过 `UniqueFd` 接管这个已连接 fd，并在所属 `EventLoop` 中完成后续读写：

```text
connected fd
    -> Session
        -> Channel
        -> input Buffer -> FrameDecoder -> Packet callback
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

## 3. Session 的职责

`Session` 持有：

- `EventLoop* loop_`：连接所属的 I/O loop。
- `UniqueFd fd_`：已连接 socket fd 的 RAII owner。
- `std::uint64_t id_`：由 `TcpServer` 分配的逻辑连接 id。
- `std::unique_ptr<Channel> channel_`：fd 的 epoll 事件代理。
- `FrameDecoder decoder_`：把 TCP 字节流解成完整 Packet。
- `Buffer input_buffer_`：保存本轮从 socket 读到的字节。
- `Buffer output_buffer_`：保存还没写完的待发送字节。
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
    -> input Buffer
    -> FrameDecoder
    -> messageCallback(Session, Packet)
```

`handleRead()` 的规则：

- `read() > 0`：追加到输入缓冲区，再喂给 `FrameDecoder`；只有成功解出完整入站 Packet 后才更新活跃时间。
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

Step 17 后的 review hardening 已补上第一版高水位保护：`sendEncodedInLoop()` 在 append 前检查 `output_buffer_.readableBytes() + encoded.size()`，超过 `kSessionOutputHighWaterMark` 也就是 4MB 时直接 `closeInLoop()`。这不是复杂限流，而是最基础的慢客户端保护，防止客户端长期不读导致服务端内存无限增长。

## 7. 关闭路径

`close()` 会回到所属 loop 执行 `closeInLoop()`：

```text
removeChannel()
close fd
clear input/output buffer
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

其中输出缓冲区高水位关闭策略已在 Step 17 后 review hardening 中补齐；心跳、业务路由和更细的限流策略仍属于后续 Step。

## 9. 测试

新增测试：

- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `SessionTest.CompletePacketInvokesMessageCallback`
- `SessionTest.HalfPacketDoesNotInvokeMessageCallback`
- `SessionTest.StickyPacketsInvokeCallbackForEachPacket`
- `SessionTest.PeerCloseInvokesCloseCallback`
- `SessionTest.SendPacketFromOtherThreadDeliversEncodedPacket`
- `SessionTest.LargePacketLeavesPendingOutputWhenPeerDoesNotRead`
- `SessionTest.CloseWhenPendingOutputExceedsHighWaterMark`
- `SessionTest.LastActiveTimeIsInitialized`
- `SessionTest.PendingOutputBytesRequiresOwnerLoopThread`

测试重点：

- 完整包能触发 message callback。
- 半包不会触发 callback。
- 粘包会按顺序触发多次 callback。
- 对端关闭能触发 close callback。
- 其他线程调用 `sendPacket()` 能投递回 owner loop 并成功写出。
- 大包在 peer 不读取时会留下 pending output，证明 output buffer 能接住未写完数据。
- pending output 超过 4MB 时会关闭连接，证明慢客户端不会无限堆积内存。
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

> 我把 `Session` 作为单连接 owner。`Acceptor` 只负责接收新连接，拿到 connected fd 后交给后续 `TcpServer`，再由 `Session(EventLoop*, UniqueFd, id)` 通过 RAII 类型接管 fd 生命周期。`Session` 持有 `Channel`、输入/输出 `Buffer` 和 `FrameDecoder`。读事件触发时循环 read 到 `EAGAIN`，把字节交给 `FrameDecoder` 解析完整 Packet；写事件触发时尽量写 output buffer，没写完就继续等下一次 `EPOLLOUT`。跨线程发送不会直接写 fd，而是通过 `EventLoop::queueInLoop()` 回到连接所属 I/O 线程。

生命周期可以这样补充：

> `Session` 用 `shared_ptr` / `weak_ptr` 管理。启动时调用 `Channel::tie()`，事件回调期间会锁住 owner，避免连接对象在回调中被释放。关闭时先从 epoll 删除 channel、关闭 fd、清空缓冲区，然后延迟释放 `Channel` 对象，避免在 `Channel::handleEvent()` 的栈帧里销毁当前 Channel。

慢客户端保护可以这样补充：

> 发送路径不会无限追加 output buffer。每次 append 前先检查当前待发送字节加本次编码包是否超过 4MB，高于上限就关闭连接。这是第一版高水位保护，简单直接，能防止慢客户端或异常网络把服务端内存耗尽。

## 11. 常见追问

**为什么 `sendPacket()` 要支持跨线程？**  
后续业务线程完成 MySQL / Redis 操作后，需要给客户端回包。业务线程不能直接写 socket，必须把发送任务投递回连接所属 I/O loop。

**为什么有 output buffer？**  
非阻塞 socket 可能一次写不完。如果阻塞等待，会卡住整个 I/O 线程；如果直接丢弃，会丢消息。所以剩余字节要留在 output buffer，等下一次可写事件继续发送。

**为什么关闭时不立刻析构 `Channel`？**  
因为关闭可能发生在 `Channel::handleEvent()` 调用的 read/write callback 里。此时当前 `Channel` 的成员函数还没返回，立刻析构它会造成未定义行为。先从 epoll 删除并关闭 fd，再延迟释放对象更安全。

**`FrameDecoder` 和 `Session` 的边界是什么？**  
`FrameDecoder` 只负责“字节流到 Packet”的解析，不读 socket、不管理连接生命周期。`Session` 负责 socket I/O 和连接关闭，并把读到的字节交给 decoder。

**为什么 Step 14 当时不做心跳和高水位？**
Step 14 先把连接读写生命周期打通。心跳依赖 `timerfd` / 定时器管理，仍然留到后续 Step；高水位保护已在 Step 17 后 review hardening 中补齐为 4MB 超限关闭。
