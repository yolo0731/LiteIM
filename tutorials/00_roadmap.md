# LiteIM + PersonaAgent 总路线图

你的简历项目建议拆成两个项目、三个阶段：

1. LiteIM 服务端核心能力和简化高性能网络点
2. LiteIM 仿微信 Qt 客户端和 bot 接入能力
3. PersonaAgent AI Agent 能力

## 总共分几个 Step

完整规划建议分为 **43 个 Step**：

- LiteIM：32 个 Step
- PersonaAgent：11 个 Step

但你现在不要同时做两个项目。当前已经做到 **LiteIM Step 15**，后续先专注 Step 16 到 Step 21，跑通：

```text
epoll 服务端 + TLV 协议 + 登录 + 私聊 + 群聊 + CLI 验证
```

这个服务端闭环跑通后，再补 `eventfd`、多 Reactor、业务线程池、慢客户端回压、Qt 仿微信客户端和 PersonaAgent。

## 项目一 LiteIM：32 个 Step

### 第一阶段：C++ 服务端基础骨架

#### Step 1：初始化 CMake 工程

目标：

- 创建标准 C++17 CMake 项目。
- 生成服务端可执行文件 `liteim_server`。
- 生成测试可执行文件 `liteim_tests`。
- 创建 `server`、`tests`、`client_qt`、`docs`、`sql` 等目录。

你要学会：

- CMake 最小项目结构
- `add_subdirectory`
- `add_executable`
- `target_compile_options`
- `enable_testing`
- 为什么构建产物要放进 `build/`

验收：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

#### Step 2：实现协议基础 Packet

目标：

- 定义 `PacketHeader`
- 定义 `Packet`
- 定义 `MessageType`
- 实现 `encodePacket`
- 实现 `parseHeader`

你要学会：

- TCP 是字节流，不保留消息边界。
- 为什么需要固定长度 Header。
- `magic`、`version`、`msg_type`、`seq_id`、`body_len` 每个字段的作用。
- 为什么要限制 `body_len` 最大 1MB。

#### Step 3：实现 FrameDecoder

目标：

- 从 TCP 字节流中解析完整 Packet。
- 支持半包。
- 支持粘包。
- 支持半包和粘包混合。
- 遇到非法包进入 error 状态。

你要学会：

- TCP 粘包/半包的根本原因。
- 输入缓冲区的作用。
- 为什么 `FrameDecoder` 不应该直接读 socket。

#### Step 4：实现 Buffer

目标：

- 实现可复用的读写缓冲区。
- 支持追加、查看、消费、一次取出所有内容。

你要学会：

- 网络程序为什么不能假设一次读完或一次写完。
- 输入缓冲区和输出缓冲区分别解决什么问题。

#### Step 5：实现 SocketUtil

目标：

- 封装 Linux socket 常用操作。
- 创建非阻塞 socket。
- 设置 `SO_REUSEADDR`、`SO_REUSEPORT`。
- 封装关闭 fd 和读取 socket 错误。

你要学会：

- 什么是 fd。
- 什么是非阻塞 I/O。
- 为什么 epoll 服务端通常配合非阻塞 fd。

#### Step 6：定义 Epoller / Channel / EventLoop 头文件接口

目标：

- 先定义 `Epoller`
- 先定义 `Channel`
- 先定义 `EventLoop`
- 不实现复杂逻辑
- 固定 Reactor 三个核心类的职责边界

你要学会：

- Reactor 模型为什么要拆成多个类。
- 如何用前向声明避免循环 include。
- 为什么先定接口再填实现。

#### Step 7：实现 Epoller

目标：

- 封装 `epoll_create1`
- 封装 `epoll_ctl add/mod/del`
- 封装 `epoll_wait`
- 第一版只使用 LT 模式。

你要学会：

- epoll 的三个核心系统调用。
- LT 和 ET 的区别。
- 为什么第一版先使用 LT。

#### Step 8：实现 EventLoop 基础骨架

目标：

- 持有 `Epoller`
- `loop()` 中调用 `epoll_wait`
- 遍历活跃 `Channel`
- 支持 `quit()`
- 支持 `updateChannel()` 和 `removeChannel()`

你要学会：

- 什么是事件循环。
- 一个线程如何管理多个连接。
- 为什么退出事件循环需要明确的状态位。

#### Step 9：实现 Channel 并联通 EventLoop

目标：

- 用 `Channel` 封装 fd 和事件回调。
- 支持读、写、关闭、错误回调。
- `handleEvent()` 根据 `revents` 分发回调。
- `enableReading()` 等接口通过 `EventLoop` 更新 epoll 关注事件。

你要学会：

- Reactor 模型里 Channel 的职责。
- 为什么 Channel 不直接拥有业务逻辑。
- `events` 和 `revents` 的区别。

#### Step 10：实现 Acceptor

目标：

- 创建监听 socket。
- `bind`
- `listen`
- 监听新连接。
- 非阻塞 `accept` 到 EAGAIN。

你要学会：

