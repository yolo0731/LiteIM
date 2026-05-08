# LiteIM

LiteIM 是一个从 Step 0 重新开始推进的 C++17 高性能即时通讯系统。项目主线是 C++ 后端、Linux 网络编程和高性能服务器开发；Qt Widgets 客户端用于后期演示真实聊天效果，PersonaAgent 后续通过 Python BotClient 作为普通 Bot 用户接入 LiteIM，并在独立 AgentService 中实现 6 节点 LangGraph、Knowledge/Memory/Authorized Style RAG、Persona 和 Safety。

仓库内路线摘要见：[`docs/roadmap.md`](docs/roadmap.md)。

## 当前状态

当前处于 `Step 15: implement EventLoopThread and EventLoopThreadPool` 已完成后的 Step 16 准备阶段。

Step 15 的目标是在 `liteim_net` 中实现 one-loop-per-thread 的 I/O 线程基础：`EventLoopThread` 在工作线程里构造并运行一个 `EventLoop`，`EventLoopThreadPool` 启动 N 个子 Reactor，并通过 round-robin 给后续 `TcpServer` 分配连接所属的 I/O loop。

Step 13 已完成 review hardening：新增 `UniqueFd` 表达 fd 所有权，`Acceptor::close()` 的实际清理回到所属 loop 线程执行，accepted fd 在 callback 抛异常时也会被 RAII 自动关闭，`Channel::tie()` 为 `Session` 生命周期保护打基础。第二轮 hardening 继续修复 Logger 级别保持、Epoller 构造失败语义、EventLoop 异常隔离和停止状态、Acceptor fd 用尽处理以及 Channel 回调复制问题。第三轮 hardening 明确 `EventLoop::isStopped()` 只表示 `loop()` 已经进入并退出，避免 loop 尚未启动时误走停止后 fallback；同时补充 Acceptor `noexcept` 日志保护和 Channel callback 不复制契约。

Step 16 前已完成一次代码清理：协议层新增 `ByteOrder.hpp` 统一 Packet/TLV 的大端读写 helper；`Epoller` 开始校验 `Channel` 是否属于同一个 `EventLoop`；`Acceptor::NewConnectionCallback` 改为按值接收 `UniqueFd`，用类型表达 accepted fd 所有权；`Acceptor::listening()` 改由 `listen_fd_` 推导，删除重复状态；测试里的自制 `FdGuard` 已替换为生产 `UniqueFd`。

本 Step 已完成：

