# LiteIM Progress

## 2026-05-05 Step 0 Reset

本次进度是 `Step 0: reset workspace`，目标是把 LiteIM 文件夹清理成可以从零教学推进的最小起点。

## 已完成

- 删除旧源码目录：`include/`、`src/`、`server/`。
- 删除旧测试目录：`tests/`。
- 删除旧教程目录：`tutorials/`。
- 删除旧文档目录：`docs/`。
- 删除旧 Qt 临时目录：`client_qt/`。
- 删除旧 SQL 目录：`sql/`。
- 删除旧构建产物：`build/`。
- 删除空的 `.codex` 临时文件。
- 删除未来 Step 才会使用的空目录和 `.gitkeep`。
- 将根 `CMakeLists.txt` 改成 Step 0 空 CMake 骨架。
- 重写 README、task_plan、findings、progress。
- 新增 `docs/architecture.md`、`docs/project_layout.md`、`tutorials/README.md`、`tutorials/step00_reset.md`。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md`，加入 Step 0 说明，并把 Qt 描述统一为常见 IM 三栏布局。

## 当前状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| Step 0 清理 | done | 旧路线代码、测试、教程和 build 产物已删除。 |
| Step 0 最小起点 | done | 不提前保留未来目录，也不保留 `.gitkeep`。 |
| Step 0 文档 | done | README、计划文件、docs 和 tutorial 已更新。 |
| Step 0 验证 | done | CMake、CTest、`.gitkeep` 检查和旧文件名检查已通过。 |
| Step 0 commit | done | 已提交：`29e41e9 chore: keep LiteIM step0 minimal`。 |
| Step 1 | pending | 下一步创建真正可构建的 server/test target。 |

## 下一步

验证 Step 0：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
find . -name .gitkeep
rg -n "SQLiteStorage|step15_sqlite|InMemoryStorage|server/net|server/protocol" .
```

然后进入 Step 1：`init: create LiteIM project structure with googletest`。

## Step 0 最小起点验证结果

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `ctest --test-dir build --output-on-failure`：通过，当前没有测试用例，符合 Step 0 预期。
- `find . -name .gitkeep`：无输出，说明没有 `.gitkeep` 残留。
- 旧路线文件名检查：无 `SQLite`、`InMemory`、`step15`、`server/net`、`server/protocol` 文件路径残留。

## 2026-05-05 Step 1 Project Init

本次进入 `Step 1: init CMake project structure`。

已完成：

- 创建 `server/` 和 `tests/` 目录。
- 更新根 `CMakeLists.txt`，接入 `server` 和 `tests` 子目录。
- 新增 `server/CMakeLists.txt` 和 `server/main.cpp`，生成最小 `liteim_server`。
- 新增 `tests/CMakeLists.txt` 和 `tests/test_main.cpp`，生成最小 `liteim_tests`。
- 将 Step 1 测试从自写 `main()` 改成 GoogleTest：`GTest::gtest_main` + `gtest_discover_tests` + `TEST(SmokeTest, GoogleTestWorks)`。
- 更新 README、docs、findings、task_plan 和 tutorials。
- 新增 `tutorials/step01_project_init.md`。

当前仍然没有创建 `include/`、`src/`、`client_qt/`、`bench/`、`scripts/`、`docker/` 等后续目录。

