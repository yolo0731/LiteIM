# LiteIM Roadmap

本文件是仓库内可公开阅读的路线摘要。开发时的工作区总方案仍由上层工作区维护；仓库 README 只链接本文件，避免依赖仓库外路径。

## Positioning

LiteIM 的目标是实现一个 C++17 高性能即时通讯服务端：

- Linux nonblocking socket。
- epoll LT。
- eventfd 跨线程唤醒。
- one-loop-per-thread Reactor。
- 主从 Reactor `TcpServer`。
- business `ThreadPool` 隔离 MySQL / Redis 阻塞调用。
- 自定义 TLV 二进制协议。
- TCP 半包 / 粘包处理。
- `shared_ptr` / `weak_ptr` 管理连接生命周期。
- 输出缓冲区高水位回压。
- MySQL 持久化用户、好友、群组、消息和离线消息。
- Redis 管理在线状态、未读计数和登录失败限流。
- GoogleTest / CTest、Python E2E、benchmark、ASan 和 CI。
- Qt Widgets demo client。
- PersonaAgent 通过 Python BotClient 作为普通 Bot 用户接入。

## Current State

当前仓库处于 Step 13 review hardening 后：

- `liteim_base` 已提供 `Config`、`Logger`、`ErrorCode`、`Status` 和 `Timestamp`。
- `liteim_protocol` 已提供 `MessageType`、`TlvType`、`Packet`、`TlvCodec` 和 `FrameDecoder`。
- `liteim_net` 已提供 `Buffer`、`SocketUtil`、`UniqueFd`、`Channel`、`Epoller`、`EventLoop` 和 `Acceptor`。
- `Acceptor` 可以创建非阻塞 listen socket，注册到 `EventLoop`，并用 `accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)` 循环接收新连接。
- `Acceptor::close()` 的清理会回到所属 loop 线程执行，避免 `Epoller` 保留 stale `Channel*`。
- `Channel::tie()` 已支持用 `weak_ptr` 保护后续 `Session` / `TcpConnection` owner 生命周期。

当前还没有 `Session`、`TcpServer`、`EventLoopThreadPool`、业务线程池、MySQL、Redis、CLI、Qt、benchmark 或 CI。

## Step Roadmap

| Phase | Step | Goal |
| --- | --- | --- |
| Phase 0 | Step 0 | Reset workspace and keep the minimal starting point. |
| Phase 1 | Step 1-20 | Build the high-performance network base and multi-Reactor echo server. |
| Phase 2 | Step 21-30 | Add MySQL / Redis storage and cache foundation. |
| Phase 3 | Step 31-40 | Add async IM services: auth, friends, private chat, group chat, history, heartbeat, BotGateway. |
| Phase 4 | Step 41-45 | Add CLI, Python E2E tests, benchmark, GoogleTest/gMock expansion, ASan/UBSan and CI. |
| Phase 5 | Step 46-53 | Add Qt Widgets demo client with familiar three-column IM layout. |
| Phase 6 | Step 54 | Final README, architecture diagrams, screenshots, benchmark report and interview notes. |

## Architecture Boundary

LiteIM 服务端只负责 IM 服务端能力。PersonaAgent 不嵌入 C++ 进程，而是通过 Python BotClient 使用同一 TLV 协议登录 LiteIM。

```text
Qt/CLI/User Client
    -> LiteIM C++ IM Server
    -> Python BotClient
    -> PersonaAgent AgentService
    -> Python BotClient
    -> LiteIM
    -> Qt/CLI/User Client
```

关键边界：

- I/O 线程不执行 MySQL / Redis 阻塞调用。
- 业务线程不直接修改连接对象。
- 跨线程响应必须通过 `EventLoop::queueInLoop()` 或 `EventLoop::runInLoop()` 回到连接所属 I/O 线程。
- 连接生命周期使用 `shared_ptr` / `weak_ptr`。
- 慢客户端保护通过输出缓冲区高水位策略实现。