- 服务端启动监听端口的完整流程。
- 为什么 accept 也要循环到 EAGAIN。

#### Step 11：实现 Session

目标：

- 管理单个客户端连接。
- 处理 read/write/close。
- 接入 `FrameDecoder`。
- 实现 `sendPacket`。

你要学会：

- 一个 TCP 连接在服务端对应什么对象。
- 连接生命周期如何管理。
- 输出缓冲区如何处理短写。

#### Step 12：实现 TcpServer

目标：

- 组合 `EventLoop`、`Acceptor`、`Session`。
- 管理所有在线连接。
- 支持新连接创建和关闭清理。
- 支持 Ctrl+C / SIGTERM 优雅退出。

你要学会：

- 为什么需要一个总控对象管理连接集合。
- `unordered_map<int, shared_ptr<Session>>` 的意义。
- 为什么 socket、timerfd、signalfd 都可以统一交给 epoll。

#### Step 13：实现 MessageRouter 和心跳响应

目标：

- 根据 `msg_type` 分发请求。
- 第一版只处理 `HEARTBEAT_REQ`。
- 返回 `HEARTBEAT_RESP`。

你要学会：

- 网络层和业务层如何解耦。
- 为什么 Router 不应该直接操作 fd。

### 第二阶段：账号、消息和存储

#### Step 14：定义 IStorage / ICache 抽象

目标：

- 定义业务层依赖的存储接口。
- 定义缓存接口。
- 实现 `NullCache`。

你要学会：

- 为什么业务层不应该直接依赖具体数据库。
- 为什么先定义接口再落地数据库实现。
- 为什么数据库/缓存在本项目里是支撑组件，不是简历主线。

#### Step 15：实现 SQLiteStorage + NullCache

目标：

- 打开 `liteim.db`
- 启动时执行 `sql/init.sql`
- 创建用户、群组、消息表。
- 实现用户、好友、群组、消息相关存储接口。

你要学会：

- 为什么 IM 系统需要持久化。
- 数据库访问层为什么要单独封装。
- SQLite 是当前已完成的单机落地方案；后续如果替换 MySQL，要作为单独 Step 重构，不在业务层直接改 SQL。

#### Step 16：实现用户注册和登录

目标：

- 实现 `AuthService`。
- 支持 `REGISTER_REQ`。
- 支持 `LOGIN_REQ`。
- 登录成功后绑定 `Session` 用户身份。

你要学会：

- 连接 `Session` 和用户身份的关系。
- 为什么业务层依赖 `IStorage`，而不是直接写数据库代码。
- 为什么密码 hash 在本项目里是教学实现，不包装成生产级安全能力。

#### Step 17：实现私聊

目标：

- 处理 `PRIVATE_MSG_REQ`。
- 保存消息。
- 在线推送。
- 离线只存储。

你要学会：

- IM 私聊的核心数据流。
- 在线连接表和消息存储如何配合。

#### Step 18：实现群聊

目标：

- 创建默认测试群或最小建群接口。
- 查询群成员。
- 保存群消息。
- 推送给群内在线成员。

你要学会：

- 群聊和私聊路由的区别。
- 为什么群成员查询要通过 service/storage 边界。

#### Step 19：实现历史消息查询

目标：

- 查询私聊最近 N 条。
- 查询群聊最近 N 条。
- 限制默认最大 50 条。

你要学会：

- 历史消息接口如何避免一次返回过多数据。
- Qt 打开会话时为什么需要历史消息接口。

### 第三阶段：连接稳定性和 CLI 验证

#### Step 20：实现 TimerHeap 和心跳清理

目标：

- 用最小堆管理超时。
- Session 维护 `last_active_time`。
- 90 秒未活跃自动断开。

你要学会：

- 心跳检测解决什么问题。
- 为什么不用每个连接一个线程或一个 timer。
- 什么是 lazy deletion。

#### Step 21：实现命令行测试客户端

目标：

- 连接服务器。
- 注册、登录。
- 发送私聊和群聊。
- 查询历史消息。
- 定期发送心跳。
- 打印 push 消息。

你要学会：

- 在写 GUI 前先用 CLI 验证服务端。
- 如何写一个协议调试工具。

### 第四阶段：简化高性能网络点

#### Step 22：实现 EventLoop eventfd 唤醒和任务投递

目标：

- 实现 `runInLoop()`。
- 实现 `queueInLoop()`。
- 使用 `eventfd` 唤醒阻塞在 `epoll_wait()` 的 EventLoop。

你要学会：

- 为什么跨线程投递任务需要唤醒 EventLoop。
- 为什么这是 one loop per thread 的基础。

#### Step 23：实现 EventLoopThreadPool 多 Reactor

目标：

- 一个线程拥有一个 `EventLoop`。
- 主 Reactor 负责 accept。
- 子 Reactor 负责连接读写。
- 新连接 round-robin 分配到子 EventLoop。

你要学会：

- one loop per thread 的基本思想。
- 为什么 `Session` 应该归属固定 I/O 线程。

