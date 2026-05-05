# LiteIM Architecture

本文档记录 LiteIM 最终目标架构。当前仓库处于 Step 0，只建立目录和文档，不包含服务端实现。

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

Step 0 only resets the workspace. Real architecture code starts from Step 1.
