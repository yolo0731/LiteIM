# LiteIM Architecture

本文档记录 LiteIM 最终目标架构。当前仓库处于 Step 6，已经有最小 `liteim_server`、`liteim_tests`、`liteim_base` 和 `liteim_protocol` 协议类型、Packet 编解码、TLV body 编解码与 TCP 字节流解包器，还没有真正的 socket / epoll 网络层实现。

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

当前 Step 6 已经实现协议层最底层的类型定义、Packet header 编解码、TLV body 编解码和 TCP 字节流解包器：

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
- Step 6 不创建网络 `Buffer`、socket、epoll 或 Reactor；这些从 Step 7 开始进入网络层。

## Current Step

Step 6 adds TCP byte-stream frame decoding for Packet. Network Buffer, socket helpers, epoll, and Reactor start in later steps.
