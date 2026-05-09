# LiteIM Architecture

本文档记录 LiteIM 最终目标架构。当前仓库处于 Step 17 完成状态，已经有最小 `liteim_server`、`liteim_tests`、`liteim_base`、`liteim_protocol` 协议类型 / ByteOrder / Packet / TLV / TCP 字节流解包器，`liteim_net` 的网络缓冲区 `Buffer`、Linux socket 工具函数 `SocketUtil`、RAII fd 包装 `UniqueFd`、`Epoller` 系统调用封装、`Channel` 事件分发 / `tie()` 生命周期保护、`EventLoop` 事件循环 / `eventfd` 任务投递、非阻塞监听器 `Acceptor`、单连接 owner `Session`、多 Reactor 子线程基础 `EventLoopThreadPool` 和多 Reactor echo `TcpServer`，以及 `liteim_concurrency` 的业务 `ThreadPool`。Step 17 后的 review hardening 已补齐 `EventLoopThread` 自线程 stop、`Session` 输出缓冲高水位、`TcpServer` 逻辑 session id 和 `Channel` error+read 分发回归。

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
  -> ByteOrder
       -> appendUint16BE(Bytes, value)
       -> appendUint32BE(Bytes, value)
       -> appendUint64BE(Bytes, value)
       -> readUint16BE(bytes)
       -> readUint32BE(bytes)
       -> readUint64BE(bytes)
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
- `ByteOrder` 是协议层通用网络字节序工具，Packet header 和 TLV header/value 统一复用它，避免 Packet/TLV 各自维护重复的大端读写 helper。
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

