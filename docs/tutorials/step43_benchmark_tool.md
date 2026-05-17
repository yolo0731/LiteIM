# Step 43：自研压测工具

## 0. 本 Step 结论

- 目标：Step 43 增加 `liteim_bench`，用普通 LiteIM TCP/TLV 客户端行为做本地压测。
- 前置依赖：依赖 Step 41 的 CLI 协议流程、Step 42 的端到端验证，以及 Step 34-40 的服务端业务 handler。
- 主要交付：新增 `bench/`、`liteim_bench_core`、`liteim_bench` 和 `Benchmark*` 单元测试。
- 压测边界：工具注册/登录唯一用户，然后发送普通 `PrivateMessageRequest`，不使用特殊测试协议。
- 报告边界：输出 JSON 或 Markdown；README 不写未实测的性能数字。

## 1. 为什么需要这个 Step

单元测试和 E2E 测试能证明功能正确，但不能回答这些运行问题：

- 多个长连接同时存在时，连接建立是否稳定。
- 持续私聊写入 MySQL、刷新 Redis、发送 push 时，端到端延迟是什么量级。
- 慢客户端和输出高水位机制是否能避免内存无限增长。
- 代码修改后，QPS、p95、p99 是否明显退化。

Step 43 的压测工具不是为了制造夸张数据，而是为了给后续优化、本地手动验证和最终简历报告提供可复现的测量入口。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `bench/liteim_bench.cpp` 命令行入口。
- 支持配置 host、port、连接数、消息大小、发送间隔、持续时间、报告格式和用户名前缀。
- 使用 1 个 receiver 连接和 `connections - 1` 个 sender 连接。
- 每个连接注册唯一用户并登录。
- sender 持续发送普通私聊消息到 receiver。
- 统计 connection success、request success、error count、QPS、平均延迟、p50、p95、p99、RSS 和 CPU 使用率。
- 输出 JSON 或 Markdown 报告。

### 本 Step 不做

- 不实现分布式压测、跨机器协调或多进程 worker。
- 不实现复杂场景脚本、图表生成或自动报告归档。
- 不把压测加入默认 CTest 。
- 不修改服务端协议、MySQL schema、Redis key 或业务 service。
- 不提前实现 Step 44 的 gMock、ASan/UBSan 或后续 Qt 客户端。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `bench/CMakeLists.txt` | 新增 | 构建 `liteim_bench_core` 和 `liteim_bench` |
| `bench/Benchmark.hpp` | 新增 | 声明参数、统计结果、报告和 runner 接口 |
| `bench/Benchmark.cpp` | 新增 | 实现参数解析、分位数统计、资源采样和真实压测流程 |
| `bench/liteim_bench.cpp` | 新增 | 命令行入口 |
| `tests/bench/benchmark_test.cpp` | 新增 | 覆盖参数解析、统计和报告格式 |
| `CMakeLists.txt` / `tests/CMakeLists.txt` | 修改 | 注册 bench 目录和测试链接 |
| `README.md` / planning 文件 | 更新 | 记录用法、边界和验证结果 |

## 4. 核心接口与契约

### `BenchmarkOptions`

```cpp
struct BenchmarkOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{9000};
    std::uint32_t connections{10};
    std::size_t message_size{128};
    std::uint32_t send_interval_ms{10};
    std::uint32_t duration_seconds{10};
    ReportFormat format{ReportFormat::Json};
    std::string username_prefix{"bench"};
    std::string password{"bench_secret"};
};
```

契约：

- `connections >= 2`，因为私聊压测需要一个 receiver 和至少一个 sender。
- `message_size > 0`，且第一版限制在 900 KiB 内，避免接近 Packet/TLV 上限。
- `duration_seconds > 0`。
- `format` 只能是 `json` 或 `markdown`。

### `BenchmarkResult`

```cpp
struct BenchmarkResult {
    std::uint64_t connection_attempts;
    std::uint64_t connection_success;
    std::uint64_t request_success;
    std::uint64_t error_count;
    double qps;
    LatencySummary latency;
    double cpu_percent;
    std::uint64_t memory_kb;
};
```

契约：

- `connection_attempts` 是请求的总连接数。
- `connection_success` 表示成功连接并完成注册/登录的连接数。
- `request_success` 只统计收到 `PrivateMessageResponse` 的请求。
- 延迟统计从 sender 发送私聊请求前开始，到收到同 seq response 后结束。
- `cpu_percent` 和 `memory_kb` 是 `liteim_bench` 进程自身的本地采样，不代表 server 进程资源。

## 5. 运行流程

### 1. 启动服务端和压测

```text
docker compose up -d --wait
./build/server/liteim_server
./build/bench/liteim_bench --connections 10 --duration-sec 10
```

### 2. 压测内部流程

```text
parseBenchmarkOptions()
    -> receiver connect/register/login
    -> receiver 后台读 PrivateMessagePush
    -> 创建 connections - 1 个 sender
    -> 每个 sender connect/register/login
    -> 多线程循环发送 PrivateMessageRequest
    -> sender 等待同 seq PrivateMessageResponse
    -> 汇总延迟和错误
    -> 采样 RSS / CPU
    -> renderReport(json 或 markdown)
```

### 3. 具体数据例子

假设命令是：

```bash
./build/bench/liteim_bench --connections 4 --message-size 64 --interval-ms 20 --duration-sec 1
```

内部会生成类似这些用户：

