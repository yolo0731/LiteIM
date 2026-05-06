# LiteIM Findings

## 权威来源

- `/home/yolo/jianli/PROJECT_MEMORY.md` 是 LiteIM 和 PersonaAgent 的唯一总方案来源。
- LiteIM 现在从 `Step 0` 重新开始。
- 当前路线是 `LiteIM High Performance + Qt Client + PersonaAgent Authorized Style RAG Edition`。
- 如果 `README.md`、`task_plan.md`、`progress.md`、教程或源码与 `PROJECT_MEMORY.md` 冲突，统一改回 `PROJECT_MEMORY.md` 的路线。

## Step 0 清理结论

本次 Step 0 的目的不是实现功能，而是清掉旧路线，留下最小起点。

已经删除的旧内容类型：

- 旧 `include/`、`src/`、`server/` 实现。
- 旧 `tests/` 单元测试。
- 旧 `tutorials/step01-step15` 教程。
- 旧 `docs/` 文档。
- 旧 `sql/` SQLite / 初始化脚本目录。
- 旧 `client_qt/` 临时结构。
- 旧 `build/` 构建产物。
- 空的 `.codex` 临时文件。
- 未来 Step 才会用到的空目录和 `.gitkeep`。

当前保留内容：

- `.gitignore`。
- `LICENSE`。
- 空 CMake 骨架。
- README / task_plan / findings / progress。
- `docs/architecture.md` 和 `docs/project_layout.md`。
- `tutorials/README.md` 和 `tutorials/step00_reset.md`。

## 关于 .gitkeep

`.gitkeep` 不是 Git 必需文件。Git 不跟踪空目录，所以 `.gitkeep` 只是社区常用占位文件。

本项目不保留 `.gitkeep`，原因是：

- 用户希望项目从 Step 0 开始逐步建立。
- 空目录提前出现会让教程边界不清楚。
- 每个目录应该在真正需要它的 Step 中创建，并在教程里解释为什么需要。

## 核心架构结论

- 先搭高性能网络底座，再做业务、MySQL、Redis、Qt 和 Agent 接入。
- 不再走旧的单 Reactor 业务 baseline。
- 最终 LiteIM 不使用 SQLite。
- `InMemoryStorage` 不能作为主线存储实现；后续最多作为测试 double / mock。
- 服务端使用 C++17、CMake、Linux nonblocking socket、epoll LT、eventfd、timerfd、signalfd 和自定义 TLV 协议。
- 使用 one-loop-per-thread：每个 I/O 线程拥有一个 `EventLoop`。
- 主 Reactor 负责 `accept`，子 Reactor 负责连接读写事件。
- MySQL / Redis 阻塞调用必须进入业务 `ThreadPool`，不能在 I/O 线程执行。
- 业务线程不能直接修改 `Session`；响应必须通过 `EventLoop::queueInLoop()` 或 `EventLoop::runInLoop()` 投递回连接所属 I/O 线程。
- `Session` / `TcpConnection` 生命周期使用 `shared_ptr` / `weak_ptr` 管理，避免跨线程长期持有裸指针。
- 慢客户端保护必须显式实现，通过输出缓冲区高水位触发关闭或限流。

## Step 1 约束

Step 1 只做第一层工程初始化：

- 只创建 Step 1 真正需要的 `server/` 和 `tests/` 目录。
- 添加真正的 CMake target：`liteim_server` 和 `liteim_tests`。
- 根 CMake 用 `FetchContent` 接入 GoogleTest v1.14.0。
- `tests/CMakeLists.txt` 链接 `GTest::gtest_main` 并使用 `gtest_discover_tests`。
- 添加 `server/main.cpp`。
- 添加最小 GoogleTest 用例 `TEST(SmokeTest, GoogleTestWorks)`。
- 保持 `include/`、`src/`、Qt、MySQL、Redis、协议、Reactor 都不提前实现。

Step 1 不允许恢复旧 Step 1-15 文件。旧代码里的知识可以参考，但文件本身不作为新路线起点。

## Step 1 实现结论