当前 Step 16 已经实现 `Epoller` 系统调用层、`Channel` 回调分发、`EventLoop` 阻塞循环、`eventfd` 任务投递、`Acceptor` 非阻塞监听、`Session` 单连接读写、`EventLoopThreadPool` 子 Reactor 线程池以及多 Reactor `TcpServer`：

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
       -> id()
       -> start()
       -> sendPacket()
       -> close()
       -> setMessageCallback()
       -> setCloseCallback()
       -> pendingOutputBytes()
       -> lastActiveTimeMilliseconds()
  -> EventLoopThread
       -> startLoop()
       -> stop()
       -> running()
  -> EventLoopThreadPool
       -> start()
       -> stop()
       -> getNextLoop()
       -> loops()
       -> threadCount()
  -> TcpServer
       -> setMessageCallback()
       -> start()
       -> stop()
       -> port()
       -> sessionCount()
       -> started()
       -> sendToSession()
       -> sendToUser()
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
- `UniqueFd::release()` 用于少数必须手动交出 fd 的场景；默认所有权转移优先用 move 表达。
- Step 13 引入 `UniqueFd` 是因为 `Acceptor` 同时管理长期 listen fd 和临时 accepted fd：listen fd 随 `Acceptor` 关闭，accepted fd 通过 `NewConnectionCallback(UniqueFd, peer)` 移动交给后续 `TcpServer` / `Session`。
- `Channel` 只代表一个 fd 的事件代理，不拥有 fd，不关闭 fd。
- `Channel::events()` 表示想监听的事件，`Channel::revents()` 表示本轮 epoll 实际返回的事件。
- `Channel::handleEvent()` 把 `EPOLLIN` / `EPOLLPRI` / `EPOLLRDHUP` 分发给 read callback，把 `EPOLLOUT` 分发给 write callback，把 `EPOLLHUP` 和 `EPOLLERR` 分发给 close/error callback；如果 `EPOLLERR` 和可读事件同时出现，error callback 执行后仍继续 read callback，避免丢掉内核缓冲中剩余数据。
- `Channel::tie()` 保存 owner 的 `weak_ptr`；事件分发前会先 lock，owner 已释放时跳过回调，owner 仍存在时用局部 `shared_ptr` 保证回调执行期间对象不被销毁。
- `Channel::handleEvent()` 先在本栈帧内 lock `tie()` 保存的 owner，再保留 `revents_` 快照并直接调用 callback，不复制 callback；owner 生命周期由局部 `shared_ptr` guard 保护，减少高频事件路径上的 `std::function` 复制。未 `tie()` 的 `Channel` 必须保证 callback 执行期间不会销毁当前 `Channel`，也不会重置正在执行的 callback。
- `Channel` 修改关注事件后通过所属 `EventLoop::updateChannel()` 交给 `Epoller` 更新 epoll 注册状态。
- `Epoller` 是 epoll 系统调用封装边界，负责 `epoll_create1(EPOLL_CLOEXEC)`、`epoll_ctl(ADD/MOD/DEL)` 和 `epoll_wait()`。
- `Epoller` 构造时如果 `epoll_create1()` 失败会直接抛异常，避免半初始化对象进入 `EventLoop`。
- `Epoller` 当前使用 LT 模式，不设置 `EPOLLET`。
- `Epoller` 用 `fd -> Channel*` 映射判断 add/mod/del，并通过 `epoll_event.data.ptr` 在 `poll()` 返回时找回活跃 `Channel`。
- `Epoller` 会拒绝更新或删除属于其他 `EventLoop` 的 `Channel`，避免一个 fd 事件代理跨 loop 注册，维护 one-loop-per-thread 边界。
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
- `Acceptor::NewConnectionCallback` 把 `UniqueFd` 和 peer address 交给后续 `TcpServer`；如果没有 callback 或 callback 抛异常，局部 `UniqueFd` 会立即关闭 accepted fd，避免泄漏。
- `Acceptor::close()` 可以从非 loop 线程请求关闭；loop 未启动或仍运行时实际 `removeChannel()` 和 listen fd 关闭会回到所属 loop 线程完成。loop 已停止时，或者 close task 已投递但 loop 在执行它之前退出时，会走 fallback 直接释放 `Channel`、listen fd 和 idle fd，避免永久等待 queued task。
- `Acceptor` fd 用尽 helper 保持 `noexcept`，但 warn 日志通过内部 no-throw wrapper 写入，避免 `spdlog` sink 或格式化异常在 `EMFILE` / `ENFILE` 路径触发 `std::terminate()`。
- `Session` 是单个已连接 fd 的 owner，持有 fd、`Channel`、输入 `Buffer`、输出 `Buffer` 和 `FrameDecoder`。
- `Session::id()` 是 `TcpServer` 分配的逻辑连接 id；fd 只用于 socket I/O，不再承担 session 表 key。
- `Session::start()` 必须在 `shared_ptr` 管理下调用；它通过 `Channel::tie()` 把 owner 生命周期绑定到事件分发期间。
- `Session::handleRead()` 在 I/O loop 中循环 `read()` 到 `EAGAIN` / `EWOULDBLOCK`，把读到的字节放进输入 `Buffer`，再交给 `FrameDecoder` 产出完整 `Packet`。
- `Session::sendPacket()` 先编码 Packet；如果从其他线程调用，会通过 `EventLoop::queueInLoop()` 回到所属 I/O loop，再写入输出 `Buffer` 并启用写事件。
- `Session::sendPacket()` 写入 output buffer 前会检查 `kSessionOutputHighWaterMark`。待发送数据超过 4MB 时直接关闭连接，避免慢客户端或网络抖动无限堆积内存。
- `Session::handleWrite()` 在 `EPOLLOUT` 到来时尽量写出 output buffer，写完后关闭写兴趣；没有写完的数据留在 output buffer，后续继续由写事件驱动。
- `Session::close()` 在所属 loop 中移除 `Channel`、关闭 fd、清空缓冲并触发 close callback；`Channel` 的释放被延迟到当前 `handleEvent()` 栈帧之后，避免在回调中销毁正在执行的 `Channel`。
- `Session` 维护 `lastActiveTimeMilliseconds()`，后续 heartbeat / timeout Step 会基于它判断连接活跃度。
- 输出缓冲区当前采用第一版慢客户端保护：超过 4MB 直接关闭连接。后续业务 Step 可以在此基础上增加日志、统计、用户级降级或更细的限流策略。
- `EventLoopThread` 在线程函数内部构造 `EventLoop`，保证 loop 的 `thread_id_` 绑定到真正的 I/O 线程。
- `EventLoopThread::stop()` 只请求 loop 退出并由 owner 线程 join；`loop_` / `running_` 的清理集中在 `threadFunc()` 末尾，避免自线程 stop 后 detach 导致对象生命周期失控。
- `EventLoopThreadPool` 启动指定数量的子 I/O loops，并用 round-robin 为后续连接选择所属 loop；线程数为 0 时返回 base loop，保留单 Reactor 调试模式。
- `TcpServer` 是当前网络层协调器：base loop 持有 `Acceptor`，子 I/O loops 负责连接读写和 Packet 编解码。
- `TcpServer::start()` 在 base loop 线程中启动 I/O 线程池、创建 `Acceptor` 并记录实际监听端口。
- `TcpServer` 从 `Acceptor` 接收 `UniqueFd`，选择一个 I/O loop 后通过 `queueInLoop()` 在目标 loop 中创建 `Session`，避免在 base loop 中直接管理连接 I/O。
- `TcpServer` 用 mutex 保护 `sessions_`，key 是自增逻辑 session id，close callback 触发时从表中删除对应 session，`sessionCount()` 可用于测试和诊断。
- `TcpServer::sendToSession()` 可以从其他线程调用；它按逻辑 session id 在线程安全表里找到 `Session`，真正的发送仍由 `Session::sendPacket()` 回到连接所属 I/O loop。
- `TcpServer::sendToUser()` 当前只提供基础接口并返回 `NotFound`，因为登录态和 user-session 绑定要等后续业务 Step。
- 未设置 `MessageCallback` 时，`TcpServer` 默认 echo 收到的 `Packet`；设置 callback 后由上层业务决定如何响应。
- 当前 `TcpServer` 不执行 MySQL / Redis、不做登录态、不做 MessageRouter；业务线程池接入、业务服务和更完整 backpressure 策略留给后续 Step。
- `retrieve()` 越界返回 `InvalidArgument`，避免网络异常输入触发进程级崩溃。