- 新增 `liteim_net` 库 target。
- 新增 `Types.hpp`，统一原始网络/协议字节别名 `Byte` / `Bytes`。
- 新增 `Buffer`，支持 `append(const Byte*, len)`、`append(const Bytes&)`、`append(const std::string&)`、`readableBytes()`、`writableBytes()`、`peek()`、`retrieve()`、`retrieveAll()` 和 `retrieveAllAsString()`。
- `Buffer` 使用读写索引维护可读区域和可写区域。
- 空间不足时优先复用已读区域，仍不足时自动扩容。
- `retrieve()` 越界返回 `InvalidArgument`，不让底层网络工具直接触发进程崩溃。
- 新增 `SocketUtil`，支持 `createNonBlockingSocket()`、`setNonBlocking()`、`setReuseAddr()`、`setReusePort()`、`setTcpNoDelay()`、`setKeepAlive()`、`closeFd()` 和 `getSocketError()`。
- `createNonBlockingSocket()` 创建 `AF_INET` / `SOCK_STREAM` / `SOCK_NONBLOCK` / `SOCK_CLOEXEC` socket。
- `closeFd()` 通过 fd 引用把关闭后的变量置为 `kInvalidFd`，避免同一变量重复关闭。
- 新增 `UniqueFd.hpp` / `UniqueFd.cpp`，用 RAII 表达 fd 独占所有权：析构关闭持有的 fd，`release()` 可手动交出 fd，move 转移所有权，`reset()` 关闭旧 fd 后接管新 fd。
- `UniqueFd` 在 Step 13 中用于保护 `Acceptor` 的 listen fd 和临时 accepted fd；Step 16 前清理后，new-connection callback 直接接收 `UniqueFd`，避免 accepted fd 继续以裸 `int` 表达所有权。
- 新增 `Channel.hpp`，声明 fd 事件代理接口，包括关注事件、实际事件、事件开关和回调入口。
- 新增 `Epoller.hpp`，声明返回 `Status` 的 `poll()`、`updateChannel()` 和 `removeChannel()`，并预留 epoll fd、事件数组和 fd 到 `Channel*` 的映射。
- 新增 `EventLoop.hpp`，声明 `loop()`、`quit()`、`runInLoop()`、`queueInLoop()`、`updateChannel()`、`removeChannel()` 和线程归属检查。
- 新增 `Acceptor.hpp`，声明非阻塞监听器接口，包括 new-connection callback、listen fd 查询、实际绑定端口查询和关闭入口。
- 新增 `Channel.cpp`，实现构造、fd/event/revent 访问、事件 mask 修改、回调 setter、`handleEvent()` 回调分发和私有 `update()`。
- `Channel` 支持 `tie(std::shared_ptr<void>)`，事件回调前先锁定 owner 的 `weak_ptr`，后续 `Session` 可用它避免 owner 已销毁时继续执行回调。`Channel` 分发事件时不复制 callback，未 `tie()` 的 callback 不能在执行中销毁当前 `Channel` 或重置正在执行的 callback。
- 新增 `Epoller.cpp`，实现 `epoll_create1(EPOLL_CLOEXEC)`、`EPOLL_CTL_ADD`、`EPOLL_CTL_MOD`、`EPOLL_CTL_DEL` 和 `epoll_wait()`。
- `Epoller` 会拒绝注册或删除属于其他 `EventLoop` 的 `Channel`，维护 one-loop-per-thread 边界。
- 新增 `EventLoop.cpp`，实现 `loop()`、`quit()`、`runInLoop()`、`queueInLoop()`、内部 `eventfd` wakeup channel、任务队列、`updateChannel()` / `removeChannel()` 到 `Epoller` 的桥接和线程归属检查。
- `EventLoop` 使用 `UniqueFd` 管理 wakeup fd，`loop()` 隔离单个回调异常，`isStopped()` 只在 `loop()` 已经进入并退出后为 true，用于停止后的资源清理判断。
- 新增 `Acceptor.cpp`，实现 `socket`、`setsockopt`、`bind`、`listen`、listen channel 注册、`accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)` 循环和关闭清理。
- `Acceptor::NewConnectionCallback` 接收 `UniqueFd` 和 peer address；没有 callback 或 callback 抛异常时，accepted fd 由 `UniqueFd` 自动关闭。
- `Acceptor::close()` 支持从非 loop 线程调用；实际 `removeChannel()` 和 fd 关闭会回到所属 loop 线程完成，避免 `Epoller` 保留旧 fd 到旧 `Channel*` 的映射。
- `Acceptor` 使用 idle fd 处理 `EMFILE` / `ENFILE`，fd 用尽时拒绝一个 pending connection，避免 LT 模式 busy loop；loop 已停止后 close 走 fallback，不等待无法执行的 queued task。fd 用尽路径保留 `noexcept`，日志写入异常会被吞掉，避免异常状态下因日志 sink 失败触发 `std::terminate()`。
- 新增 `Session.hpp` / `Session.cpp`，实现单连接生命周期、非阻塞读写、Packet 解码、输出缓冲、跨线程 `sendPacket()` 和关闭回调。
- `Session` 使用 `std::enable_shared_from_this`，启动时通过 `Channel::tie()` 把 owner 绑定到事件分发期间，避免回调执行时 owner 被释放。
- `Session::handleRead()` 循环 `read()` 到 `EAGAIN` / `EWOULDBLOCK`，把读到的字节放进输入 `Buffer`，再交给 `FrameDecoder` 输出完整 `Packet`。
- `Session::sendPacket()` 先调用 `encodePacket()`，跨线程调用时通过 `EventLoop::queueInLoop()` 回到所属 I/O loop，再追加到输出 `Buffer` 并启用写事件。
- `Session::handleWrite()` 在 `EPOLLOUT` 到来时持续写输出缓冲区，写完后关闭写兴趣；大包或慢客户端写不完时剩余字节保留在 output buffer。
- `Session::close()` 在所属 loop 中移除 `Channel`、关闭 fd、清空缓冲区并触发 close callback；`Channel` 的释放被延后到当前事件回调返回之后，避免在 `Channel::handleEvent()` 栈帧内销毁正在执行的 `Channel`。
- 新增 `EventLoopThread.hpp` / `EventLoopThread.cpp`，一个线程拥有一个 `EventLoop`，`startLoop()` 等待 loop 初始化完成后返回观察指针，`stop()` 通过 `quit()` 唤醒并退出 loop，然后 join 线程。
- 新增 `EventLoopThreadPool.hpp` / `EventLoopThreadPool.cpp`，支持启动指定数量的子 I/O loops，`getNextLoop()` round-robin 返回下一个 loop，线程数为 0 时返回 base loop 作为单 Reactor fallback。
- `Epoller` 使用 LT 模式，不设置 `EPOLLET`。
- `Epoller` 构造失败直接抛异常，不保留无效 epoll fd 的半初始化对象。
- `epoll_wait()` 返回时通过 `epoll_event.data.ptr` 找回 `Channel*`，并写入 `Channel::setRevents()`。
- `tests/` 新增 Logger、EventLoop、Channel、Acceptor hardening round 2/3 回归测试、Session 单连接 I/O 测试、EventLoopThreadPool 多 Reactor 基础测试，以及 Step 16 前 ByteOrder / Epoller owner-loop / Acceptor UniqueFd callback 清理回归测试；当前 CTest 共 136 个测试。