待验证：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过，生成 `liteim_server`、`liteim_tests`，并构建 GoogleTest / GoogleMock 依赖。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running.`。
- `ctest --test-dir build --output-on-failure`：通过，`SmokeTest.GoogleTestWorks` 1/1 passed。
- `find . -name .gitkeep`：无输出。
- 旧路线文件名检查：无 `SQLite`、`InMemory`、`step15`、`server/net`、`server/protocol`、`gitkeep` 文件路径残留。

提交：

```text
init: create LiteIM project structure with googletest
```


## 2026-05-05 PROJECT_MEMORY PersonaAgent Sync

用户更新了 `/home/yolo/jianli/PROJECT_MEMORY.md`，主要变化集中在项目二 PersonaAgent。

已同步到 LiteIM 文档的结论：

- PersonaAgent 新路线是 Authorized Style RAG Edition。
- PersonaAgent 保持 20 Step，但 Step 7-20 改为 6 节点 LangGraph + Knowledge/Memory/Authorized Style RAG + Persona + Safety + Tool Calling + Checkpoint + Trace + Evaluation。
- LiteIM 不嵌入 Python、LangGraph、LLM、embedding 或 vector DB，只提供 Python BotClient 可以复用的 TLV 协议和 BotGateway 接入点。
- Authorized Style RAG 样本必须有 consent manifest、来源、用途、脱敏、撤回和 SafetyGuard 边界。

## 2026-05-05 Step 2 Base Module

本次进入 `Step 2: add config logger and error code`。

概念完成：

- 明确 `base` 模块只放跨模块基础能力，不放业务和网络逻辑。
- `Config` 统一保存 server、MySQL、Redis、Qt 客户端默认连接配置。
- `Logger` 统一封装 `spdlog`，避免后续模块直接使用 `std::cout`。
- `ErrorCode` + `Status` 统一表达成功/失败，避免到处返回裸字符串。
- `Timestamp` 提供基础时间表达，后续消息、日志、压测统计可复用。

代码完成：

- 更新根 `CMakeLists.txt`，新增 `spdlog` v1.13.0 `FetchContent`，并添加 `src/` 子目录。
- 新增 `src/CMakeLists.txt` 和 `src/base/CMakeLists.txt`。
- 新增 `include/liteim/base/Config.hpp`、`ErrorCode.hpp`、`Status.hpp`、`Logger.hpp`、`Timestamp.hpp`。
- 新增 `src/base/Config.cpp`、`ErrorCode.cpp`、`Status.cpp`、`Logger.cpp`、`Timestamp.cpp`。
- 更新 `server/CMakeLists.txt`，让 `liteim_server` 链接 `liteim_base`。
- 更新 `server/main.cpp`，使用默认配置初始化日志并输出 `0.0.0.0:9000`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/base/` 下的 GoogleTest 文件。

测试完成：

- 新增 `tests/base/config_test.cpp`，覆盖默认配置、文件覆盖、缺失值保留默认值、缺失文件、未知 key、非法端口。
- 新增 `tests/base/error_code_test.cpp`，覆盖 `ErrorCode` 字符串和 `Status` 成功/失败状态。
- 新增 `tests/base/logger_test.cpp`，覆盖日志级别解析、未知级别回退和 logger 初始化。
- 新增 `tests/base/timestamp_test.cpp`，覆盖当前毫秒时间戳和 Unix epoch UTC 格式。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，15/15 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已新增 `tutorials/step02_base.md`。
- 已删除临时 `build/` 产物。
- 提交完成：`feat(base): add config logger and error code`。

恢复说明：

- `planning-with-files` 的 `session-catchup.py` 提示了旧版 Buffer 纯问答上下文，但该内容属于重构前路线，当前 Step 2 以 `/home/yolo/jianli/PROJECT_MEMORY.md` 和当前 LiteIM 文件为准。

## 2026-05-05 Step 3 Protocol Types

本次进入 `Step 3: define MessageType and TLV types`。

概念完成：

- 明确 Step 3 只做协议枚举和分类，不做 Packet 编解码。
- `MessageType` 表示一帧消息的业务类型，后续会进入 Packet header 的 `msg_type` 字段。
- `TlvType` 表示 TLV body 中每个字段的类型，例如用户名、消息文本、群组 ID、错误信息和 Persona ID。
- `Push` 消息用于后续服务端主动投递给接收方，它既不是 request，也不是 response。
- 补充修正：`Push` 需要 `isPushType()` 显式分类，不能只靠 `!isRequestType() && !isResponseType()`，否则会和 `Unknown` 混在一起。

代码完成：

- 新增 `include/liteim/protocol/MessageType.hpp`。
- 新增 `include/liteim/protocol/Tlv.hpp`。
- 新增 `src/protocol/CMakeLists.txt`。
- 新增 `src/protocol/MessageType.cpp`。
- 新增 `src/protocol/Tlv.cpp`。
- 更新 `src/CMakeLists.txt`，接入 `protocol` 子目录。
- 更新 `tests/CMakeLists.txt`，让 `liteim_tests` 链接 `liteim_protocol`。
- 补充修正 `MessageType`：新增 `ListGroupsRequest` / `ListGroupsResponse`，并把群聊消息编号调整为 `406/407/408`。
- 补充 `isPushType()`，显式识别 `PrivateMessagePush`、`GroupMessagePush` 和 `BotMessagePush`。