- 根 `CMakeLists.txt` 只接入 `server/` 和 `tests/`。
- `server/CMakeLists.txt` 生成 `liteim_server`。
- `tests/CMakeLists.txt` 生成 `liteim_tests`，链接 `GTest::gtest_main`，并通过 `gtest_discover_tests` 注册 CTest。
- `server/main.cpp` 只打印启动信息。
- `tests/test_main.cpp` 使用 `TEST(SmokeTest, GoogleTestWorks)` 验证 C++17 编译环境和 GoogleTest/CTest 链路。
- Step 1 没有创建 `.gitkeep`，也没有提前创建未来目录。

## Step 2 约束

Step 2 只实现基础公共模块，不进入协议、socket、Reactor、MySQL、Redis 或 Qt。

本 Step 允许新增：

- `include/liteim/base/`
- `src/base/`
- `tests/base/`
- `src/CMakeLists.txt`
- `src/base/CMakeLists.txt`

本 Step 不允许提前新增：

- `include/liteim/protocol/`
- `include/liteim/net/`
- `include/liteim/storage/`
- `include/liteim/cache/`
- `client_cli/`
- `client_qt/`
- `bench/`
- `scripts/`
- `docker/`

## Step 2 实现结论

- `Config` 使用默认值 + 简单 `key=value` 文件加载，先覆盖 server、MySQL、Redis、Qt 默认连接配置，为后续 Step 保留统一入口。
- `Config::loadFromFile()` 返回 `Status`，而不是直接抛异常或返回裸 `bool`，方便调用方区分 `NotFound`、`ParseError` 和 `InvalidArgument`。
- 当前配置解析器只支持本项目需要的扁平 key，不做 YAML/JSON/TOML，避免在基础阶段引入额外复杂度。
- `Logger` 通过 `spdlog` 建立 `liteim` logger，统一输出格式；本 Step 不自研异步日志。
- `Logger` 内部用 `std::mutex` 保护全局 logger 初始化，避免多处调用 `init()` / `get()` 时重复创建。
- `ErrorCode` 和 `Status` 是后续模块的统一错误表达：简单场景返回 `Status`，复杂数据结果以后可以再引入 `Result<T>`。
- `Timestamp` 提供毫秒时间戳和 UTC ISO-8601 字符串，后续消息时间、日志字段、压测统计都可以复用。
- `liteim_server` 仍然只是 scaffold，没有真实监听 socket；它现在用默认 `Config` 初始化日志并打印 `0.0.0.0:9000`。
- `liteim_base` 暴露公共 include root：`${PROJECT_SOURCE_DIR}/include`，后续模块统一使用 `#include "liteim/base/Config.hpp"` 这种项目限定路径。
- Step 2 没有恢复 SQLite、`InMemoryStorage`、旧 `server/net` 或旧 `server/protocol` 路线。

## Step 3 约束

Step 3 只实现协议类型定义，不进入二进制 Packet 编解码或 TCP 流式解码。

本 Step 允许新增：

- `include/liteim/protocol/MessageType.hpp`
- `include/liteim/protocol/Tlv.hpp`
- `src/protocol/CMakeLists.txt`
- `src/protocol/MessageType.cpp`
- `src/protocol/Tlv.cpp`
- `tests/protocol/message_type_test.cpp`
- `tests/protocol/tlv_type_test.cpp`

本 Step 不允许提前新增：

- `PacketHeader`
- `Packet`
- `encodePacket()`
- `parseHeader()`
- `FrameDecoder`
- `Buffer`
- socket / epoll / Reactor 相关代码

## Step 3 实现结论

- `MessageType` 使用 `std::uint16_t` 作为底层类型，和后续 Packet header 的 `msg_type` 字段匹配。
- 消息类型按范围分组：心跳、认证、好友、私聊、群聊、离线/历史、Bot 和错误响应。
- 私聊、群聊和 Bot 都保留 `Push` 类型，用于后续服务端向接收方主动推送消息；`Push` 既不是 request，也不是 response，但需要通过 `isPushType()` 显式识别，避免和 `Unknown` 混在一起。
- `isRequestType()` 只返回客户端或 BotClient 主动请求类型。
- `isResponseType()` 只返回服务端对请求的响应类型，`ErrorResponse` 归为 response，方便后续统一错误返回。
- `isPushType()` 只返回 `PrivateMessagePush`、`GroupMessagePush` 和 `BotMessagePush`。
- `ListGroupsRequest` / `ListGroupsResponse` 已放入 4xx 群聊类型段，避免 Step 37 再回头修改协议编号。
- 未知 `MessageType` 和 `TlvType` 都返回 `UNKNOWN`，后续解析到未注册类型时可以安全记录日志而不是崩溃。
- `TlvType` 先覆盖登录、用户、好友、群组、会话、消息、错误和 Bot/Persona 接入需要的字段类型。
- 本 Step 没有定义 TLV value 的二进制格式、长度编码、网络字节序或 Packet header；这些属于 Step 4 和 Step 5。