本 Step 只实现 I/O loop 线程和线程池，不创建 `TcpServer`，不做连接分发、业务线程池、MySQL 或 Redis。仍然不提前创建 `client_cli/`、`client_qt/`、`bench/`、`scripts/`、`docker/` 等后续目录。

下一步是 `Step 16: implement TcpServer multi-Reactor version`。

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

项目二路线摘要见 [`docs/roadmap.md`](docs/roadmap.md)，LiteIM 只保留 Bot 用户和 BotGateway 所需的协议边界：

- `PersonaAgent Step 1-6`：Python BotClient、FastAPI `/chat`、协议兼容、登录心跳重连、Echo Bot。
- `PersonaAgent Step 7-20`：FastAPI + 6 节点 LangGraph + Knowledge/Memory/Authorized Style RAG + Persona + Safety + Tool Calling + Checkpoint + Trace + Evaluation。
- LangGraph 第一版只保留 6 个核心节点：`dialogue_policy`、`retrieve`、`tool_router`、`generate_reply`、`safety_check`、`send_message`。
- Authorized Style RAG 是项目二的核心卖点，不是普通 few-shot；聊天样本必须有授权、来源、用途、脱敏、撤回和 persona_id 隔离。
- Bot 对外定位是“授权风格模拟 AI 助手”，不能声称自己是授权对象本人，也不能代表真人做现实承诺。

推荐在 LiteIM Step 41 之后启动项目二，这时 Python BotClient 可以直接参考 CLI 客户端协议流程；最终在 Qt 客户端中打开 AI Assistant 联系人完成演示。

## 当前文件结构

