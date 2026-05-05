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
