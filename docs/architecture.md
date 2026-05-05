# LiteIM Architecture

本文档记录 LiteIM 最终目标架构。当前仓库处于 Step 2，已经有最小 `liteim_server`、`liteim_tests` 和 `liteim_base` 基础公共模块，还没有真正的网络层实现。

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

## Current Step

Step 2 adds reusable base utilities. Real protocol and networking architecture starts in later steps.