#### Step 24：实现业务线程池

目标：

- 实现固定大小 `ThreadPool`。
- 登录、聊天、历史查询等业务任务可以投递到业务线程池。
- 业务线程处理完成后，再把响应投递回对应 EventLoop。

你要学会：

- 为什么 MySQL/Redis/Agent 等可能阻塞的任务不能放在 I/O 线程里。

#### Step 25：实现 Session 慢客户端回压

目标：

- 给输出缓冲区增加高水位。
- 慢客户端导致待发送数据超过阈值时关闭连接。
- 用测试证明 output buffer 不会无限增长。

你要学会：

- 慢客户端为什么会拖垮服务端内存。
- 第一版为什么用简单高水位关闭策略即可。

### 第五阶段：Qt 仿微信客户端

#### Step 26：Qt 客户端基础工程和 TcpClient

目标：

- `client_qt` 可选构建。
- `TcpClient` 使用 `QTcpSocket`。
- 客户端实现 TLV 编解码。
- 客户端处理半包和粘包。

#### Step 27：Qt 登录和注册窗口

目标：

- 用户名输入框。
- 密码输入框。
- 登录按钮。
- 注册按钮。
- 登录成功后打开主窗口。

#### Step 28：Qt 仿微信主窗口和会话列表

目标：

- 左侧功能栏。
- 中间会话列表。
- 右侧聊天区域。
- 好友、群聊和 AI Bot 联系人占位。

注意：

- 只借鉴常见聊天软件三栏布局，不使用微信 logo、名称、图标或素材。

#### Step 29：Qt 聊天窗口和消息气泡

目标：

- 左右消息气泡。
- 文本输入框。
- 私聊和群聊发送。
- 收到 push 后刷新 UI。
- 打开会话时加载最近历史消息。

#### Step 30：Qt 心跳、断线提示和 AI Bot 入口

目标：

- 登录成功后启动 `QTimer`。
- 每 30 秒发送心跳。
- 断线后 UI 提示。
- 会话列表展示 `AI Assistant` Bot 联系人。

### 第六阶段：测试、压测和文档

#### Step 31：端到端测试和简单压测

目标：

- 端到端测试注册、登录、私聊、群聊、历史消息、心跳。
- 压测长连接、私聊和慢客户端回压。
- 统计连接数、QPS、平均延迟、p99 和内存占用。
- README 里只写真实测出来的数据。

#### Step 32：补齐 README、截图和 docs

目标：

- 写清楚服务端架构、Qt 客户端、运行方式、测试方式和压测结果。
- 补充 Qt 客户端截图。
- 补充 PersonaAgent BotClient 接入方式。

## 项目二 PersonaAgent：11 个 Step

PersonaAgent 等 LiteIM 能跑通后再做。

#### Step 1：初始化 Python 项目

创建 FastAPI 服务、目录结构和 `/health` 接口。

#### Step 2：实现 Python 版 TLV 协议

和 C++ 的 Packet 协议保持一致，并测试半包、粘包。

#### Step 3：实现 BotClient 登录 LiteIM

Python Bot 作为虚拟用户登录 LiteIM，并能接收 push。

#### Step 4：实现 LangGraph StateGraph

定义 AgentState 和基础节点。

#### Step 5：实现 DialoguePolicy

控制私聊回复、群聊 @ 回复、冷却时间和自循环防护。

#### Step 6：实现 Knowledge RAG

读取项目文档和技术笔记，构建知识库检索。

#### Step 7：实现 Memory RAG

实现长期记忆存储和检索。

#### Step 8：实现 Style RAG

基于授权聊天记录构建风格样例检索。

#### Step 9：实现回复生成

组合 persona、上下文、RAG 文档、长期记忆和风格样例生成回复。

#### Step 10：实现 SafetyGuard

拦截隐私、冒充真实个人、高风险建议等内容。

#### Step 11：接回 LiteIM

BotClient 收到消息后调用 AgentService，并把回复发回聊天室。

## 推荐推进节奏

当前最重要的不是“项目看起来大”，而是先让第一个闭环跑起来。

建议顺序：

1. 先完成 LiteIM Step 1 到 Step 3：工程、协议、拆包。
2. 再完成 LiteIM Step 4 到 Step 13：epoll Reactor 和心跳响应。
3. 你当前已经完成 Step 15：存储抽象和 SQLiteStorage 单机落地。
4. 下一阶段先完成 Step 16 到 Step 21：注册登录、私聊、群聊、历史、心跳和 CLI 客户端，形成服务端聊天闭环。
5. 再完成 Step 22 到 Step 25：`eventfd`、多 Reactor、业务线程池和慢客户端回压，补充 C++ 网络高性能关键点。
6. 再完成 Step 26 到 Step 30：Qt 仿微信客户端和 AI Bot 联系人入口。
7. 最后完成 Step 31 到 Step 32：端到端测试、简单压测、README、截图和面试文档。
8. PersonaAgent 等 LiteIM 客户端闭环稳定后再做。