## Current Concurrency Layer

当前 Step 17 已经实现业务线程池基础设施：

```text
liteim_concurrency
  -> ThreadPool
       -> start()
       -> submit(Task)
       -> stop()
       -> workerCount()
       -> pendingTaskCount()
       -> started()
```

职责边界：

- `ThreadPool` 只负责执行业务任务，不处理 socket、Packet、TLV 或 `Session` 生命周期。
- 固定 worker 数在构造时确定，`start()` 后创建 worker 线程；worker 数为 0 会返回 `InvalidArgument`。
- `submit()` 只接收 `std::function<void()>`，不返回 `future`，避免第一版业务线程池把接口做复杂。
- `ThreadPool` 内部使用单一 `running_` 状态表达是否运行和是否接受新任务；`submit()` 会拒绝空任务和未运行的线程池。
- `stop()` 会停止接收新任务，唤醒 worker，并等待已经入队的任务执行完再退出。
- 如果 `stop()` 从 worker 任务内部调用，当前 worker 不能 join 自己；线程池通过 worker-local 标记识别 self-stop，只发出停止请求并返回，直到 owner 线程后续调用 `stop()` 或析构完成 join 和清理。
- 外部多线程并发调用 `stop()` 时，join/cleanup 阶段由 `stop_mutex_` 串行化，避免多个线程同时 join 同一个 `std::thread`。
- `pendingTaskCount()` 返回还在队列里等待 worker 取走的任务数，不包含正在执行的任务。
- 单个任务抛异常不会杀死 worker 线程；后续业务服务需要自己把业务错误转换成 `Status` 或响应 Packet。
- 后续 MySQL / Redis / 密码哈希 / 历史消息查询会投递到 `ThreadPool`。业务任务完成后，响应仍必须通过 `EventLoop::queueInLoop()` 或 `runInLoop()` 回到连接所属 I/O 线程。

## Current Step

Step 17 implements the fixed-size business `ThreadPool` in `liteim_concurrency`: tasks are submitted through `submit()`, workers sleep on a `condition_variable`, `stop()` drains already queued tasks before exit, and `pendingTaskCount()` exposes queue length for later diagnostics. Step 17 后的 hardening 还补齐了网络层生命周期和慢客户端保护回归。

MySQL / Redis blocking calls, login routing, user-session binding, MessageRouter and heartbeat timeout remain later steps. `Session` 当前已有 4MB 输出缓冲高水位关闭保护，后续会继续扩展成完整业务级 backpressure。
