# LiteIM

LiteIM 是一个从 Step 0 重新开始推进的 C++17 高性能即时通讯系统。项目主线是 C++ 后端、Linux 网络编程和高性能服务器开发；Qt Widgets 客户端用于后期演示真实聊天效果，PersonaAgent 后续通过 Python BotClient 作为普通 Bot 用户接入 LiteIM，并在独立 AgentService 中实现 6 节点 LangGraph、Knowledge/Memory/Authorized Style RAG、Persona 和 Safety。

权威总方案见：[`../PROJECT_MEMORY.md`](../PROJECT_MEMORY.md)。如果本文档与 `PROJECT_MEMORY.md` 冲突，以 `PROJECT_MEMORY.md` 为准。

## 当前状态

当前处于 `Step 2: add config logger and error code`。

Step 2 的目标是在 Step 1 的最小 CMake + GoogleTest 工程上，新增 `base` 基础公共模块，给后续网络层、协议层、MySQL / Redis 和 Qt 客户端提供统一的配置、日志、错误码、状态返回和时间戳能力。

本 Step 已完成：

- 根 `CMakeLists.txt` 继续使用 `FetchContent` 接入 GoogleTest v1.14.0，并新增 `spdlog` v1.13.0。
- 新增 `include/liteim/base/` 和 `src/base/`，编译生成 `liteim_base` 静态库。
- 新增 `Config`，支持默认配置和简单 `key=value` 配置文件加载。
- 新增 `Logger`，封装 `spdlog`，统一 LiteIM 日志入口。
- 新增 `ErrorCode` 和 `Status`，避免模块之间返回裸字符串或裸 `bool`。
- 新增 `Timestamp`，提供毫秒时间戳和 UTC ISO-8601 字符串。
- `liteim_server` 链接 `liteim_base`，启动时使用默认配置初始化日志并打印监听地址。
- `tests/base/` 新增 14 个 GoogleTest case；加上 Step 1 smoke test，当前 CTest 共 15 个测试。

本 Step 只创建当前需要的 `include/liteim/base/`、`src/base/` 和 `tests/base/`。仍然不提前创建 `client_cli/`、`client_qt/`、`bench/`、`scripts/`、`docker/` 等后续目录。

下一步是 `Step 3: define MessageType and TLV types`。

## 为什么不保留 .gitkeep

`.gitkeep` 不是 Git 必需文件。Git 本身不跟踪空目录，所以很多项目会放一个 `.gitkeep` 让空目录能被提交。

但 LiteIM 是教学式项目，目录应该随着每个 Step 的真实需求逐步创建。提前提交大量空目录会让 Step 边界变模糊，所以 Step 0 不保留 `.gitkeep`。

## 项目目标

最终目标是一套可以演示、可以压测、可以用于简历讲解的 IM 系统：