## Step 4 约束

Step 4 只实现 fixed Packet header 编解码和校验，不进入 TLV body 字段编解码或 TCP 流式解码。

本 Step 允许新增：

- `include/liteim/protocol/Packet.hpp`
- `src/protocol/Packet.cpp`
- `tests/protocol/packet_test.cpp`

本 Step 不允许提前新增：

- `TlvCodec`
- `FrameDecoder`
- `Buffer`
- socket / epoll / Reactor 相关代码

## Step 4 实现结论

- `PacketHeader` 固定 20 字节，字段顺序为 `magic`、`version`、`flags`、`msg_type`、`seq_id`、`body_len`。
- `magic` 固定为 `0x4C494D31`，对应 ASCII `LIM1`。
- `version` 当前固定为 `1`，`flags` 当前固定为 `0`。
- `body_len` 上限为 1MB，避免异常 header 让后续解码器无限缓存。
- Header 多字节字段手动按网络字节序写入和读取，不直接 `memcpy` 整个结构体，避免结构体 padding、对齐和本机字节序影响 wire format。
- `validateHeader()` 只校验 header 级别约束，不检查 TLV body、登录态或业务权限。
- `encodePacket()` 会用 `packet.body.size()` 重新设置 `body_len`，不信任调用方传入的 `packet.header.body_len`。
- `parseHeader()` 只解析 fixed header，不解析 body；完整包拼接和半包/粘包处理属于后续 `FrameDecoder`。
- `liteim_protocol` 因为使用 `Status` / `ErrorCode`，需要链接 `liteim_base`。

## Step 5 约束

Step 5 只实现 TLV body 字段编解码，不进入 TCP 流式解码或网络层。

本 Step 允许新增：

- `include/liteim/protocol/TlvCodec.hpp`
- `src/protocol/TlvCodec.cpp`
- `tests/protocol/tlv_codec_test.cpp`

本 Step 不允许提前新增：

- `FrameDecoder`
- `Buffer`
- socket / epoll / Reactor 相关代码

## Step 5 实现结论

- TLV wire format 固定为 `type(2 bytes) + len(4 bytes) + value(len bytes)`。
- `type` 使用 `TlvType` 的 `std::uint16_t` 编号，`len` 使用 `std::uint32_t`。
- TLV header 和整数 value 都按网络字节序编码。
- `TlvMap` 使用 `std::unordered_map<TlvType, std::vector<std::vector<std::uint8_t>>>`，支持重复字段。
- `parseTlvMap()` 负责通用格式校验和边界检查，不判断业务必需字段。
- `getString()`、`getUint64()`、`getRepeatedString()` 和 `getRepeatedUint64()` 表达必需字段读取；缺失字段返回 `ErrorCode::NotFound`。
- `getUint64()` 要求 value 长度必须是 8 字节，否则返回 `ErrorCode::ParseError`。
- 主动编码 `TlvType::Unknown` 返回 `ErrorCode::InvalidArgument`，避免本端生成无意义字段。
- 当前 TLV 工具层只保留 `String` 和 `Uint64` 两套读写 API；后续确有 signed 字段需求时，再成对添加 `appendInt64()` / `getInt64()`。

## Step 6 约束

Step 6 只实现 socket-agnostic 的 TCP 字节流解包器，不进入 socket、epoll、Reactor、网络 `Buffer` 或 `Session`。

本 Step 允许新增：

