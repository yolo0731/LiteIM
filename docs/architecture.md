# LiteIM Architecture

本文档记录 LiteIM 最终目标架构。当前仓库处于 Step 1，只包含最小 `liteim_server` 和 `liteim_tests`，还没有真正的网络层实现。

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

## Current Step

Step 1 only creates the minimal buildable server/test targets. Real networking architecture starts in later steps.