测试完成：

- 新增 `tests/protocol/message_type_test.cpp`，覆盖核心消息类型字符串、未知消息类型、请求类型分类、响应类型分类、Push 类型分类和 Unknown 不分类。
- 新增 `tests/protocol/tlv_type_test.cpp`，覆盖核心 TLV 字段字符串和未知 TLV 类型。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，23/23 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 3 测试清单。
- 已删除临时 `build/` 产物。
- 提交完成：`feat(protocol): define message and tlv types`。

## 2026-05-05 Step 4 Packet Encoding

本次进入 `Step 4: implement Packet encode/decode`。

概念完成：

- 明确 Step 4 只实现 fixed Packet header 编解码，不实现 TLV body 字段编解码。
- `PacketHeader` 固定 20 字节，字段为 `magic`、`version`、`flags`、`msg_type`、`seq_id`、`body_len`。
- Header 多字节字段使用网络字节序，避免结构体 padding、CPU 字节序和内存对齐影响 wire format。
- `body_len` 最大 1MB，异常 header 直接返回错误。
- TCP 半包 / 粘包处理留给 Step 6 `FrameDecoder`。

代码完成：

- 新增 `include/liteim/protocol/Packet.hpp`。
- 新增 `src/protocol/Packet.cpp`。
- 更新 `src/protocol/CMakeLists.txt`，把 `Packet.cpp` 加入 `liteim_protocol`，并链接 `liteim_base`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/protocol/packet_test.cpp`。
- 新增 `PacketHeader`、`Packet`、`validateHeader()`、`encodePacket()` 和 `parseHeader()`。

测试完成：

- 新增 `tests/protocol/packet_test.cpp`，覆盖普通 Packet 编解码、空 body、UTF-8 body、网络字节序、错误 magic、错误 version、body_len 超限、encode 超大 body、不完整 header 和空指针输入。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，33/33 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已新增 `tutorials/step04_packet.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 4 测试清单。
- 已删除临时 `build/` 产物。
- 提交完成：`feat(protocol): add packet encoding and header validation`。

## 2026-05-05 Step 5 TlvCodec

本次进入 `Step 5: implement TlvCodec`。

开始状态：

- 工作区已有用户未提交改动：`src/base/Config.cpp` 和 `src/protocol/Packet.cpp`。
- 这些改动主要是格式和注释调整，不属于 Step 5 范围；本 Step 保留它们，不纳入 Step 5 commit。

概念完成：

- 明确 Step 5 只实现 TLV body 字段编解码。
- TLV 格式固定为 `type(2 bytes) + len(4 bytes) + value(len bytes)`。
- `type` 和 `len` 使用网络字节序。
- 重复字段通过 `TlvMap` 保存为一个 type 对应多个 value。
- 缺失必需字段由 getter 返回 `NotFound`，不在通用 parser 中判断。
- TCP 半包 / 粘包处理留给 Step 6 `FrameDecoder`。

代码完成：

- 新增 `include/liteim/protocol/TlvCodec.hpp`。
- 新增 `src/protocol/TlvCodec.cpp`。
- 更新 `src/protocol/CMakeLists.txt`，把 `TlvCodec.cpp` 加入 `liteim_protocol`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/protocol/tlv_codec_test.cpp`。
- 新增 `appendString()`、`appendUint64()`、`parseTlvMap()`、`getString()`、`getUint64()`、`getRepeatedString()` 和 `getRepeatedUint64()`。

测试完成：

- 新增 `tests/protocol/tlv_codec_test.cpp`，覆盖单字段、多字段、UTF-8 字符串、重复字符串字段、重复 `uint64` 字段、`uint64` 网络字节序、TLV len 越界、不完整 TLV header、缺失字段、错误 `uint64` 长度和 Unknown 类型编码。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，45/45 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已新增 `tutorials/step05_tlv_codec.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 5 测试清单。
- 本次按用户要求保留本地 `build/` 目录。
- 提交完成：`feat(protocol): implement tlv codec`。

## 2026-05-06 Step 5 TlvCodec API Correction