- `include/liteim/protocol/FrameDecoder.hpp`
- `src/protocol/FrameDecoder.cpp`
- `tests/protocol/frame_decoder_test.cpp`

本 Step 不允许提前新增：

- `include/liteim/net/`
- `src/net/`
- socket / epoll / Reactor 相关代码

## Step 6 实现结论

- `FrameDecoder` 内部用 `std::vector<std::uint8_t>` 保存未消费字节，后续 Step 7 的网络 `Buffer` 会独立实现。
- `feed()` 每次追加新字节后，可能输出 0 个、1 个或多个完整 `Packet`。
- header 不足 20 字节时只缓存，不返回错误。
- header 足够后调用 `parseHeader()`，复用 Step 4 的 magic/version/body_len 校验。
- 完整帧长度为 `kPacketHeaderSize + header.body_len`，body 不足时继续缓存。
- 错误 magic、version、body_len 超限会让 decoder 进入 error 状态。
- error 状态下后续 `feed()` 直接返回 `ParseError`，等待上层关闭连接或显式 `reset()`。
- `FrameDecoder` 不解析 TLV body，不知道用户名、消息文本或业务类型含义。

## Step 7 约束

Step 7 只实现网络层通用字节缓冲区，不进入 socket、epoll、Reactor 或 `Session`。

本 Step 允许新增：

- `include/liteim/net/Buffer.hpp`
- `src/net/CMakeLists.txt`
- `src/net/Buffer.cpp`
- `tests/net/buffer_test.cpp`

本 Step 不允许提前新增：

- `SocketUtil`
- `Epoller`
- `Channel`
- `EventLoop`
- `Acceptor`
- `Session`
- `TcpServer`

## Step 7 实现结论

- `Buffer` 是 socket-agnostic 字节缓冲区，只负责保存可读数据和可写空间。
- `read_index_` / `write_index_` 划分已读、可读、可写区域。
- `append()` 会在空间不足时调用 `ensureWritableBytes()`，避免调用方直接管理 `std::vector<char>`。
- `ensureWritableBytes()` 优先复用已经 `retrieve()` 掉的前部空间；当前部空间不够时才扩容。
- `retrieve()` 只移动读指针或在读完时重置索引，不缩小底层 `vector`，避免频繁释放和重新分配。
- `retrieveAllAsString()` 返回所有可读字节并清空可读区域，适合测试和后续一次性取出文本 payload。
- `retrieve()` 越界返回 `InvalidArgument`，保持缓冲区原样，避免服务端因异常输入直接崩溃。
- `append(nullptr, nonzero)` 返回 `InvalidArgument`；`append(nullptr, 0)` 作为空追加成功。
- `liteim_net` 链接 `liteim_base`，因为错误路径使用 `Status` / `ErrorCode`。

## Step 8 约束

Step 8 只实现 Linux socket 常用工具函数，不进入 epoll、Reactor 或连接生命周期管理。

本 Step 允许新增：

- `include/liteim/net/SocketUtil.hpp`
- `src/net/SocketUtil.cpp`
- `tests/net/socket_util_test.cpp`

本 Step 不允许提前新增：

- `Epoller`
- `Channel`
- `EventLoop`
- `Acceptor`
- `Session`
- `TcpServer`
- `bind()` / `listen()` / `accept()` 封装

## Step 8 实现结论

- `SocketUtil` 是系统调用薄封装，不拥有 fd 生命周期之外的连接语义。
- `createNonBlockingSocket()` 使用 `AF_INET`、`SOCK_STREAM`、`SOCK_NONBLOCK` 和 `SOCK_CLOEXEC`，保证创建出的 socket 从一开始就是非阻塞并带 close-on-exec。
- `setNonBlocking()` 使用 `fcntl(F_GETFL)` 读取现有 flag，再通过 `fcntl(F_SETFL)` 加上 `O_NONBLOCK`。
- `setReuseAddr()`、`setReusePort()`、`setTcpNoDelay()` 和 `setKeepAlive()` 统一走 `setsockopt()`，并用 `Status` 返回错误。
- `closeFd(int& fd)` 关闭前保存当前 fd，然后立刻把原变量置为 `kInvalidFd`，让同一变量重复调用 `closeFd()` 成为 no-op。
- `closeFd()` 不能保护 fd 整数副本，后续 `Session` / fd owner 仍需要 RAII 管理所有权。
- `getSocketError()` 读取 `SO_ERROR`，后续非阻塞连接、写事件和连接异常处理会复用。
- 负 fd 统一返回 `InvalidArgument`，系统调用失败返回 `IoError`。

