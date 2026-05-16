# Step 45：测试覆盖、gMock 和 ASan/UBSan

## 0. 本 Step 结论

- 目标：Step 45 扩充关键模块测试覆盖，引入 gMock 边界测试，并增加 ASan/UBSan 构建入口。
- 主要交付：新增 `MockStorage`、`MockCache` 和 `ServiceMockBoundaryTest`，覆盖 service 与 storage/cache/online 边界。
- 覆盖补强：补充 FrameDecoder、TLV、ThreadPool、TimerHeap 和字符串 `toString()` 测试边界。
- 验证分组：CTest 现在能按 `unit`、`integration`、`mysql`、`redis`、`docker`、`e2e` 标签筛选。
- 构建入口：`-DLITEIM_ENABLE_SANITIZERS=ON` 会在 GNU/Clang 下启用 AddressSanitizer 和 UndefinedBehaviorSanitizer。

## 1. 为什么需要这个 Step

Step 34-44 已经把 LiteIM 的业务服务、E2E 和压测工具串起来，但测试仍有几个缺口：

- fake 测试能验证普通流程，但不容易精确断言 service 是否按正确顺序调用 storage/cache/online 边界。
- 协议解码、线程池和定时器有很多边界输入，单靠主路径测试不够。
- 集成测试依赖 Docker MySQL/Redis，需要能和普通单元测试分开跑。
- 内存越界、悬垂引用和未定义行为不能只靠普通 Release 构建发现。

Step 45 的重点不是改业务语义，而是把现有语义用更硬的测试边界固定下来。

## 2. 本 Step 边界

### 本 Step 做

- 给 `IStorage` / `ICache` 增加测试侧 gMock mock。
- 增加 service mock 边界测试，覆盖 Auth、Chat、Group、History 关键依赖调用。
- 增加 FrameDecoder、TlvCodec、ThreadPool、TimerHeap 的边界测试。
- 修正已有 `const char*` 文本断言，避免 ASan 构建下用指针地址比较字符串。
- 给 CTest 测试注册单元、集成、MySQL、Redis、Docker、E2E 标签。
- 增加 `LITEIM_ENABLE_SANITIZERS` CMake 选项。

### 本 Step 不做

- 不修改 LiteIM TCP/TLV 协议、MessageType、TlvType 或 Packet header。
- 不修改 MySQL schema、Redis key 或业务 service 语义。
- 不替换现有 fake 测试，不大规模重写测试结构。
- 不引入覆盖率工具、CI workflow、Qt 客户端或 PersonaAgent。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `CMakeLists.txt` | 修改 | 增加 `LITEIM_ENABLE_SANITIZERS` 和 sanitizer interface target |
| `tests/CMakeLists.txt` | 修改 | 链接 `GTest::gmock`，注册 mock 测试和 CTest 标签 |
| `tests/mocks/MockStorage.hpp` | 新增 | 用 gMock 覆盖 `IStorage` 接口 |
| `tests/mocks/MockCache.hpp` | 新增 | 用 gMock 覆盖 `ICache` 接口 |
| `tests/service/service_mock_boundary_test.cpp` | 新增 | 验证 service 与 storage/cache/online 边界 |
| `tests/protocol/frame_decoder_test.cpp` | 修改 | 增加 split、粘包、多包和长度边界测试 |
| `tests/protocol/tlv_codec_test.cpp` | 修改 | 增加空 body、重复字段和非法长度测试 |
| `tests/concurrency/thread_pool_test.cpp` | 修改 | 增加空队列 stop/restart 边界 |
| `tests/timer/timer_heap_test.cpp` | 修改 | 增加重复 cancel、未知 cancel 和相同 deadline 测试 |
| `tests/base/error_code_test.cpp` / `tests/protocol/*_type_test.cpp` | 修改 | 用 `EXPECT_STREQ` 比较 C 字符串内容 |
| `README.md` / planning 文件 | 更新 | 记录 Step 45 命令、标签、边界和验证结果 |

## 4. 核心接口与契约

### `MockStorage`

`MockStorage` 位于 `tests/mocks/MockStorage.hpp`，只服务测试，不进入生产 target。