Step 15 后只保留当前步骤真正需要的文件：

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
│       ├── base/
│       │   ├── Config.hpp
│       │   ├── ErrorCode.hpp
│       │   ├── Logger.hpp
│       │   ├── Status.hpp
│       │   ├── Types.hpp
│       │   └── Timestamp.hpp
│       ├── net/
│       │   ├── Buffer.hpp
│       │   ├── Acceptor.hpp
│       │   ├── Channel.hpp
│       │   ├── Epoller.hpp
│       │   ├── EventLoop.hpp
│       │   ├── EventLoopThread.hpp
│       │   ├── EventLoopThreadPool.hpp
│       │   ├── Session.hpp
│       │   ├── SocketUtil.hpp
│       │   └── UniqueFd.hpp
│       └── protocol/
│           ├── ByteOrder.hpp
│           ├── FrameDecoder.hpp
│           ├── MessageType.hpp
│           ├── Packet.hpp
│           ├── TlvCodec.hpp
│           └── Tlv.hpp
├── server/
│   ├── CMakeLists.txt
│   └── main.cpp
├── src/
│   ├── CMakeLists.txt
│   ├── base/
│   │   ├── CMakeLists.txt
│   │   ├── Config.cpp
│   │   ├── ErrorCode.cpp
│   │   ├── Logger.cpp
│   │   ├── Status.cpp
│   │   └── Timestamp.cpp
│   ├── net/
│   │   ├── Acceptor.cpp
│   │   ├── Buffer.cpp
│   │   ├── Channel.cpp
│   │   ├── EventLoop.cpp
│   │   ├── EventLoopThread.cpp
│   │   ├── EventLoopThreadPool.cpp
│   │   ├── Epoller.cpp
│   │   ├── Session.cpp
│   │   ├── SocketUtil.cpp
│   │   ├── UniqueFd.cpp
│   │   └── CMakeLists.txt
│   └── protocol/
│       ├── CMakeLists.txt
│       ├── FrameDecoder.cpp
│       ├── MessageType.cpp
│       ├── Packet.cpp
│       ├── TlvCodec.cpp
│       └── Tlv.cpp
├── tests/
│   ├── base/
│   │   ├── config_test.cpp
│   │   ├── error_code_test.cpp
│   │   ├── logger_test.cpp
│   │   └── timestamp_test.cpp
│   ├── net/
│   │   ├── acceptor_header_test.cpp
│   │   ├── acceptor_test.cpp
│   │   ├── buffer_test.cpp
│   │   ├── channel_header_test.cpp
│   │   ├── channel_test.cpp
│   │   ├── epoller_header_test.cpp
│   │   ├── epoller_test.cpp
│   │   ├── event_loop_header_test.cpp
│   │   ├── event_loop_test.cpp
│   │   ├── event_loop_thread_header_test.cpp
│   │   ├── event_loop_thread_pool_header_test.cpp
│   │   ├── event_loop_thread_test.cpp
│   │   ├── event_loop_thread_pool_test.cpp
│   │   ├── session_header_test.cpp
│   │   ├── session_test.cpp
│   │   ├── socket_util_test.cpp
│   │   └── unique_fd_test.cpp
│   ├── protocol/
│   │   ├── byte_order_test.cpp
│   │   ├── frame_decoder_test.cpp
│   │   ├── message_type_test.cpp
│   │   ├── packet_test.cpp
│   │   ├── tlv_codec_test.cpp
│   │   └── tlv_type_test.cpp
│   ├── CMakeLists.txt
│   └── test_main.cpp
└── tutorials/
    ├── README.md
    ├── step00_reset.md
    ├── step01_project_init.md
    ├── step02_base.md
    ├── step03_protocol_types.md
    ├── step04_packet.md
    ├── step05_tlv_codec.md
    ├── step06_frame_decoder.md
    ├── step07_buffer.md
    ├── step08_socket_util.md
    ├── step09_reactor_interfaces.md
    ├── step10_epoller.md
    ├── step11_channel.md
    ├── step12_event_loop.md
    ├── step13_acceptor.md
    ├── step14_session.md
    └── step15_event_loop_thread_pool.md