## Step 9 约束

Step 9 只定义 Reactor 核心接口，不实现实际 `epoll` 行为和事件循环。

本 Step 允许新增：

- `include/liteim/net/Epoller.hpp`
- `include/liteim/net/Channel.hpp`
- `include/liteim/net/EventLoop.hpp`
- `tests/net/channel_header_test.cpp`
- `tests/net/epoller_header_test.cpp`
- `tests/net/event_loop_header_test.cpp`

本 Step 不允许提前新增：

- `src/net/Epoller.cpp`
- `src/net/Channel.cpp`
- `src/net/EventLoop.cpp`
- `epoll_create1()` / `epoll_ctl()` / `epoll_wait()` 的真实封装实现
- `Channel::handleEvent()` 回调分发实现
- `EventLoop::loop()` 的阻塞循环实现
- `eventfd` 跨线程唤醒
- `Acceptor`、`Session`、`TcpServer`

## Step 9 实现结论

- `Channel.hpp` 只声明 fd 事件代理接口，不拥有 fd，不关闭 fd。
- `Channel` 保存 `owner_loop_`、`fd_`、关注事件 `events_`、本轮实际事件 `revents_` 和四类回调入口。
- `Channel::kReadEvent` 对应 `EPOLLIN | EPOLLPRI`，`Channel::kWriteEvent` 对应 `EPOLLOUT`，本项目当前仍按 LT 模式推进，不在 Step 9 暴露 ET 策略。
- `Channel::update()` 是私有接口，后续由 `enableReading()` / `disableWriting()` 等事件变更函数触发，再交给所属 `EventLoop` 更新 epoll。
- `Epoller.hpp` 只声明 epoll 封装边界，保留 `ChannelList`、`poll()`、`updateChannel()` 和 `removeChannel()` 接口。
- `Epoller` 私有状态预留 `epoll_fd_`、事件数组和 fd 到 `Channel*` 的映射，真实系统调用实现留给 Step 10。
- `EventLoop.hpp` 只声明 Reactor 调度层接口，提供 `loop()`、`quit()`、`updateChannel()`、`removeChannel()` 和线程归属检查。
- `EventLoop` 通过 `std::unique_ptr<Epoller>` 表达“一个 loop 拥有一个 epoller”，但不在 Step 9 实现阻塞循环、任务队列或 `eventfd` 唤醒。

## Step 10 约束

Step 10 只实现 `Epoller` 系统调用封装。

本 Step 允许新增：

- `src/net/Epoller.cpp`
- `src/net/Channel.cpp`
- `tests/net/epoller_test.cpp`

本 Step 允许调整：

- `Epoller` 接口从 `void`/直接返回列表改为 `Status` 返回错误，并通过输出参数返回 active channel list。

本 Step 不允许提前新增：

- `src/net/EventLoop.cpp`
- `Channel::handleEvent()` 回调分发实现
- `Channel::update()` 自动投递到 `EventLoop`
- `EventLoop::loop()` 阻塞循环
- `eventfd` 跨线程唤醒
- `Acceptor`、`Session`、`TcpServer`

## Step 10 设计结论

- `Epoller` 使用 `epoll_create1(EPOLL_CLOEXEC)` 创建 epoll fd，并在析构里关闭 epoll fd。
- 第一版只使用 LT 模式，不设置 `EPOLLET`。
- `epoll_event.data.ptr` 保存 `Channel*`，`poll()` 返回时把它取回，并把事件写入 `Channel::setRevents()`。
- `Epoller` 维护 `fd -> Channel*` 映射，用于判断 `EPOLL_CTL_ADD`、`EPOLL_CTL_MOD` 和 `EPOLL_CTL_DEL`。
- `epoll_wait()` 遇到 `EINTR` 时返回空 active list 和 `Status::ok()`，不把普通信号中断当成致命错误。
- 无效 `Channel*`、负 fd、重复删除等无效操作返回 `Status::error(...)`，不直接退出进程。
- `Channel.cpp` 在本 Step 只实现构造、fd/event/revent 访问、事件 mask 修改和回调 setter；`handleEvent()` 与自动 `update()` 留给后续 Step。

