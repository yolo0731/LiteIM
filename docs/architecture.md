# LiteIM Architecture

本文档记录 LiteIM 最终目标架构。当前仓库处于 Step 9，已经有最小 `liteim_server`、`liteim_tests`、`liteim_base`、`liteim_protocol` 协议类型 / Packet / TLV / TCP 字节流解包器，以及 `liteim_net` 的网络缓冲区 `Buffer`、Linux socket 工具函数 `SocketUtil` 和 Reactor 核心接口 `Channel` / `Epoller` / `EventLoop`。当前还没有真实 epoll 系统调用实现、事件循环实现或 `Session` 实现。

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
       -> appendString(type, value, body)
       -> appendUint64(type, value, body)
       -> parseTlvMap(body)
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

当前 Step 10 已经实现 `Epoller` 系统调用层，但 `Channel` 回调分发和 `EventLoop` 阻塞循环仍留给后续步骤：

```text
liteim_net
  -> Buffer
       -> append()
       -> appendString()
       -> readableBytes()
       -> writableBytes()
       -> peek()
       -> retrieve()
       -> retrieveAll()
       -> retrieveAllAsString()
       -> ensureWritableBytes()
  -> SocketUtil
       -> createNonBlockingSocket()
       -> setNonBlocking()
       -> setReuseAddr()
       -> setReusePort()
       -> setTcpNoDelay()
       -> setKeepAlive()
       -> closeFd()
       -> getSocketError()
  -> Channel
       -> fd()
       -> events()
       -> revents()
       -> enableReading()
       -> enableWriting()
       -> setReadCallback()
       -> setWriteCallback()
       -> handleEvent()
  -> Epoller
       -> poll()
       -> updateChannel()
       -> removeChannel()
  -> EventLoop
       -> loop()
       -> quit()
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
- `Channel` 只代表一个 fd 的事件代理，不拥有 fd，不关闭 fd。
- `Channel::events()` 表示想监听的事件，`Channel::revents()` 表示本轮 epoll 实际返回的事件。
- `Epoller` 是 epoll 系统调用封装边界，负责 `epoll_create1(EPOLL_CLOEXEC)`、`epoll_ctl(ADD/MOD/DEL)` 和 `epoll_wait()`。
- `Epoller` 当前使用 LT 模式，不设置 `EPOLLET`。
- `Epoller` 用 `fd -> Channel*` 映射判断 add/mod/del，并通过 `epoll_event.data.ptr` 在 `poll()` 返回时找回活跃 `Channel`。
- `EventLoop` 是 Reactor 调度层接口，本阶段只表达“一个 loop 拥有一个 epoller 并管理 channel”，不实现阻塞循环、任务队列或 `eventfd` 唤醒。
- 后续 `Session` 会持有输入 `Buffer` 和输出 `Buffer`，I/O 线程负责把 socket 字节读入输入缓冲区，再交给 `FrameDecoder`。
- 输出缓冲区高水位回压会在后续慢客户端保护 Step 中实现；当前只提供可复用的缓冲区底座。
- `retrieve()` 越界返回 `InvalidArgument`，避免网络异常输入触发进程级崩溃。

## Current Step

Step 10 implements the real `Epoller` wrapper in `liteim_net`. `Channel::handleEvent()`, automatic `Channel::update()` wiring, `EventLoop` event dispatch, `eventfd` wakeup, `Acceptor`, `Session`, `TcpServer`, and backpressure policy stay in later steps.
