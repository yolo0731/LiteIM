# LiteIM Architecture

本文档记录 LiteIM 最终目标架构。当前仓库处于 Step 14 后，已经有最小 `liteim_server`、`liteim_tests`、`liteim_base`、`liteim_protocol` 协议类型 / Packet / TLV / TCP 字节流解包器，以及 `liteim_net` 的网络缓冲区 `Buffer`、Linux socket 工具函数 `SocketUtil`、RAII fd 包装 `UniqueFd`、`Epoller` 系统调用封装、`Channel` 事件分发 / `tie()` 生命周期保护、`EventLoop` 事件循环 / `eventfd` 任务投递、非阻塞监听器 `Acceptor` 和单连接 owner `Session`。当前还没有 `TcpServer` 或多 Reactor 线程池实现。

## Target Data Flow

```text
Qt/CLI/User Client
    -> LiteIM TCP Server
    -> Session / MessageRouter
    -> Business ThreadPool
    -> MySQL / Redis
```

PersonaAgent 接入链路：

```text
User Client -> LiteIM -> Python BotClient -> AgentService -> Python BotClient -> LiteIM -> User Client
```

AgentService 内部目标结构：

```text
/chat API
  -> dialogue_policy
  -> retrieve
       -> Knowledge RAG
       -> Memory RAG
       -> Authorized Style RAG
  -> tool_router
  -> generate_reply
  -> safety_check
  -> send_message
```

边界：

- LiteIM 不依赖 LangGraph、LLM SDK、embedding 或 vector DB。
- Python BotClient 复用 LiteIM TLV 协议，证明 LiteIM 协议能扩展到 Bot 用户。
- Authorized Style RAG 的原始授权聊天记录不提交到 Git；PersonaAgent 只提交示例 manifest、脱敏样本和处理逻辑。
- SafetyGuard 必须阻止 bot 声称自己是授权对象本人，或代表真人做现实承诺。

## Thread Model

最终服务端采用：

- main Reactor：负责监听 socket 和 accept。
- sub Reactor pool：每个 I/O 线程一个 `EventLoop`，负责连接读写和协议编解码。
- business ThreadPool：负责 MySQL / Redis 等阻塞业务任务。

约束：

- I/O 线程不执行阻塞数据库或缓存调用。
- 业务线程不直接修改 `Session`。
- 跨线程发送通过 `queueInLoop()` 或 `runInLoop()` 回到连接所属 I/O 线程。

## Current Base Layer

当前 Step 2 已经实现的只是所有后续模块都会复用的 `base` 层：

```text
liteim_base
  -> Config
  -> Logger
  -> ErrorCode
  -> Status
  -> Timestamp
```

职责边界：

- `Config` 只负责读取和保存配置，不创建 socket，不连接 MySQL / Redis。
- `Logger` 只负责统一日志入口，不负责业务状态或错误恢复。
- `ErrorCode` 和 `Status` 只表达函数调用结果，不承载业务实体。
- `Timestamp` 只负责时间表达，不做定时器调度；`timerfd` 和心跳超时会在后续 Step 实现。

这些基础能力会被协议层、网络层、存储层、缓存层、业务层和测试复用，但它们本身不依赖任何上层模块。

## Current Protocol Layer

当前协议层已经实现最底层的类型定义、Packet header 编解码、TLV body 编解码和 TCP 字节流解包器：

```text
liteim_protocol
  -> MessageType
       -> toString(MessageType)
       -> isRequestType(MessageType)
       -> isResponseType(MessageType)
       -> isPushType(MessageType)
  -> TlvType
       -> toString(TlvType)
  -> PacketHeader
  -> Packet
       -> validateHeader(PacketHeader)
       -> encodePacket(Packet)
       -> parseHeader(bytes)
  -> TlvCodec
       -> appendString(type, value, Bytes)
       -> appendUint64(type, value, Bytes)
       -> parseTlvMap(Bytes)
       -> getString(map, type)
       -> getUint64(map, type)
       -> getRepeatedString(map, type)
       -> getRepeatedUint64(map, type)
  -> FrameDecoder
       -> feed(bytes)
       -> bufferedBytes()
       -> hasError()
       -> reset()
```