根据 Step 34 / Step 37 后续好友列表、群列表等真实用例，修正 Step 5 API：

- 删除暂时没有真实字段需求的 `appendInt64()`。
- 新增 `getRepeatedUint64()`，用于读取重复 `FriendId`、`UserId`、`GroupId`、`MessageId` 等 ID 列表。
- 保持 C++17 和当前项目 `Status + output parameter` 风格，不改成 C++20 `std::span` 或 `std::byte`。

## 2026-05-06 Step 6 FrameDecoder

本次进入 `Step 6: implement FrameDecoder`。

开始状态：

- 工作区已有用户暂存改动：`include/liteim/protocol/TlvCodec.hpp`、`src/base/Config.cpp` 和 `src/protocol/Packet.cpp`。
- Step 6 不修改这 3 个文件，不把它们纳入 Step 6 commit。

概念完成：

- 明确 TCP 是字节流，不保留消息边界。
- `FrameDecoder` 只负责连续字节流到完整 `Packet` 的解包。
- 本 Step 不读 socket、不调用 epoll、不解析 TLV body、不做业务路由。
- 错误 header 进入 error 状态，等待上层关闭连接或显式 `reset()`。

代码完成：

- 新增 `include/liteim/protocol/FrameDecoder.hpp`。
- 新增 `src/protocol/FrameDecoder.cpp`。
- 更新 `src/protocol/CMakeLists.txt`，把 `FrameDecoder.cpp` 加入 `liteim_protocol`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/protocol/frame_decoder_test.cpp`。
- 新增 `FrameDecoder::feed()`、`hasError()`、`bufferedBytes()` 和 `reset()`。

测试完成：

- 新增 `tests/protocol/frame_decoder_test.cpp`，覆盖完整包、半包、粘包、半包后接粘包、错误 magic、错误 version、body_len 超限、error 状态拒绝继续解析和空指针输入。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，54/54 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已新增 `tutorials/step06_frame_decoder.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 6 测试清单。
- 本次按用户要求保留本地 `build/` 目录。
- 提交完成：`feat(protocol): implement tcp frame decoder`。

## 2026-05-06 Step 7 Buffer

本次进入 `Step 7: add buffer abstraction`。

概念完成：

- 明确 `Buffer` 是网络层通用字节缓冲区，不读 socket、不调用 epoll、不管理连接生命周期。
- `Buffer` 后续会作为 `Session` 的输入缓冲区和输出缓冲区底座。
- `read_index_` 和 `write_index_` 用来区分已读区域、可读区域和可写区域。
- `ensureWritableBytes()` 优先复用已读空间，空间仍不足时再扩容。
- `retrieve()` 越界返回 `InvalidArgument`，不使用进程级断言处理线上输入错误。

代码完成：

- 新增 `include/liteim/net/Buffer.hpp`。
- 新增 `src/net/CMakeLists.txt`。
- 新增 `src/net/Buffer.cpp`。
- 更新 `src/CMakeLists.txt`，接入 `net` 子目录。
- 新增 `tests/net/buffer_test.cpp`。
- 更新 `tests/CMakeLists.txt`，让 `liteim_tests` 链接 `liteim_net`。

测试完成：