```text
Qt/CLI/User Client
    -> LiteIM C++ IM Server
    -> Python BotClient
    -> PersonaAgent six-node LangGraph AgentService
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
- GoogleTest / gMock / CTest
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
- AgentService 内部路线以 6 个核心节点为主：`dialogue_policy`、`retrieve`、`tool_router`、`generate_reply`、`safety_check`、`send_message`。
- RAG 分为 Knowledge、Memory 和 Authorized Style 三类 collection；Style RAG 必须有授权 manifest、脱敏、撤回机制和 SafetyGuard 边界。

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
| Phase 4 | Step 41-45 | CLI、Python E2E、压测、gMock 覆盖率增强、ASan/UBSan、CI。 |
| Phase 5 | Step 46-53 | Qt Widgets 常见 IM 三栏客户端。 |
| Phase 6 | Step 54 | README、架构图、Qt 截图、压测报告和面试文档。 |

Qt 客户端放在服务端主线之后实现，目标是演示登录、会话列表、消息气泡、私聊、群聊、历史消息、心跳/断线提示和 AI Bot 联系人入口。Qt 不使用微信名称、logo、图标或素材。

## PersonaAgent 接入路线

PersonaAgent 是项目二，不嵌入 LiteIM 进程。LiteIM 只需要把 AI 助手抽象为普通 Bot 用户和 BotGateway 路由能力。

新版项目二路线以 `/home/yolo/jianli/PROJECT_MEMORY.md` 为准：

- `PersonaAgent Step 1-6`：Python BotClient、FastAPI `/chat`、协议兼容、登录心跳重连、Echo Bot。
- `PersonaAgent Step 7-20`：FastAPI + 6 节点 LangGraph + Knowledge/Memory/Authorized Style RAG + Persona + Safety + Tool Calling + Checkpoint + Trace + Evaluation。
- LangGraph 第一版只保留 6 个核心节点：`dialogue_policy`、`retrieve`、`tool_router`、`generate_reply`、`safety_check`、`send_message`。
- Authorized Style RAG 是项目二的核心卖点，不是普通 few-shot；聊天样本必须有授权、来源、用途、脱敏、撤回和 persona_id 隔离。
- Bot 对外定位是“授权风格模拟 AI 助手”，不能声称自己是授权对象本人，也不能代表真人做现实承诺。

推荐在 LiteIM Step 41 之后启动项目二，这时 Python BotClient 可以直接参考 CLI 客户端协议流程；最终在 Qt 客户端中打开 AI Assistant 联系人完成演示。

## 当前文件结构

Step 2 后只保留当前步骤真正需要的文件：

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
├── include/
│   └── liteim/
│       └── base/
│           ├── Config.hpp
│           ├── ErrorCode.hpp
│           ├── Logger.hpp
│           ├── Status.hpp
│           └── Timestamp.hpp
├── server/
│   ├── CMakeLists.txt
│   └── main.cpp
├── src/
│   ├── CMakeLists.txt
│   └── base/
│       ├── CMakeLists.txt
│       ├── Config.cpp
│       ├── ErrorCode.cpp
│       ├── Logger.cpp
│       ├── Status.cpp
│       └── Timestamp.cpp
├── tests/
│   ├── base/
│   │   ├── config_test.cpp
│   │   ├── error_code_test.cpp
│   │   ├── logger_test.cpp
│   │   └── timestamp_test.cpp
│   ├── CMakeLists.txt
│   └── test_main.cpp
└── tutorials/
    ├── README.md
    ├── step00_reset.md
    ├── step01_project_init.md
    └── step02_base.md
```

后续目录按 Step 逐步创建。比如 `include/liteim/protocol/` 和 `src/protocol/` 会在 Step 3 需要协议类型时创建。

## Step 2 验证

Step 2 有一个 server target、一个 `liteim_base` library target 和一个 GoogleTest test target。运行：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

预期结果：

```text
[2026-05-05 ...] [info] LiteIM server scaffold is running on 0.0.0.0:9000
```

CTest 应发现并通过 15 个测试：

- `SmokeTest.GoogleTestWorks`
- `ConfigTest.DefaultsContainExpectedValues`
- `ConfigTest.LoadFromFileOverridesConfiguredValues`
- `ConfigTest.MissingValuesKeepDefaults`
- `ConfigTest.MissingFileReturnsNotFound`
- `ConfigTest.UnknownKeyFails`
- `ConfigTest.InvalidPortFails`
- `ErrorCodeTest.ToStringReturnsReadableNames`
- `StatusTest.OkStatusHasOkCode`
- `StatusTest.ErrorStatusCarriesCodeAndMessage`
- `LoggerTest.ParseLogLevelReturnsExpectedLevel`
- `LoggerTest.UnknownLogLevelFallsBackToInfo`
- `LoggerTest.InitCreatesReusableLogger`
- `TimestampTest.NowReturnsPositiveEpochMilliseconds`
- `TimestampTest.Iso8601StringUsesUtcFormat`

## 历史 Step

```text
Step 0: reset workspace
Step 1: init CMake project structure with googletest
```

相关教程：

```text
    ├── step00_reset.md
    ├── step01_project_init.md
    └── step02_base.md
```

## 当前执行文件

- [`../PROJECT_MEMORY.md`](../PROJECT_MEMORY.md)：总路线。
- [`task_plan.md`](task_plan.md)：当前执行计划。
- [`findings.md`](findings.md)：设计结论和风险记录。
- [`progress.md`](progress.md)：真实完成进度。
- [`tutorials/step01_project_init.md`](tutorials/step01_project_init.md)：Step 1 教程。
- [`tutorials/step02_base.md`](tutorials/step02_base.md)：Step 2 教程。