职责边界：

- `MessageType` 只定义消息类型编号，例如登录请求、私聊请求、群聊推送、Bot 消息和错误响应。
- `TlvType` 只定义 body 里的字段类型编号，例如用户名、密码、消息文本、群组 ID、错误信息和 Persona ID。
- `toString()` 只用于日志、测试和调试，不参与网络字节序转换。
- `isRequestType()` / `isResponseType()` / `isPushType()` 只做类型分类，后续 `MessageRouter`、Qt 客户端和 Python BotClient 会基于这个分类处理请求、响应和服务端推送。
- `PacketHeader` 固定为 20 字节：`magic` 4 字节、`version` 1 字节、`flags` 1 字节、`msg_type` 2 字节、`seq_id` 8 字节、`body_len` 4 字节。
- Header 多字节字段使用网络字节序，保证不同 CPU 字节序上协议结果一致。
- `validateHeader()` 只校验 header 级别约束：`magic`、`version` 和 `body_len <= 1MB`。
- `encodePacket()` 会根据 `body.size()` 写入真实 `body_len`，避免调用方传入不一致的 header 长度。
- `parseHeader()` 只解析 fixed header，不解析完整 body。
- TLV wire format 固定为 `type(2 bytes) + len(4 bytes) + value(len bytes)`。
- `parseTlvMap()` 支持重复字段，同一个 `TlvType` 可以保存多个 value。
- `getString()` / `getUint64()` / `getRepeatedString()` / `getRepeatedUint64()` 负责表达“业务必需字段”，缺失时返回 `NotFound`。
- `FrameDecoder` 不操作 socket，只接收上层传入的字节片段，内部缓存不足一个完整包的半包数据。
- `FrameDecoder` 支持一次输入 0 个、1 个或多个完整 `Packet`。
- 错误 header 会让 `FrameDecoder` 进入 error 状态；后续输入会被拒绝，直到上层关闭连接或调用 `reset()`。
- `FrameDecoder` 不拥有 socket，也不负责连接生命周期；它只处理已经读入内存的字节流。

## Current Network Layer

当前 Step 14 已经实现 `Epoller` 系统调用层、`Channel` 回调分发、`EventLoop` 阻塞循环、`eventfd` 任务投递、`Acceptor` 非阻塞监听和 `Session` 单连接读写，但还没有 `TcpServer` / `EventLoopThreadPool`：

```text
liteim_net
  -> Buffer
       -> append(const Byte*, len)
       -> append(const Bytes&)
       -> append(const std::string&)
       -> readableBytes()
       -> writableBytes()
       -> peek()
       -> retrieve()
       -> retrieveAll()
       -> retrieveAllAsString()
  -> SocketUtil
       -> createNonBlockingSocket()
       -> setNonBlocking()
       -> setReuseAddr()
       -> setReusePort()
       -> setTcpNoDelay()
       -> setKeepAlive()
       -> closeFd()
       -> getSocketError()
  -> UniqueFd
       -> fd()
       -> release()
       -> reset()
  -> Acceptor
       -> setNewConnectionCallback()
       -> listenFd()
       -> port()
       -> listening()
       -> close()
  -> Session
       -> start()
       -> sendPacket()
       -> close()
       -> setMessageCallback()
       -> setCloseCallback()
       -> pendingOutputBytes()
       -> lastActiveTimeMilliseconds()
  -> Channel
       -> fd()
       -> events()
       -> revents()
       -> enableReading()
       -> disableReading()
       -> enableWriting()
       -> disableWriting()
       -> disableAll()
       -> setReadCallback()
       -> setWriteCallback()
       -> setCloseCallback()
       -> setErrorCallback()
       -> tie()
       -> handleEvent()
  -> Epoller
       -> poll()
       -> updateChannel()
       -> removeChannel()
  -> EventLoop
       -> loop()
       -> quit()
       -> runInLoop()
       -> queueInLoop()
       -> updateChannel()
       -> removeChannel()
       -> isInLoopThread()
```

职责边界：