- 新增 `BufferTest.DefaultBufferHasNoReadableBytes`。
- 新增 `BufferTest.AppendIncreasesReadableBytes`。
- 新增 `BufferTest.AppendStringStoresReadableData`。
- 新增 `BufferTest.AppendUint8PointerStoresBytes`。
- 新增 `BufferTest.RetrieveAdvancesReadIndex`。
- 新增 `BufferTest.RetrieveAllResetsBuffer`。
- 新增 `BufferTest.RetrieveAllAsStringReturnsReadableDataAndClearsBuffer`。
- 新增 `BufferTest.EnsureWritableBytesExpandsWhenNeeded`。
- 新增 `BufferTest.EnsureWritableBytesCompactsReadableDataBeforeExpanding`。
- 新增 `BufferTest.AppendExpandsAndPreservesExistingData`。
- 新增 `BufferTest.RetrievePastReadableBytesReturnsError`。
- 新增 `BufferTest.NullAppendWithNonzeroLengthReturnsError`。
- 新增 `BufferTest.NullAppendWithZeroLengthIsOk`。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，67/67 tests passed。
- `git diff --check`：通过。
- `find . -name .gitkeep`：无输出。
- 旧路线文件名检查：没有恢复旧 `SQLiteStorage`、`step15_sqlite`、真实 `server/net` 或 `server/protocol` 文件路径。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已新增 `tutorials/step07_buffer.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 7 测试清单。
- 本次按用户要求保留本地 `build/` 目录。
- 提交完成：`feat(net): add buffer abstraction`。

## 2026-05-06 Step 8 SocketUtil

本次进入 `Step 8: implement SocketUtil`。

开始状态：

- 工作区干净。
- 当前新路线中 Step 7 `Buffer` 已完成，下一步是 `SocketUtil`。
- 旧记忆里曾出现过 Step 8 `EventLoop`，但那属于重启前路线；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 和当前 `task_plan.md` 为准。

概念进行中：

- `SocketUtil` 只封装 Linux socket 常用系统调用。
- 本 Step 不实现 `epoll`、`Channel`、`EventLoop`、`Acceptor`、`Session` 或 `TcpServer`。

概念完成：

- 明确 `SocketUtil` 是 Linux fd 工具层，不拥有连接生命周期。
- `createNonBlockingSocket()` 负责创建 `AF_INET` / `SOCK_STREAM` / `SOCK_NONBLOCK` / `SOCK_CLOEXEC` socket。
- `setNonBlocking()` 使用 `fcntl(F_GETFL)` + `fcntl(F_SETFL)` 补充设置非阻塞。
- socket option 封装只设置常用选项，不在工具函数里绑定地址、监听端口或退出进程。
- `closeFd()` 接收 fd 引用，关闭前保存当前 fd，然后把变量置为 `kInvalidFd`，避免同一变量重复关闭。

代码完成：

- 新增 `include/liteim/net/SocketUtil.hpp`。
- 新增 `src/net/SocketUtil.cpp`。
- 更新 `src/net/CMakeLists.txt`，把 `SocketUtil.cpp` 加入 `liteim_net`。
- 新增 `createNonBlockingSocket()`、`setNonBlocking()`、`setReuseAddr()`、`setReusePort()`、`setTcpNoDelay()`、`setKeepAlive()`、`closeFd()` 和 `getSocketError()`。

测试完成：

- 新增 `tests/net/socket_util_test.cpp`。
- 新增 `SocketUtilTest.CreateNonBlockingSocketReturnsNonblockingFd`。
- 新增 `SocketUtilTest.SetNonBlockingMarksPlainSocketNonblocking`。
- 新增 `SocketUtilTest.SocketOptionsCanBeEnabled`。
- 新增 `SocketUtilTest.InvalidFdReturnsError`。
- 新增 `SocketUtilTest.CloseFdInvalidatesDescriptorAndCanBeRepeated`。
- 新增 `SocketUtilTest.GetSocketErrorReturnsCurrentSoError`。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，73/73 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): add socket utility functions`。

## 2026-05-06 Step 10 Epoller

本次进入 `Step 10: implement Epoller`。

开始状态：

- LiteIM 仓库当前 HEAD 是 `cdfaa14 feat(net): define reactor core interfaces`。
- 工作区干净。
- `session-catchup.py` 提示的未同步内容仍来自旧的纯概念问答，不对应当前 Step 10 代码改动。

概念进行中：

- `Epoller` 是 Reactor 的系统调用层，只封装 `epoll_create1()`、`epoll_ctl()` 和 `epoll_wait()`。
- `Channel` 仍是 fd/event 状态对象，不拥有 fd。
- 本 Step 会补最小 `Channel.cpp` 状态函数以支撑 `Epoller` 测试，但不实现 `Channel::handleEvent()` 回调分发或自动 `EventLoop` 更新。
- 为满足“无效操作返回错误”，`Epoller` 接口会改为返回 `Status`，并通过输出参数返回 active channel list。

TDD RED：

- 更新 `tests/net/epoller_header_test.cpp`，要求 `Epoller::poll()`、`updateChannel()`、`removeChannel()` 返回 `Status`。
- 新增 `tests/net/epoller_test.cpp`，使用真实 `pipe()` fd 覆盖 add、mod、del、timeout 和无效操作。
- 更新 `tests/CMakeLists.txt` 注册 `epoller_test.cpp`。
- 运行 `cmake --build build`，预期失败于 `EpollerHeaderIsSelfContained` 的 `static_assert`，证明当前接口还不满足 Step 10 错误返回要求。