## PersonaAgent 最新路线结论

- PersonaAgent 作为项目二独立推进，不嵌入 C++ LiteIM 服务端。
- PersonaAgent 通过 Python BotClient 使用同一套 TLV 协议登录 LiteIM，作为普通 Bot 用户收消息、发消息。
- AgentService 使用 FastAPI，对 BotClient 暴露 `/chat`。
- LangGraph 第一版收敛为 6 个核心节点：`dialogue_policy`、`retrieve`、`tool_router`、`generate_reply`、`safety_check`、`send_message`。
- `retrieve` 节点内部统一调度 Knowledge RAG、Memory RAG 和 Authorized Style RAG，不再拆成十几个薄节点。
- Authorized Style RAG 是项目二核心卖点，不是普通 few-shot。样本入库前必须有 consent manifest、来源、用途、脱敏、active 状态和撤回机制。
- Style RAG 只能用于授权风格模拟，SafetyGuard 必须拦截冒充真人、越权模仿、隐私泄露和替真人做现实承诺。
- 默认单元测试使用 `MockLLMClient`，真实 GPT / embedding / Chroma 只在集成或演示模式使用。
- Evaluation 必须覆盖 Retrieval Hit@5、Style Similarity、Persona Consistency、Safety Violation Rate、Human Review Trigger Rate、延迟、token cost 和 LiteIM 接入成功率。

## 测试要求

- Step 0 验证 CMake 空骨架可 configure/build。
- Step 1 开始，每个行为变化都要配 GoogleTest 测试。
- Step 2 使用 `tests/base/*_test.cpp` 覆盖默认配置、配置文件覆盖、缺失配置保留默认值、缺失文件、未知 key、非法端口、错误码字符串、`Status` 成功/失败状态、日志级别解析、logger 初始化和时间戳格式。
- Step 3 使用 `tests/protocol/*_test.cpp` 覆盖消息类型字符串、未知类型回退、请求/响应/Push 分类、群列表消息类型和 TLV 字段字符串。
- Step 4 使用 `tests/protocol/packet_test.cpp` 覆盖普通 Packet 编解码、空 body、UTF-8 body、网络字节序、错误 magic、错误 version、body_len 超限、encode 超大 body、不完整 header 和空指针输入。
- Step 5 使用 `tests/protocol/tlv_codec_test.cpp` 覆盖单字段、多字段、UTF-8 字符串、重复字符串字段、重复 `uint64` 字段、`uint64` 网络字节序、TLV len 越界、不完整 TLV header、缺失字段、错误 `uint64` 长度和 Unknown 类型编码。
- Step 6 使用 `tests/protocol/frame_decoder_test.cpp` 覆盖完整包、半包、粘包、半包后接粘包、错误 magic、错误 version、body_len 超限、error 状态拒绝继续解析和空指针输入。
- Step 7 使用 `tests/net/buffer_test.cpp` 覆盖默认状态、append、appendString、`std::uint8_t*` 输入、retrieve、retrieveAll、retrieveAllAsString、自动扩容、前部空间复用、越界 retrieve 和空指针 append。
- Step 8 使用 `tests/net/socket_util_test.cpp` 覆盖非阻塞 socket 创建、plain socket 设置非阻塞、常用 socket option、无效 fd 错误返回、重复关闭保护和 `SO_ERROR` 查询。
- 协议、Buffer、FrameDecoder 等底层模块优先写确定性的 GoogleTest 单元测试。
- 后续业务层测试优先使用 gMock mock `IStorage` / `ICache`，避免单元测试依赖真实 MySQL / Redis。
- 网络行为先写 smoke test，等 CLI / Python 客户端出现后再补 E2E。
- MySQL / Redis 区分纯单元测试和依赖 Docker Compose 的集成测试。
- README 和报告里的 QPS、p99、内存占用只能来自真实压测结果，不能写虚构数字。