契约：

- 完整 mock `IStorage` 的纯虚接口，避免 service mock 测试漏实现导致抽象边界被绕开。
- 测试用 `EXPECT_CALL` 固定调用参数、返回值和调用顺序。
- 不连接真实 MySQL，也不替代 MySQL integration tests。

### `MockCache`

`MockCache` 位于 `tests/mocks/MockCache.hpp`，对应 `ICache`。

契约：

- 覆盖在线状态、未读计数、登录失败限制和 TTL 刷新接口。
- 用于验证 service 是否在正确场景调用 Redis 抽象。
- 不连接真实 Redis，也不改变已有 Redis integration tests。

### Sanitizer 选项

```bash
cmake -S . -B build-asan -DLITEIM_ENABLE_SANITIZERS=ON
```

契约：

- GNU/Clang 下添加 `-fsanitize=address,undefined`。
- 同时添加 `-fno-omit-frame-pointer`，方便 sanitizer 栈追踪。
- 添加 `-fno-sanitize-recover=all`，发现 sanitizer 错误时直接失败。
- 非 GNU/Clang 编译器开启该选项会 CMake 配置失败，避免静默无效。

## 5. 运行流程

### 1. 普通单元测试路径

```text
cmake --build build --target liteim_tests
ctest --test-dir build -L unit --output-on-failure
```

这里主要跑不依赖 Docker 的 GoogleTest/gMock 用例，例如 `ServiceMockBoundaryTest`、`FrameDecoderTest`、`TlvCodecTest`、`ThreadPoolTest` 和 `TimerHeapTest`。

### 2. 集成测试路径

```text
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build -L integration --output-on-failure
```

集成测试仍保留原有 skip 语义：Docker MySQL/Redis 不可用时，依赖真实服务的测试 skip，不让普通单元测试失败。

### 3. ASan/UBSan 路径

```text
cmake -S . -B build-asan -DLITEIM_ENABLE_SANITIZERS=ON
cmake --build build-asan -j2
ctest --test-dir build-asan --output-on-failure
```

### 4. 具体数据例子

一次私聊 mock 边界测试可以固定这样的调用：

```text
sender user_id = 1001
receiver user_id = 1002
conversation_id = 10011002
message_id = 5001
```

当 `receiver=1002` 在线时，`ChatService` 必须 push 给在线 session，但不能调用 `saveMessageWithOfflineRecipients()` 写离线 recipient，也不能调用 `incrUnread()`。当 `receiver=1002` 离线时，测试反过来断言必须保存 offline recipient，并调用 Redis unread 抽象。

## 6. 关键实现点

### 1. gMock 只压 service 边界

`ServiceMockBoundaryTest` 不复写所有 fake 测试。它只验证更适合 mock 的内容：调用是否发生、参数是否正确、权限校验是否先于真正查询或写入。

### 2. CTest 标签保持可筛选

普通 GoogleTest 通过 `unit` 标签运行。Docker 依赖测试按 MySQL、Redis、MySQL+Redis 和 E2E 分组注册，保证下面这些命令都有结果：

```bash
ctest --test-dir build -L unit
ctest --test-dir build -L integration
ctest --test-dir build -L mysql
ctest --test-dir build -L redis
ctest --test-dir build -L docker
ctest --test-dir build -L e2e
```

### 3. Sanitizer 暴露测试脆弱点

ASan 全量第一次跑出 5 个已有测试失败，根因不是生产代码越界，而是测试用 `EXPECT_EQ` 比较 `const char*` 指针地址。修正为 `EXPECT_STREQ` 后，ASan/UBSan 全量通过。

### 4. 不把 sanitizer 选项写进第三方源码

sanitizer flags 通过 LiteIM 内部 interface target 传递给本项目 target。第三方 GoogleTest/spdlog 仍由 FetchContent 管理，不改第三方源码。

## 7. 测试设计

