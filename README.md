# LiteIM

LiteIM 是一个按 Step 手写推进的 C++17 高性能即时通讯服务端项目。当前主线是 Linux nonblocking socket、epoll LT、eventfd、one-loop-per-thread Reactor、业务线程池、timerfd 心跳超时，后续继续接入 signalfd、MySQL、Redis、CLI、Qt Widgets 客户端、benchmark、CI，以及通过 Python BotClient 接入的 PersonaAgent。

项目总路线以 `/home/yolo/jianli/PROJECT_MEMORY.md` 为准；Codex 协作规范以 `/home/yolo/jianli/AGENTS.md` 为准。本仓库对外说明集中在本 README；开发过程记忆只保留在 `task_plan.md`、`findings.md`、`progress.md` 和每个 Step 的 `tutorials/` 文件里。

## Current Status

当前完成到 `Step 18: implement TimerManager + timerfd heartbeat timeout`。

已完成的核心模块：

- `liteim_base`：`Config`、`Logger`、`ErrorCode`、`Status`、`Timestamp`、`Byte` / `Bytes`。
- `liteim_protocol`：`MessageType`、`TlvType`、`ByteOrder`、`Packet`、`TlvCodec`、`FrameDecoder`。
- `liteim_net`：`Buffer`、`SocketUtil`、`UniqueFd`、`Channel`、`Epoller`、`EventLoop`、`Acceptor`、`Session`、`EventLoopThread`、`EventLoopThreadPool`、`TcpServer`。
- `liteim_concurrency`：固定大小业务 `ThreadPool`。
- `liteim/timer`：`TimerHeap` 和 `TimerManager`。

当前 `TcpServer` 已经可以把主 Reactor 的 `Acceptor`、子 Reactor 线程池和 `Session` 串起来，默认 echo 收到的 `Packet`。Step 18 通过 base-loop `TimerManager` 增加 idle session cleanup：默认每 5 秒检查一次，90 秒没有收到完整 `Packet` 的连接会关闭。

下一步是 `Step 19: implement signalfd graceful shutdown`。

## Architecture Boundary

当前目标架构：

```text
Client
  -> LiteIM TCP Server
  -> Session / MessageRouter
  -> Business ThreadPool
  -> MySQL / Redis
```

关键边界：

- I/O 线程只处理 socket 读写、Packet/TLV 编解码和连接生命周期。
- MySQL / Redis 等阻塞工作必须进入业务 `ThreadPool`。
- 业务线程不能直接修改 `Session`；响应必须通过 `EventLoop::queueInLoop()` / `runInLoop()` 回到连接所属 I/O loop。
- `Session` 生命周期使用 `shared_ptr` / `weak_ptr` 保护。
- 慢客户端保护通过输出缓冲区高水位策略实现。
- PersonaAgent 不嵌入 C++ 服务端，而是后续通过 Python BotClient 作为普通 Bot 用户接入 LiteIM。

## Build And Test

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

常用清理检查：

```bash
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

## Repository Layout

```text
LiteIM/
├── CMakeLists.txt
├── README.md
├── task_plan.md
├── findings.md
├── progress.md
├── include/liteim/
│   ├── base/
│   ├── concurrency/
│   ├── net/
│   ├── protocol/
│   └── timer/
├── src/
│   ├── base/
│   ├── concurrency/
│   ├── net/
│   ├── protocol/
│   └── timer/
├── server/
├── tests/
│   ├── base/
│   ├── concurrency/
│   ├── net/
│   ├── protocol/
│   └── timer/
└── tutorials/
```

目录约定：

- 头文件放在 `include/liteim/<module>/`。
- 库实现放在 `src/<module>/`。
- 可执行入口放在 `server/`，后续 CLI / Qt / benchmark 分别放到 `client_cli/`、`client_qt/`、`bench/`。
- 不再维护仓库内 `docs/` markdown；公开说明放 README，路线放 `/home/yolo/jianli/PROJECT_MEMORY.md`，过程记忆放 `task_plan.md`、`findings.md`、`progress.md`。

## Development Notes

每个 LiteIM Step 按固定节奏推进：

```text
concept -> handwritten code -> tests -> commit
```

当前工作记忆：

- `task_plan.md`：只记录当前完成位置、下一步和本次活跃任务。
- `findings.md`：只记录仍然有效的设计结论、风险和边界。
- `progress.md`：只记录当前阶段的真实变更、验证结果和提交状态。
- `tutorials/`：保留每个 Step 的教学解释、测试说明和面试复盘。

旧路线的 SQLite、`InMemoryStorage`、单 Reactor 业务基线不再是主线；这些名字不应该重新出现在当前实现路径中。