```text
bench_receiver_0_12345_1715840000000
bench_sender_1_12345_1715840000000
bench_sender_2_12345_1715840000000
bench_sender_3_12345_1715840000000
```

receiver 登录后不主动发消息，只负责持续读取 push；三个 sender 每隔 20 ms 发送一条 64 字节文本私聊给 receiver。

## 6. 关键实现点

### 1. 压测仍走普通 IM 协议

`liteim_bench` 不添加专用 benchmark request，也不绕过业务层。它和普通客户端一样注册、登录、发送 `PrivateMessageRequest`，这样测到的是真实 service、MySQL、Redis 和网络回包路径。

### 2. receiver 必须读 push

如果 receiver 在线但不读 push，server 对 receiver 的输出缓冲会增长并触发 backpressure。Step 43 的默认私聊压测关注 sender 请求延迟，所以 receiver 后台读包，避免把“慢客户端测试”混进普通吞吐压测。

### 3. 分位数使用 nearest-rank

`p95` / `p99` 用 nearest-rank 规则：

```text
rank = ceil(q * count)
value = sorted[rank - 1]
```

这个规则简单、可解释，适合第一版本地报告。

### 4. README 不写虚假性能数字

压测数字受 CPU、内存、Docker MySQL/Redis、build type、服务端线程数、消息大小和本机负载影响。Step 43 只提供工具和真实运行入口，不在 README 里预置夸张 QPS。

## 7. 测试设计

| 风险 | 测试覆盖 |
| --- | --- |
| 参数解析错把无效配置放行 | `BenchmarkOptionsTest.ParsesCustomArguments` / `RejectsSingleConnectionPrivateBenchmark` |
| p99 统计偏一位 | `BenchmarkStatsTest.ComputesNearestRankPercentiles` |
| 报告缺关键字段 | `BenchmarkReportTest.RendersJsonReportWithRequiredMetrics` |
| payload 大小影响消息体 | `BenchmarkPayloadTest.BuildsPayloadWithExactSize` |
| 真实压测入口不可运行 | 手动启动 server 后运行小规模 `liteim_bench` smoke |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R Benchmark --output-on-failure
cmake --build build --target liteim_bench -j2
./build/bench/liteim_bench --help
docker compose -f docker/docker-compose.yml up -d --wait
timeout 2s ./build/server/liteim_server || test $? -eq 124
ctest --test-dir build --output-on-failure
git diff --check
```

小规模真实压测需要另开一个终端启动 server：

```bash
./build/server/liteim_server
./build/bench/liteim_bench \
  --connections 4 \
  --message-size 64 \
  --interval-ms 20 \
  --duration-sec 1 \
  --format json
```

本地 smoke 记录：

```text
Date: 2026-05-16
OS: Linux 6.8.0-111-generic x86_64
CPU: 13th Gen Intel(R) Core(TM) i9-13900HX, 32 logical CPUs
Memory: 31 GiB total
Compiler: g++ 13.3.0
Build type: Release
Dependencies: Docker Compose MySQL and Redis healthy on local default ports
Server config: host=0.0.0.0, port=9000, io_threads=4, business_threads=4
Command: ./build/bench/liteim_bench --host 127.0.0.1 --port 9000 --connections 4 --message-size 64 --interval-ms 20 --duration-sec 1 --format json
Result: connection_success=4/4, request_success=114, error_count=0, qps=111.874, average_latency_us=6665.71, p50_us=6494, p95_us=8984, p99_us=9403
```

## 9. 面试表达

> Step 43 增加了一个自研压测工具 `liteim_bench`。它不是 mock 服务端，而是作为真实 TCP/TLV 客户端注册、登录并发送普通私聊消息，统计连接成功、QPS、平均延迟、p50/p95/p99、错误数和本进程资源占用，输出 JSON 或 Markdown 报告。

展开说：

> 我把可测试逻辑拆成 `liteim_bench_core`，单元测试覆盖参数解析、分位数和报告格式。真正压测时，一个 receiver 连接负责读 push，多个 sender 连接循环发送私聊请求并按 seq 等待 response。这样既能压到服务端的 AuthService、ChatService、MySQL、Redis 和网络回包路径，又不会引入专用协议破坏普通 IM 语义。

容易被追问：

> 为什么不把压测放进默认测试？因为压测依赖本机资源、Docker MySQL/Redis 和时间窗口，结果不可稳定断言。真实压测放本地脚本手动执行。

## 10. 面试常见追问

### 为什么 receiver 只读 push 不发消息？

这个工具第一版测的是 sender 的私聊请求闭环：发送请求、服务端保存、推送 receiver、sender 收 response。receiver 持续读 push 是为了避免它变成慢客户端，从而让普通吞吐压测和 backpressure 压测分开。

### connection success 和 request success 有什么区别？

`connection_success` 表示连接建立并完成注册/登录。`request_success` 表示发送私聊后收到了对应 seq 的 `PrivateMessageResponse`。连接成功但请求失败，通常说明业务路径、存储路径或连接后续读写出现问题。

### p99 为什么不用复杂 HDR histogram？

第一版样本量不大，nearest-rank 足够直观，代码和测试也简单。后续如果要做长时间压测或高精度延迟分布，可以引入 histogram 或分桶聚合。

### CPU 和内存采样为什么是 bench 进程自身？

Step 43 的工具是本地客户端压测入口，不直接管理 server 进程。它先记录自身 RSS 和 CPU，保证报告字段完整；服务端资源采样可以在后续压测报告阶段用 `pidstat`、`perf`、cgroup 或 Docker stats 补充。