```

后续目录按 Step 逐步创建。比如 `TcpServer`、CLI、Qt、benchmark、Docker 会在后续 Step 需要时创建。

## Step 15 验证

Step 15 有一个 server target、`liteim_base` / `liteim_protocol` / `liteim_net` library target 和一个 GoogleTest test target。运行：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

预期结果：

```text
[2026-05-06 ...] [info] LiteIM server scaffold is running on 0.0.0.0:9000
```

CTest 应发现并通过 136 个测试。Step 15 新增测试：

- `ReactorInterfaceTest.EventLoopThreadHeaderIsSelfContained`
- `ReactorInterfaceTest.EventLoopThreadPoolHeaderIsSelfContained`
- `EventLoopThreadTest.StartLoopCreatesLoopOnWorkerThread`
- `EventLoopThreadTest.StopWithoutStartIsNoop`
- `EventLoopThreadTest.DestructorStopsRunningLoop`
- `EventLoopThreadPoolTest.StartCreatesRequestedNumberOfLoops`
- `EventLoopThreadPoolTest.GetNextLoopUsesRoundRobinOrder`
- `EventLoopThreadPoolTest.ZeroThreadsReturnsBaseLoop`
- `EventLoopThreadPoolTest.ChildLoopsRunTasksOnDistinctThreads`

已有 Step 14 测试包括：

- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `SessionTest.CompletePacketInvokesMessageCallback`
- `SessionTest.HalfPacketDoesNotInvokeMessageCallback`
- `SessionTest.StickyPacketsInvokeCallbackForEachPacket`
- `SessionTest.PeerCloseInvokesCloseCallback`
- `SessionTest.SendPacketFromOtherThreadDeliversEncodedPacket`
- `SessionTest.LargePacketLeavesPendingOutputWhenPeerDoesNotRead`
- `SessionTest.LastActiveTimeIsInitialized`

已有 Step 13 和 review hardening 测试包括：

- `ReactorInterfaceTest.AcceptorHeaderIsSelfContained`
- `AcceptorTest.ServerCanListenOnEphemeralPort`
- `AcceptorTest.ClientConnectionTriggersNewConnectionCallback`
- `AcceptorTest.MultiplePendingConnectionsAreAccepted`
- `AcceptorTest.ClosedListenSocketRejectsNewConnections`
- `AcceptorTest.CloseFromOtherThreadRemovesChannelBeforeClosingFd`
- `AcceptorTest.AcceptedFdIsClosedWhenCallbackThrowsBeforeTakingOwnership`
- `AcceptorTest.CloseFromOtherThreadAfterLoopStopsDoesNotBlock`
- `AcceptorTest.CloseFromOtherThreadBeforeLoopStartsStillRemovesChannel`
- `AcceptorTest.FdExhaustionRejectsPendingConnectionWithoutLaterCallback`
- `UniqueFdTest.DestructorClosesOwnedFd`
- `UniqueFdTest.ReleaseReturnsFdWithoutClosing`
- `UniqueFdTest.MoveTransfersOwnership`
- `UniqueFdTest.ResetClosesPreviousFd`
- `ChannelTest.TiedExpiredOwnerSkipsCallbacks`
- `ChannelTest.TiedOwnerStaysAliveDuringCallback`
- `ChannelTest.HandleEventDoesNotCopyStoredCallbacks`
- `EventLoopTest.ChannelCallbackExceptionDoesNotEscapeLoop`
- `EventLoopTest.IsStoppedIsFalseBeforeLoopEverStarts`
- `EventLoopTest.IsStoppedIsFalseAfterQuitBeforeLoopEverStarts`
- `EventLoopTest.LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart`
- `EventLoopTest.IsStoppedBecomesTrueAfterLoopReturns`
- `LoggerTest.GetDoesNotResetConfiguredLevel`
- `LoggerTest.SetLevelSurvivesLaterGetCalls`

已有 Step 12 测试包括：

- `EventLoopTest.RunInLoopExecutesImmediatelyOnOwnerThread`
- `EventLoopTest.QueueInLoopFromOtherThreadWakesAndExecutesTask`
- `EventLoopTest.LoopHandlesRegisteredFdEvent`
- `EventLoopTest.QueueInLoopRunsMultipleTasksAfterWakeup`

已有 Step 11 测试包括：

- `ChannelTest.EnableAndDisableEventsUpdateInterestMask`
- `ChannelTest.ReadableEventInvokesReadCallback`
- `ChannelTest.WritableEventInvokesWriteCallback`
- `ChannelTest.ReadWriteEventInvokesCallbacksInStableOrder`
- `ChannelTest.HangupWithoutReadableEventInvokesCloseOnly`
- `ChannelTest.ErrorEventInvokesErrorCallback`
- `ChannelTest.HandleEventToleratesMissingCallbacks`
- `ChannelTest.TiedExpiredOwnerSkipsCallbacks`
- `ChannelTest.TiedOwnerStaysAliveDuringCallback`

已有 Step 10 测试包括：

- `EpollerTest.AddChannelReceivesReadableEvent`
- `EpollerTest.ModifyChannelToWriteInterestTakesEffect`
- `EpollerTest.RemoveChannelStopsEvents`
- `EpollerTest.PollTimeoutReturnsEmptyActiveList`
- `EpollerTest.InvalidChannelOperationsReturnError`
- `EpollerTest.RejectsChannelOwnedByDifferentEventLoop`

已有 Step 9 测试包括：

- `ReactorInterfaceTest.ChannelHeaderIsSelfContained`
- `ReactorInterfaceTest.EpollerHeaderIsSelfContained`
- `ReactorInterfaceTest.EventLoopHeaderIsSelfContained`

已有 Step 8 测试包括：

- `SocketUtilTest.CreateNonBlockingSocketReturnsNonblockingFd`
- `SocketUtilTest.SetNonBlockingMarksPlainSocketNonblocking`
- `SocketUtilTest.SocketOptionsCanBeEnabled`
- `SocketUtilTest.InvalidFdReturnsError`
- `SocketUtilTest.CloseFdInvalidatesDescriptorAndCanBeRepeated`
- `SocketUtilTest.GetSocketErrorReturnsCurrentSoError`

已有 Step 7 测试包括：

- `BufferTest.DefaultBufferHasNoReadableBytes`
- `BufferTest.AppendIncreasesReadableBytes`
- `BufferTest.AppendStringStoresReadableData`
- `BufferTest.AppendBytePointerStoresBytes`
- `BufferTest.RetrieveAdvancesReadIndex`
- `BufferTest.RetrieveAllResetsBuffer`
- `BufferTest.RetrieveAllAsStringReturnsReadableDataAndClearsBuffer`
- `BufferTest.AppendExpandsWhenNeeded`
- `BufferTest.AppendCompactsReadableDataBeforeExpanding`
- `BufferTest.AppendExpandsAndPreservesExistingData`
- `BufferTest.RetrievePastReadableBytesReturnsError`
- `BufferTest.NullAppendWithNonzeroLengthReturnsError`
- `BufferTest.NullAppendWithZeroLengthIsOk`

已有 Step 6 测试包括：

- `FrameDecoderTest.CompletePacketEmitsOnePacket`
- `FrameDecoderTest.PacketSplitAcrossFeedsEmitsAfterSecondFeed`
- `FrameDecoderTest.MultiplePacketsInOneFeedAreDecoded`
- `FrameDecoderTest.HalfPacketThenStickyPacketAreDecodedTogether`
- `FrameDecoderTest.InvalidMagicEntersErrorState`
- `FrameDecoderTest.InvalidVersionEntersErrorState`
- `FrameDecoderTest.OversizedBodyLengthEntersErrorState`
- `FrameDecoderTest.ErrorStateRejectsFurtherFeedUntilReset`
- `FrameDecoderTest.NullInputWithNonzeroLengthReturnsError`

已有 Step 5 测试包括：

- `TlvCodecTest.StringFieldCanBeEncodedAndDecoded`
- `TlvCodecTest.MultipleFieldsCanBeEncodedAndDecoded`
- `TlvCodecTest.Utf8StringCanBeEncodedAndDecoded`
- `TlvCodecTest.RepeatedStringFieldsArePreserved`
- `TlvCodecTest.RepeatedUint64FieldsArePreserved`
- `TlvCodecTest.Uint64UsesNetworkByteOrder`
- `TlvCodecTest.TlvLengthOutOfBoundsReturnsError`
- `TlvCodecTest.IncompleteTlvHeaderReturnsError`
- `TlvCodecTest.MissingStringFieldReturnsError`
- `TlvCodecTest.MissingUint64FieldReturnsError`
- `TlvCodecTest.WrongUint64LengthReturnsError`
- `TlvCodecTest.UnknownTypeCannotBeEncoded`

Step 16 前协议字节序清理测试包括：

- `ByteOrderTest.AppendsUnsignedIntegersAsBigEndianBytes`
- `ByteOrderTest.ReadsUnsignedIntegersFromBigEndianBytes`

已有 Step 4 测试包括：

- `PacketTest.EncodePacketThenParseHeader`
- `PacketTest.EmptyBodyCanBeEncoded`
- `PacketTest.Utf8BodyCanBeEncoded`
- `PacketTest.HeaderUsesNetworkByteOrder`
- `PacketTest.InvalidMagicReturnsError`
- `PacketTest.InvalidVersionReturnsError`
- `PacketTest.OversizedBodyLengthReturnsError`
- `PacketTest.EncodingOversizedBodyReturnsError`
- `PacketTest.IncompleteHeaderReturnsError`
- `PacketTest.NullHeaderDataReturnsError`

已有测试仍然包括：

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
- `MessageTypeTest.CoreTypesReturnReadableNames`
- `MessageTypeTest.UnknownTypeReturnsUnknown`
- `MessageTypeTest.RequestTypesAreClassified`
- `MessageTypeTest.ResponseTypesAreClassified`
- `MessageTypeTest.PushTypesAreClassified`
- `MessageTypeTest.UnknownTypesAreNotClassified`
- `TlvTypeTest.CoreTypesReturnReadableNames`
- `TlvTypeTest.UnknownTypeReturnsUnknown`

## 历史 Step

```text
Step 0: reset workspace
Step 1: init CMake project structure with googletest
Step 2: add config logger and error code
Step 3: define MessageType and TLV types
Step 4: implement Packet encode/decode
Step 5: implement TlvCodec
Step 6: implement FrameDecoder
Step 7: implement Buffer
Step 8: implement SocketUtil
Step 9: define Reactor core interfaces
Step 10: implement Epoller
Step 11: implement Channel
Step 12: implement EventLoop
Step 13: implement Acceptor
Step 14: implement Session
```

相关教程：

```text
    ├── step00_reset.md
    ├── step01_project_init.md
    ├── step02_base.md
    ├── step03_protocol_types.md
    ├── step04_packet.md
    ├── step05_tlv_codec.md
    ├── step06_frame_decoder.md
    ├── step07_buffer.md
    ├── step08_socket_util.md
    ├── step09_reactor_interfaces.md
    ├── step10_epoller.md
    ├── step11_channel.md
    ├── step12_event_loop.md
    └── step13_acceptor.md