代码完成：

- 更新 `include/liteim/net/Epoller.hpp`，让 `poll()`、`updateChannel()`、`removeChannel()` 返回 `Status`。
- 新增 `src/net/Epoller.cpp`，实现 `epoll_create1(EPOLL_CLOEXEC)`、`EPOLL_CTL_ADD`、`EPOLL_CTL_MOD`、`EPOLL_CTL_DEL` 和 `epoll_wait()`。
- 新增 `src/net/Channel.cpp`，只实现构造、fd/event/revent 访问、读写事件 mask 修改和回调 setter。
- 更新 `src/net/CMakeLists.txt`，把 `Channel.cpp` 和 `Epoller.cpp` 加入 `liteim_net`。
- 本 Step 仍未实现 `Channel::handleEvent()`、`Channel::update()`、`EventLoop::loop()` 或 `eventfd`。

TDD GREEN：

- 运行 `cmake --build build`：通过。
- 运行 `ctest --test-dir build --output-on-failure -R Epoller`：通过，6/6 tests passed。

文档完成：

- 更新 README，把当前状态切到 Step 10，并补充 `Epoller.cpp`、`Channel.cpp`、LT 模式、`Status` 返回和 81 个测试总数。
- 更新 `docs/architecture.md`，补充 `Epoller` 系统调用层行为和当前边界。
- 更新 `docs/project_layout.md`，补充 Step 10 新增源码、测试文件和教程。
- 更新 `tutorials/README.md`，登记 Step 10 教程。
- 更新 `tutorials/step09_reactor_interfaces.md` 中 `Epoller` 的当前 `Status` 接口签名，避免教程和代码漂移。
- 新增 `tutorials/step10_epoller.md`，按概念、接口、实现规则、测试和面试问答展开说明。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 10 测试清单。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，81/81 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): implement epoller wrapper`。

## 2026-05-06 Step 9 Reactor Interfaces

本次进入 `Step 9: define Epoller / Channel / EventLoop interface`。

开始状态：

- 工作区干净。
- 当前新路线中 Step 8 `SocketUtil` 已完成，下一步是 Reactor 核心接口。
- `session-catchup.py` 提示的未同步消息来自之前纯概念问答，没有项目文件改动，本次不把它当作待同步实现内容。

概念进行中：

- `Channel` 表示一个 fd 的事件代理，保存关注事件和回调接口，但本 Step 不实现回调分发。
- `Epoller` 表示 epoll 系统调用封装边界，但本 Step 不调用 `epoll_create1()`、`epoll_ctl()` 或 `epoll_wait()`。
- `EventLoop` 表示 Reactor 调度层接口，拥有 `Epoller` 并管理 `Channel`，但本 Step 不实现阻塞事件循环和 `eventfd` 唤醒。

TDD RED：

- 新增 `tests/net/channel_header_test.cpp`、`tests/net/epoller_header_test.cpp`、`tests/net/event_loop_header_test.cpp`。
- 更新 `tests/CMakeLists.txt` 注册三个接口编译测试。
- 运行 `cmake --build build`，预期失败于 `fatal error: liteim/net/Channel.hpp: No such file or directory`，证明测试能捕获 Step 9 头文件尚未定义的问题。

代码完成：

- 新增 `include/liteim/net/Channel.hpp`，声明 fd、关注事件、实际事件、事件开关、回调设置和 `handleEvent()` 接口。
- 新增 `include/liteim/net/Epoller.hpp`，声明 `poll()`、`updateChannel()` 和 `removeChannel()` 接口。
- 新增 `include/liteim/net/EventLoop.hpp`，声明 `loop()`、`quit()`、`updateChannel()`、`removeChannel()` 和线程归属检查接口。
- 本 Step 没有新增 `src/net/Epoller.cpp`、`Channel.cpp` 或 `EventLoop.cpp`，也没有实现 epoll 系统调用。

TDD GREEN：

- 运行 `cmake --build build`：通过。
- 运行 `ctest --test-dir build --output-on-failure -R ReactorInterfaceTest`：通过，3/3 tests passed。

文档完成：

- 更新 README，把当前状态切到 Step 9，并补充 `Channel` / `Epoller` / `EventLoop` 接口说明和 76 个测试总数。
- 更新 `docs/architecture.md`，补充当前网络层中的 Reactor 接口边界。
- 更新 `docs/project_layout.md`，补充 Step 9 新增头文件、测试文件和教程。
- 更新 `tutorials/README.md`，登记 Step 9 教程。
- 新增 `tutorials/step09_reactor_interfaces.md`，按概念、接口、边界、测试、面试讲法展开说明。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 9 文件清单和测试清单。

错误记录：

- 一次 stale 文案扫描命令把包含反引号的 pattern 放在双引号里，shell 将反引号内容当作命令替换，出现 `Step: command not found`。已改用单引号重新执行，确认只有 Step 8 历史教程保留自身测试说明，不属于 stale 当前状态。
- `/home/yolo/jianli` 不是 Git 仓库，`PROJECT_MEMORY.md` 是工作区级元数据，不能纳入 `LiteIM` 仓库 commit；LiteIM commit 只会包含仓库内 Step 9 文件。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，76/76 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): define reactor core interfaces`。