| 风险 | 测试覆盖 |
| --- | --- |
| Auth 登录限流顺序错 | `AuthLoginChecksLimiterBeforeStorageAndRecordsPasswordFailure` |
| 登录成功未清理失败计数或未绑定在线状态 | `AuthLoginSuccessClearsFailuresAndBindsOnlineState` |
| 在线私聊误写离线和未读 | `ChatOnlineReceiverSkipsOfflineRowsAndUnreadCounter` |
| 离线私聊漏写 offline/unread | `ChatOfflineReceiverWritesOfflineRecipientAndUnread` |
| 群聊保存前漏校验群和成员身份 | `GroupMessageAuthorizesBeforeSaveAndUnreadForOfflineMember` |
| 历史查询绕过权限校验 | `HistoryPrivateQueryChecksMembershipBeforeStorageQuery` / `HistoryGroupQueryAuthorizesGroupMemberBeforeStorageQuery` |
| FrameDecoder 半包/粘包边界退化 | `FrameDecoderSplitTest` 和多包 split 测试 |
| TLV 重复字段或非法长度解析退化 | `TlvCodecTest` 新增边界 |
| ThreadPool/TimerHeap 生命周期边界退化 | 新增 empty stop、duplicate cancel、same deadline 测试 |
| sanitizer 构建无效或测试自身脆弱 | `build-asan` 全量 CTest |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "ServiceMockBoundary|FrameDecoder|TlvCodec|ThreadPool|TimerHeap|TcpServer" --output-on-failure
ctest --test-dir build -L unit --output-on-failure
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build -L integration --output-on-failure
cmake -S . -B build-asan -DLITEIM_ENABLE_SANITIZERS=ON
cmake --build build-asan -j2
ctest --test-dir build-asan --output-on-failure
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
timeout 2s ./build/server/liteim_server || test $? -eq 124
```

本地验证结果：

```text
Targeted mock/protocol/thread/timer/net tests: 78/78 passed
Unit label: 301/301 passed
Integration label: 70/70 passed
ASan/UBSan full CTest: 371/371 passed
Full normal CTest: 371/371 passed
```

## 9. 面试表达

> Step 45 主要补测试工程能力。我给 service 层引入 gMock 边界测试，验证 Auth、Chat、Group、History 对 storage/cache/online 的调用顺序和参数；同时给协议解码、TLV、线程池、定时器补边界用例，并通过 CTest 标签把单元、MySQL、Redis、Docker、E2E 测试分开。最后增加 ASan/UBSan 构建入口，用 sanitizer 全量跑测试。

展开说：

> 我没有把 fake 测试全部重写成 mock，而是保留原有 fake 覆盖普通业务流，只在需要断言边界调用时使用 gMock。这样既减少 churn，又能证明关键依赖调用不会被重构改坏。sanitizer 这次还暴露了测试自身用 `EXPECT_EQ` 比较 C 字符串指针的问题，修成 `EXPECT_STREQ` 后 ASan/UBSan 全量通过。

容易被追问：

> 为什么要给集成测试打标签？因为 MySQL/Redis/E2E 依赖 Docker，本地和 CI 都需要能单独跑 `unit`，也需要能在依赖启动后只跑 `integration`、`mysql`、`redis` 或 `e2e`。

## 10. 面试常见追问

### 为什么不把所有 service fake 测试都换成 gMock？

fake 测试更适合验证完整业务结果，gMock 更适合验证依赖调用边界。全部替换会增加维护成本，也容易让测试过度绑定实现细节。Step 45 只在边界最关键的位置使用 mock。

### sanitizer 是不是等于没有内存问题？

不是。ASan/UBSan 只能覆盖测试跑到的路径，但它能把越界、use-after-free、未定义行为等问题变成明确失败。它和普通单元测试、集成测试、代码审查是互补关系。

### 为什么 integration 标签看起来有组合标签？

GoogleTest discovery 对多标签注册有转义细节。这里用 `integration_mysql_docker` 这类组合标签，同时保留 `-L integration`、`-L mysql`、`-L redis`、`-L docker` 的 regex 筛选能力，保证常用筛选命令有效。

### ASan 第一次失败为什么还算有价值？

第一次失败说明 sanitizer 构建真正生效了。它发现的是测试断言问题：`EXPECT_EQ(const char*, "text")` 比较的是地址，不是字符串内容。修正后同一批 ASan/UBSan 测试全量通过。