- `Buffer` 只管理内存中的字节，不调用 `read()`、`write()`、`recv()` 或 `send()`。
- `Buffer` 不知道 Packet、TLV、MessageType，也不解析业务数据。
- `Buffer` 不依赖 epoll，不管理 fd，也不做线程同步。
- `SocketUtil` 只封装 Linux socket 常用系统调用，不绑定地址、不监听端口、不 accept 连接。
- `createNonBlockingSocket()` 创建非阻塞 TCP fd，并设置 close-on-exec。
- `setReuseAddr()` / `setReusePort()` / `setTcpNoDelay()` / `setKeepAlive()` 只负责 socket option 的薄封装。
- `closeFd()` 通过引用把关闭后的 fd 置为 `kInvalidFd`，减少同一变量重复关闭的风险。
- `getSocketError()` 读取 `SO_ERROR`，后续非阻塞连接、写事件和连接异常处理会复用。
- `UniqueFd` 是轻量 RAII fd 包装，只表达独占所有权和关闭责任，不隐藏 socket 的业务语义。
- `UniqueFd` 禁止拷贝、允许移动，避免多个 owner 同时以为自己负责关闭同一个 fd。
- `UniqueFd::release()` 用于把 fd 所有权交给上层；如果 callback 抛异常而没有成功接管，局部 `UniqueFd` 会自动关闭 fd。
- Step 13 引入 `UniqueFd` 是因为 `Acceptor` 同时管理长期 listen fd 和临时 accepted fd：listen fd 随 `Acceptor` 关闭，accepted fd 只有在 callback 正常返回后才转交给后续 `TcpServer` / `Session`。
- `Channel` 只代表一个 fd 的事件代理，不拥有 fd，不关闭 fd。
- `Channel::events()` 表示想监听的事件，`Channel::revents()` 表示本轮 epoll 实际返回的事件。
- `Channel::handleEvent()` 把 `EPOLLIN` / `EPOLLPRI` / `EPOLLRDHUP` 分发给 read callback，把 `EPOLLOUT` 分发给 write callback，把 `EPOLLHUP` 和 `EPOLLERR` 分发给 close/error callback。
- `Channel::tie()` 保存 owner 的 `weak_ptr`；事件分发前会先 lock，owner 已释放时跳过回调，owner 仍存在时用局部 `shared_ptr` 保证回调执行期间对象不被销毁。
- `Channel::handleEvent()` 先在本栈帧内 lock `tie()` 保存的 owner，再保留 `revents_` 快照并直接调用 callback，不复制 callback；owner 生命周期由局部 `shared_ptr` guard 保护，减少高频事件路径上的 `std::function` 复制。未 `tie()` 的 `Channel` 必须保证 callback 执行期间不会销毁当前 `Channel`，也不会重置正在执行的 callback。
- `Channel` 修改关注事件后通过所属 `EventLoop::updateChannel()` 交给 `Epoller` 更新 epoll 注册状态。
- `Epoller` 是 epoll 系统调用封装边界，负责 `epoll_create1(EPOLL_CLOEXEC)`、`epoll_ctl(ADD/MOD/DEL)` 和 `epoll_wait()`。
- `Epoller` 构造时如果 `epoll_create1()` 失败会直接抛异常，避免半初始化对象进入 `EventLoop`。
- `Epoller` 当前使用 LT 模式，不设置 `EPOLLET`。
- `Epoller` 用 `fd -> Channel*` 映射判断 add/mod/del，并通过 `epoll_event.data.ptr` 在 `poll()` 返回时找回活跃 `Channel`。
- `EventLoop` 是 Reactor 调度层，持有一个 `Epoller`，在 `loop()` 中等待活跃 `Channel` 并调用 `Channel::handleEvent()`。
- `EventLoop` 内部使用 `eventfd` 作为 wakeup fd，由 `UniqueFd` 管理，并把它包装成内部 `Channel` 注册到 epoll。
- `runInLoop()` 在所属线程直接执行任务，跨线程调用时转入 `queueInLoop()`。
- `queueInLoop()` 用 mutex 保护 pending task 队列，并通过写 `eventfd` 唤醒阻塞中的 loop。
- `EventLoop::loop()` 用 RAII guard 复位运行状态，并捕获单个 `Channel` 回调或 pending task 异常，避免一个业务回调直接终止 I/O loop。
- `EventLoop::isStopped()` 只在 `loop()` 已经进入并退出后为 true。刚构造但尚未进入 `loop()` 的对象不是 stopped，即使提前调用过 `quit()` 也不是 stopped，避免跨线程关闭误走停止后 fallback。
- `EventLoop::loop()` 会先执行已经排队的 pending task，再处理 `quit_` 退出条件；即使 `quit()` 早于第一次 `loop()`，已排队的关闭清理任务仍有机会执行。
- `updateChannel()` / `removeChannel()` 仍要求在 loop 所属线程调用，维持 one-loop-per-thread 边界。
- `Acceptor` 只负责监听 socket 和新连接接收，不创建 `Session`，不做登录、协议解析或业务路由。
- `Acceptor` 构造时创建非阻塞 TCP listen fd，设置 `SO_REUSEADDR` / `SO_REUSEPORT`，完成 bind/listen，并把 listen fd 包装成 `Channel` 注册到所属 `EventLoop`。
- listen fd 可读时，`Acceptor` 使用 `accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)` 循环接收新连接，直到 `EAGAIN` / `EWOULDBLOCK`。
- `Acceptor` 持有一个 `/dev/null` idle fd；当 `accept4()` 遇到 `EMFILE` / `ENFILE` 时，会释放 idle fd、accept 并立即关闭一个 pending connection，再尝试恢复 idle fd，避免 LT 模式 busy loop。
- `Acceptor::NewConnectionCallback` 把已连接 fd 和 peer address 交给后续 `TcpServer`；如果没有 callback，`Acceptor` 会立即关闭 accepted fd，避免泄漏。
- `Acceptor::close()` 可以从非 loop 线程请求关闭；loop 未启动或仍运行时实际 `removeChannel()` 和 listen fd 关闭会回到所属 loop 线程完成，loop 已停止时才走 fallback 直接释放 `Channel`、listen fd 和 idle fd，避免永久等待 queued task。
- `Acceptor` fd 用尽 helper 保持 `noexcept`，但 warn 日志通过内部 no-throw wrapper 写入，避免 `spdlog` sink 或格式化异常在 `EMFILE` / `ENFILE` 路径触发 `std::terminate()`。
- `Session` 是单个已连接 fd 的 owner，持有 fd、`Channel`、输入 `Buffer`、输出 `Buffer` 和 `FrameDecoder`。
- `Session::start()` 必须在 `shared_ptr` 管理下调用；它通过 `Channel::tie()` 把 owner 生命周期绑定到事件分发期间。
- `Session::handleRead()` 在 I/O loop 中循环 `read()` 到 `EAGAIN` / `EWOULDBLOCK`，把读到的字节放进输入 `Buffer`，再交给 `FrameDecoder` 产出完整 `Packet`。
- `Session::sendPacket()` 先编码 Packet；如果从其他线程调用，会通过 `EventLoop::queueInLoop()` 回到所属 I/O loop，再写入输出 `Buffer` 并启用写事件。
- `Session::handleWrite()` 在 `EPOLLOUT` 到来时尽量写出 output buffer，写完后关闭写兴趣；没有写完的数据留在 output buffer，后续继续由写事件驱动。
- `Session::close()` 在所属 loop 中移除 `Channel`、关闭 fd、清空缓冲并触发 close callback；`Channel` 的释放被延迟到当前 `handleEvent()` 栈帧之后，避免在回调中销毁正在执行的 `Channel`。
- `Session` 维护 `lastActiveTimeMilliseconds()`，后续 heartbeat / timeout Step 会基于它判断连接活跃度。
- 输出缓冲区高水位回压会在后续慢客户端保护 Step 中实现；当前 Step 14 只记录 pending output bytes，不主动限流或踢慢客户端。
- `retrieve()` 越界返回 `InvalidArgument`，避免网络异常输入触发进程级崩溃。

## Current Step

Step 14 implements `Session` in `liteim_net`: a single connected fd owner with `Channel`, input/output `Buffer`, `FrameDecoder`, packet callbacks, cross-thread `sendPacket()`, close cleanup, and `last_active_time` tracking. `TcpServer`, `EventLoopThreadPool`, business routing, and high-water-mark backpressure policy stay in later steps.