## 2026-05-06 Step 11 Channel

本次进入 `Step 11: implement Channel`。

开始状态：

- 当前新路线中 Step 10 `Epoller` 已完成并提交。
- 工作区干净。
- `session-catchup.py` 提示的未同步内容来自旧的纯概念问答，不对应当前 Step 11 代码改动。
- 旧记忆里的 Step 11 `Session` 属于重启前路线；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 当前 Step 11 `Channel` 为准。

概念完成：

- `Channel` 是 Reactor 中的 fd 事件分发器，不拥有 fd，不关闭 fd。
- `events_` 表示希望监听的事件，`revents_` 表示本轮 epoll 实际返回的事件。
- `handleEvent()` 根据 `revents_` 分发 read/write/close/error callback。
- `enableReading()`、`enableWriting()` 等关注事件变化函数需要通过 `EventLoop` 更新 epoll 关注状态。
- 本 Step 只补最小 `EventLoop` 更新桥接，不实现 `EventLoop::loop()`、`eventfd` 或跨线程任务队列。

TDD RED：

- 新增 `tests/net/channel_test.cpp`，覆盖事件 mask 修改和 read/write/close/error 回调分发。
- 更新 `tests/CMakeLists.txt` 注册 `channel_test.cpp`。
- 运行 `cmake --build build`，预期失败于 `undefined reference to liteim::Channel::handleEvent()`，证明新增测试能捕获当前缺失行为。

代码完成：

- 更新 `src/net/Channel.cpp`，实现 `handleEvent()` 和私有 `update()`。
- `handleEvent()` 处理 `EPOLLHUP`、`EPOLLERR`、`EPOLLIN`、`EPOLLPRI`、`EPOLLRDHUP` 和 `EPOLLOUT`。
- `handleEvent()` 先拷贝 active events 和 callbacks，降低回调中关闭连接或修改状态带来的成员访问风险。
- `enableReading()`、`disableReading()`、`enableWriting()`、`disableWriting()` 和 `disableAll()` 修改 `events_` 后调用 `update()`。
- 新增 `src/net/EventLoop.cpp`，只实现 `updateChannel()` / `removeChannel()` 到 `Epoller` 的桥接、`quit()` 和线程归属检查。
- 更新 `src/net/CMakeLists.txt`，把 `EventLoop.cpp` 加入 `liteim_net`。

TDD GREEN：

- 运行 `cmake --build build`：通过。
- 运行 `ctest --test-dir build -R ChannelTest --output-on-failure`：通过，7/7 tests passed。

文档完成：

- 更新 README，把当前状态切到 Step 11，并补充 `Channel::handleEvent()`、`Channel::update()`、`EventLoop.cpp` 最小桥接和 88 个测试总数。
- 更新 `docs/architecture.md`，补充 `Channel` 事件分发和当前 `EventLoop` 边界。
- 更新 `docs/project_layout.md`，补充 Step 11 新增源码、测试文件和教程。
- 更新 `tutorials/README.md`，登记 Step 11 教程。
- 新增 `tutorials/step11_channel.md`，按概念、接口、实现规则、测试和面试问答展开说明。
- 更新 `findings.md`、`task_plan.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 11 记录。

错误记录：

- 一次 stale 文案扫描命令又把包含反引号的 pattern 放进双引号，shell 将反引号内容当成命令替换，出现 `Step: command not found`、`Channel::handleEvent: command not found` 等输出。已改用单引号重新执行，确认命中项只是 Step 10 历史进度和 Step 10 计划段落，不是当前状态文案。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，88/88 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): implement channel event dispatching`。

