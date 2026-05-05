# LiteIM

LiteIM 是一个从 Step 0 重新开始推进的 C++17 高性能即时通讯系统。项目主线是 C++ 后端、Linux 网络编程和高性能服务器开发；Qt Widgets 客户端用于后期演示真实聊天效果，PersonaAgent 后续作为普通 Bot 用户接入 LiteIM。

权威总方案见：[`../PROJECT_MEMORY.md`](../PROJECT_MEMORY.md)。如果本文档与 `PROJECT_MEMORY.md` 冲突，以 `PROJECT_MEMORY.md` 为准。

## 当前状态

当前处于 `Step 0: reset workspace`。

Step 0 的目标是“清空旧路线，留下最小起点”，不是提前搭完整项目文件夹。

本 Step 已完成：

- 删除旧的网络层、协议层、业务层、测试、教程和 build 产物。
- 删除 SQLite / `InMemoryStorage` 路线相关文件。
- 删除未来 Step 才会用到的空目录和 `.gitkeep`。
- 保留 `.gitignore`、`LICENSE`、README、计划文件、Step 0 docs 和 Step 0 tutorial。
- `CMakeLists.txt` 只保留 Step 0 空骨架；真正的 server/test target 从 Step 1 开始添加。

下一步是 `Step 1: init CMake project structure`。

## 为什么不保留 .gitkeep

`.gitkeep` 不是 Git 必需文件。Git 本身不跟踪空目录，所以很多项目会放一个 `.gitkeep` 让空目录能被提交。

但 LiteIM 是教学式项目，目录应该随着每个 Step 的真实需求逐步创建。提前提交大量空目录会让 Step 边界变模糊，所以 Step 0 不保留 `.gitkeep`。

## 项目目标

最终目标是一套可以演示、可以压测、可以用于简历讲解的 IM 系统：

```text
Qt/CLI/User Client
    -> LiteIM C++ IM Server
    -> Python BotClient
    -> PersonaAgent LangGraph AgentService
    -> Python BotClient
    -> LiteIM
    -> Qt/CLI/User Client
```

LiteIM 服务端最终负责：

- TCP 长连接接入
- TLV 二进制协议编解码
- 半包 / 粘包处理
- 登录注册
- 好友关系
- 私聊
- 群聊
- 离线消息
- 历史消息
- 心跳超时
- 慢客户端回压
- Bot 用户路由

## 技术定位

LiteIM 的简历表达重点是：

> 基于 C++17 / Linux epoll 实现主从 Reactor 高性能 IM 服务端，I/O 线程负责非阻塞网络读写和协议编解码，业务线程池负责 MySQL / Redis 阻塞任务；通过 Qt 客户端和 Python BotClient 展示真实聊天闭环，并为 PersonaAgent 接入预留统一 Bot 用户入口。

核心技术点：

- C++17 / CMake
- Linux nonblocking socket
- epoll LT
- eventfd 跨线程唤醒
- one-loop-per-thread Reactor
- 主从 Reactor `TcpServer`
- business `ThreadPool`
- timerfd 心跳超时
- signalfd 优雅退出
- 自定义 TLV 二进制协议
- TCP 流式解码器
- `shared_ptr` / `weak_ptr` 管理连接生命周期
- 输出缓冲区高水位回压
- MySQL 连接池、RAII `ConnectionGuard`、prepared statement、DAO
- Redis 在线状态、每会话未读计数、登录失败限流
- GoogleTest / CTest
- Python E2E 测试
- 压测工具
- GitHub Actions CI + ASan
- Qt Widgets + `QTcpSocket` 演示客户端

## 目标架构

```text
                         +----------------------+
                         |  Qt Widgets Client   |
                         |  CLI Client          |
                         |  Python BotClient    |
                         +----------+-----------+
                                    |
                              TLV over TCP
                                    |
+------------------------------- LiteIM Server -------------------------------+
|                                                                            |
|  Main Reactor                                                               |
|  - nonblocking listen socket                                                |
|  - epoll accept events                                                      |
|  - dispatch new connections                                                 |
|                                                                            |
|  Sub Reactor Thread Pool                                                    |
|  - one EventLoop per I/O thread                                             |
|  - epoll read/write events                                                  |
|  - eventfd queueInLoop wakeup                                               |
|  - FrameDecoder + Session lifecycle                                         |
|  - output-buffer high-water-mark backpressure                               |
|                                                                            |
|  Business Thread Pool                                                       |
|  - AuthService / ChatService / GroupService / HistoryService                |
|  - MySQL DAO and Redis cache operations                                     |
|  - post responses back to the owning EventLoop                              |
|                                                                            |
+---------------------------+----------------------+-------------------------+
                            |                      |
                         MySQL                  Redis
                   persistent entities      online/unread/rate-limit
```

关键边界：

- I/O 线程不执行 MySQL / Redis 阻塞调用。
- 业务线程不直接修改 `Session`。
- 跨线程发送统一通过 `EventLoop::queueInLoop()` 或 `EventLoop::runInLoop()` 回到连接所属 I/O 线程。
- PersonaAgent 不嵌入 C++ 服务端，只通过 Python BotClient 接入。

## Step 路线

LiteIM 按 `Step 0 + Step 1-54` 推进，每一步都遵循：

```text
concept -> handwritten code -> tests -> commit
```

阶段划分：

| 阶段 | Step | 目标 |
| --- | --- | --- |
| Phase 0 | Step 0 | 清理旧文件夹，保留最小起点。 |
| Phase 1 | Step 1-20 | 高性能网络底座，多 Reactor echo server。 |
| Phase 2 | Step 21-30 | MySQL / Redis 存储缓存层。 |
| Phase 3 | Step 31-40 | 异步 IM 业务闭环和 BotGateway。 |
| Phase 4 | Step 41-45 | CLI、Python E2E、压测、GoogleTest、ASan、CI。 |
| Phase 5 | Step 46-53 | Qt Widgets 常见 IM 三栏客户端。 |
| Phase 6 | Step 54 | README、架构图、Qt 截图、压测报告和面试文档。 |

Qt 客户端放在服务端主线之后实现，目标是演示登录、会话列表、消息气泡、私聊、群聊、历史消息、心跳/断线提示和 AI Bot 联系人入口。Qt 不使用微信名称、logo、图标或素材。

## 当前文件结构

Step 0 后只保留当前步骤真正需要的文件：

```text
LiteIM/
├── .gitignore
├── CMakeLists.txt
├── LICENSE
├── README.md
├── task_plan.md
├── findings.md
├── progress.md
├── docs/
│   ├── architecture.md
│   └── project_layout.md
└── tutorials/
    ├── README.md
    └── step00_reset.md
```

后续目录按 Step 逐步创建，例如 Step 1 才创建 `server/`、`tests/`、`include/liteim/`、`src/` 的最小结构。

## Step 0 验证

Step 0 只有空 CMake 骨架，没有 server/test target。可以运行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

预期结果：CMake configure/build 成功；CTest 当前没有测试用例。

## 当前执行文件

- [`../PROJECT_MEMORY.md`](../PROJECT_MEMORY.md)：总路线。
- [`task_plan.md`](task_plan.md)：当前执行计划。
- [`findings.md`](findings.md)：设计结论和风险记录。
- [`progress.md`](progress.md)：真实完成进度。
- [`tutorials/step00_reset.md`](tutorials/step00_reset.md)：Step 0 教程。