```

## 当前执行文件

- [`docs/roadmap.md`](docs/roadmap.md)：仓库内路线摘要。
- [`task_plan.md`](task_plan.md)：当前执行计划。
- [`findings.md`](findings.md)：设计结论和风险记录。
- [`progress.md`](progress.md)：真实完成进度。
- [`tutorials/step01_project_init.md`](tutorials/step01_project_init.md)：Step 1 教程。
- [`tutorials/step02_base.md`](tutorials/step02_base.md)：Step 2 教程。
- [`tutorials/step03_protocol_types.md`](tutorials/step03_protocol_types.md)：Step 3 教程。
- [`tutorials/step04_packet.md`](tutorials/step04_packet.md)：Step 4 教程。
- [`tutorials/step05_tlv_codec.md`](tutorials/step05_tlv_codec.md)：Step 5 教程。
- [`tutorials/step06_frame_decoder.md`](tutorials/step06_frame_decoder.md)：Step 6 教程。
- [`tutorials/step07_buffer.md`](tutorials/step07_buffer.md)：Step 7 教程。
- [`tutorials/step08_socket_util.md`](tutorials/step08_socket_util.md)：Step 8 教程。
- [`tutorials/step09_reactor_interfaces.md`](tutorials/step09_reactor_interfaces.md)：Step 9 教程。
- [`tutorials/step10_epoller.md`](tutorials/step10_epoller.md)：Step 10 教程。
- [`tutorials/step11_channel.md`](tutorials/step11_channel.md)：Step 11 教程。
- [`tutorials/step12_event_loop.md`](tutorials/step12_event_loop.md)：Step 12 教程。
- [`tutorials/step13_acceptor.md`](tutorials/step13_acceptor.md)：Step 13 教程。
- [`tutorials/step14_session.md`](tutorials/step14_session.md)：Step 14 教程。