## 2026-05-06 Step 12 EventLoop + eventfd

本次进入 `Step 12: implement EventLoop + eventfd task dispatch`。

开始状态：

- 当前新路线中 Step 11 `Channel` 已完成并提交。
- LiteIM 工作区干净。
- `session-catchup.py` 提示的未同步内容来自旧的纯概念问答，不对应当前 Step 12 代码改动。
- 旧记忆里的 Step 12 `TcpServer` 属于重启前路线；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 当前 Step 12 `EventLoop + eventfd` 为准。

概念完成：

- `EventLoop` 是 Reactor 调度层，负责持有 `Epoller`、阻塞等待 fd 事件、遍历活跃 `Channel` 并调用 `handleEvent()`。
- 每个 `EventLoop` 内置一个 `eventfd` wakeup channel，用于跨线程唤醒阻塞中的 `epoll_wait()`。
- `runInLoop()` 在所属线程立即执行任务，跨线程调用时转入 `queueInLoop()`。
- `queueInLoop()` 把任务放进 mutex 保护的队列，并在需要时写 eventfd 唤醒 loop。
- 本 Step 不实现 `Acceptor`、`Session`、`TcpServer`、业务线程池、MySQL 或 Redis。

TDD RED：

- 更新 `tests/net/event_loop_header_test.cpp`，要求 `EventLoop::Functor`、`runInLoop()` 和 `queueInLoop()` 存在。
- 新增 `tests/net/event_loop_test.cpp`，使用真实线程、真实 pipe fd 和真实 eventfd wakeup 路径验证事件循环。
- 更新 `tests/CMakeLists.txt` 注册 `event_loop_test.cpp`。
- 运行 `cmake --build build`，预期失败于 `EventLoop::Functor does not name a type`、`runInLoop is not a member of EventLoop` 和 `queueInLoop is not a member of EventLoop`。

代码完成：

- 更新 `include/liteim/net/EventLoop.hpp`，新增 `Functor`、`runInLoop()`、`queueInLoop()`、wakeup fd、wakeup channel、pending task 队列、mutex 和 pending-task 执行标记。
- 更新 `src/net/EventLoop.cpp`，实现 `eventfd` 创建、wakeup channel 注册、`loop()`、`quit()`、`runInLoop()`、`queueInLoop()`、`wakeup()`、`handleWakeup()` 和 `doPendingTasks()`。
- `loop()` 先执行 pending tasks，再阻塞 `Epoller::poll(-1)`，返回后遍历活跃 `Channel` 并调用 `handleEvent()`，随后再次执行 pending tasks。
- `quit()` 跨线程调用时写 eventfd，保证阻塞中的 loop 能醒来退出。
- `EventLoop` 析构时从 `Epoller` 移除内部 wakeup channel，并关闭 wakeup fd。

TDD GREEN：

- `cmake --build build`：通过。
- `ctest --test-dir build -R EventLoop --output-on-failure`：通过，5/5 tests passed。

文档完成：

- 新增 `tutorials/step12_event_loop.md`，说明 EventLoop、eventfd、任务队列、线程边界、测试和面试问答。
- 更新 `findings.md` 记录 Step 12 约束和设计结论。
- 更新 README，把当前状态切到 Step 12，并补充 `EventLoop::loop()`、`runInLoop()`、`queueInLoop()`、eventfd wakeup 和 92 个测试总数。
- 更新 `docs/architecture.md`，补充当前网络层中的 `EventLoop` 职责、eventfd wakeup 和线程边界。
- 更新 `docs/project_layout.md`，补充 Step 12 新增测试文件和教程。
- 更新 `tutorials/README.md`，登记 Step 12 教程。
- 更新 `task_plan.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 12 记录。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，92/92 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): add event loop with eventfd task queue`。
