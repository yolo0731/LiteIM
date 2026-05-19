# LiteIM 压测报告：本机基准、外部项目对照与面试口径

## 1. 报告结论

本报告记录 2026-05-18 在本机对 LiteIM 进行的 Step 43 压测结果。LiteIM 当前压测不是极限容量测试，而是“真实 TCP/TLV 客户端 -> C++ server -> MySQL/Redis -> response/push”的本机闭环压测。

Step 53 说明：本报告作为最终 README 的压测数据来源保留。报告中的 Git HEAD、工作区状态和命令反映 2026-05-18 当时的本机压测现场；Step 53 未重新运行 benchmark，也不把这些数字改写成生产容量声明。

核心结论：

- 功能稳定性：本次三组压测均为 `error_count = 0`，连接、注册、登录、私聊保存、在线 push 和响应链路正常。
- 本机基准：10 个长连接、128B 消息、每 sender 10ms 间隔时，约 `579.714 QPS`，p99 约 `9.548 ms`。
- 更高压力：30 个长连接、128B 消息、每 sender 5ms 间隔时，约 `758.092 QPS`，p99 约 `48.122 ms`。
- 和 OpenIM、WuKongIM、goim 这类成熟项目相比，LiteIM 的绝对吞吐明显更低；但这些公开数据往往使用更高配置、多节点、专门压测机、批量 fanout 或非持久化广播模型，不能直接等价横比。
- 面试时应表达为：LiteIM 当前是单机学习型 C++ IM 项目，强调网络架构、线程隔离、MySQL/Redis 持久化链路和可复现压测方法，不应宣称百万连接或几十万 QPS。

一句话面试口径：

> 我没有把 LiteIM 包装成百万连接项目，而是做了真实链路压测：在本机 Docker MySQL/Redis 环境下，10 个长连接、128B 消息、10 秒压测约 580 QPS，p99 约 9.5ms；30 连接更高压力下约 758 QPS，p99 约 48ms，三组测试 0 错误。相比 OpenIM、WuKongIM 这类成熟项目绝对吞吐低很多，但测试口径不同，我更看重结果可复现、链路真实、指标不夸大。

## 2. 为什么压测结果会受电脑性能影响

本次压测是本机压测，`liteim_server`、`liteim_bench`、Docker MySQL 和 Docker Redis 都在同一台机器上运行。因此结果同时受以下因素影响：

| 影响因素 | 说明 |
| --- | --- |
| LiteIM 服务器代码 | Reactor、业务线程池、Session 写路径、TLV 编解码、MySQL/Redis 调用、锁和队列都会影响吞吐与延迟 |
| CPU | server、bench、MySQL、Redis 同机抢占 CPU；单核性能和调度会影响 p95/p99 |
| 内存和缓存 | buffer、数据库缓存、Docker 内存压力会影响波动 |
| Docker MySQL/Redis | 本次消息保存和在线状态更新都走本机 Docker 依赖，数据库路径不是纯内存 benchmark |
| 当前系统负载 | 浏览器、IDE、代理、其他服务会影响尾延迟 |
| 构建类型 | 本次是 Release；Debug 或 sanitizer 构建会显著变慢 |
| 压测模型 | Step 43 的 bench 是同步 request/response 模型，不是极限异步打满模型 |

因此，这份报告适合用于说明“当前实现的本机基准”和“后续代码改动的回归对照”，不适合作为跨机器、跨项目的绝对性能排名。

## 3. 本次测试环境

| 项 | 值 |
| --- | --- |
| 测试时间 | 2026-05-18 17:49-17:50 CST |
| 项目路径 | `/home/yolo/jianli/LiteIM` |
| Git HEAD | `d5bfb89` |
| 工作区状态 | 有未提交改动，涉及 `bench/Benchmark.*`、`bench/liteim_bench.cpp`、`client_cli/*`、`include/liteim/base/Types.hpp` |
| OS | Linux 6.8.0-111-generic x86_64 |
| CPU | 13th Gen Intel Core i9-13900HX，32 logical CPUs |
| 内存 | 31 GiB，总可用约 19 GiB |
| 编译器 | g++ 13.3.0 |
| CMake build type | Release |
| MySQL | Docker `mysql:8.0`，healthy，`127.0.0.1:33060` |
| Redis | Docker `redis:7.2-alpine`，healthy，`127.0.0.1:63790` |
| Server 端口 | 临时 server 监听 `0.0.0.0:19000` |
| Server 线程 | `server.io_threads = 4`，`server.business_threads = 4` |
| Server 启动方式 | `./build/server/liteim_server --config <process-substitution config>` |
| Benchmark 工具 | `./build/bench/liteim_bench` |

严谨性说明：

- 本次没有复用已有 9000 端口 server，而是临时启动了一个新的 server 监听 19000，避免混入旧进程状态。
- 测试完成后，临时 server 已通过 SIGTERM 关闭，日志显示收到 signal 15 并进入 shutdown。
- 当前工作区有未提交改动，因此结果代表“当前工作区构建产物”，不是干净 commit 的严格发布基准。

## 4. 验证命令

构建与 benchmark 单元测试：

```bash
cd /home/yolo/jianli/LiteIM

cmake --build build --target liteim_server liteim_bench liteim_tests -j2
ctest --test-dir build -R Benchmark --output-on-failure
```

依赖状态：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
docker compose -f docker/docker-compose.yml ps
```

临时 server 启动命令：

```bash
bash -lc './build/server/liteim_server --config <(printf "%s\n" \
  "server.port = 19000" \
  "server.io_threads = 4" \
  "server.business_threads = 4")'
```

server CPU 采样：

```bash
pidstat -p <liteim_server_pid> 1 10
```

## 5. 本机压测结果

### 5.1 Smoke：4 连接，64B，20ms，1 秒

命令：

```bash
./build/bench/liteim_bench \
  --host 127.0.0.1 \
  --port 19000 \
  --connections 4 \
  --message-size 64 \
  --interval-ms 20 \
  --duration-sec 1 \
  --format json
```

结果：

| 指标 | 值 |
| --- | ---: |
| connection_success | 4 / 4 |
| request_success | 114 |
| error_count | 0 |
| elapsed_ms | 1018 |
| qps | 111.984 |
| average_latency_us | 6695.59 |
| p50_us | 6670 |
| p95_us | 8970 |
| p99_us | 9238 |
| bench_cpu_percent | 0.982318 |
| bench_memory_kb | 4316 |

解释：最小链路 smoke 通过，证明临时 server、Docker MySQL/Redis、注册登录和私聊闭环正常。

### 5.2 基准样本：10 连接，128B，10ms，10 秒

命令：

```bash
./build/bench/liteim_bench \
  --host 127.0.0.1 \
  --port 19000 \
  --connections 10 \
  --message-size 128 \
  --interval-ms 10 \
  --duration-sec 10 \
  --format json
```

结果：

| 指标 | 值 |
| --- | ---: |
| connection_success | 10 / 10 |
| request_success | 5807 |
| error_count | 0 |
| elapsed_ms | 10017 |
| qps | 579.714 |
| average_latency_us | 5446.15 |
| p50_us | 5240 |
| p95_us | 7950 |
| p99_us | 9548 |
| bench_cpu_percent | 4.59219 |
| bench_memory_kb | 4564 |
| server_avg_cpu_percent | 35.00 |

换算：

| 指标 | 值 |
| --- | ---: |
| 平均延迟 | 5.446 ms |
| p50 | 5.240 ms |
| p95 | 7.950 ms |
| p99 | 9.548 ms |

server CPU 采样摘要：

```text
Average: liteim_server %usr=17.60 %system=17.40 %CPU=35.00
```

解释：10 连接下，QPS 约 580，p99 低于 10ms，错误数为 0；这可以作为当前本机基线。

### 5.3 压力样本：30 连接，128B，5ms，10 秒

命令：

```bash
./build/bench/liteim_bench \
  --host 127.0.0.1 \
  --port 19000 \
  --connections 30 \
  --message-size 128 \
  --interval-ms 5 \
  --duration-sec 10 \
  --format json
```

结果：

| 指标 | 值 |
| --- | ---: |
| connection_success | 30 / 30 |
| request_success | 7612 |
| error_count | 0 |
| elapsed_ms | 10041 |
| qps | 758.092 |
| average_latency_us | 33110.3 |
| p50_us | 32591 |
| p95_us | 43338 |
| p99_us | 48122 |
| bench_cpu_percent | 5.37795 |
| bench_memory_kb | 4820 |
| server_avg_cpu_percent | 39.80 |

换算：

| 指标 | 值 |
| --- | ---: |
| 平均延迟 | 33.110 ms |
| p50 | 32.591 ms |
| p95 | 43.338 ms |
| p99 | 48.122 ms |

server CPU 采样摘要：

```text
Average: liteim_server %usr=21.00 %system=18.80 %CPU=39.80
```

解释：30 连接更高压力下，QPS 提升到约 758，但 p99 从约 9.5ms 上升到约 48.1ms。错误仍为 0，说明功能链路稳定；尾延迟上升说明业务线程、数据库写入、同步 request/response 模型或本机资源调度已经开始排队。

## 6. 外部开源项目公开数据对照

这些数据只能作为背景参照，不能直接横向证明谁强谁弱。不同项目的测试目标、机器配置、消息模型、是否持久化、是否多节点、是否 fanout、是否压测客户端和服务端分离都不同。

| 项目 | 公开数据摘要 | 口径 | 与 LiteIM 是否可直接比较 |
| --- | --- | --- | --- |
| OpenIM | 官方 benchmark 示例里，10 个群、每群 5,000 人，共 50,000 在线用户，输入约 1,700 messages/s，平均延迟约 0.202s，最大延迟约 3.641s；另一个 100,000 人商业群样例中，输入约 200 messages/s，服务端 fanout 约 20,000,000 pushed messages/s，平均延迟约 0.805s | 多组件 IM 系统，群聊 fanout 压测，使用专门压测说明和资源配置 | 不能直接比较；OpenIM 是成熟项目，且 fanout pushed messages/s 与 LiteIM sender QPS 不是同一指标 |
| WuKongIM | 官方单节点 v2.1.2 压测中，发送吞吐约 40,000 msg/s，平均发送延迟约 400-600ms；接收吞吐约 200,000 msg/s，延迟小于 1s；混合场景约 12,000 msg/s 发送和 36,000 msg/s 接收 | 单节点、高配置、专门压测机；区分发送吞吐和接收吞吐 | 不能直接比较；它是成熟 IM 网关/消息系统，吞吐目标和优化层级高于 LiteIM 当前阶段 |
| goim | README/pkg.go.dev 公开 benchmark 描述中，1,000,000 在线连接、房间广播 40/s，接收侧约 35,900,000 msg/s，CPU 约 2000%-2300%，内存约 14GB | 大规模在线连接和广播 fanout，偏推送网关模型 | 不能直接比较；这是 fanout 接收速率，不是“每条消息都写 MySQL 再私聊 response”的 QPS |
| Turms | 官方文档明确不发布简化 benchmark 报告，原因包括网络、硬件、OS、JVM、配置、业务场景和数据库设置都会造成巨大差异，并且简单 benchmark 容易误导 | 对 benchmark 方法论的提醒 | 不是性能对照，而是证明“严谨报告必须写清测试条件，不能只报 QPS” |

外部来源：

- OpenIM benchmark 文档：<https://docs.openim.io/guides/benchmark/benchmark_test>
- WuKongIM stress report：<https://docs.githubim.com/en/getting-started/stress-report>
- goim README/pkg.go.dev benchmark：<https://pkg.go.dev/github.com/guojun1992/goim>
- Turms testing 文档：<https://turms-im.github.io/docs/server/development/testing.html>

## 7. 我是否落后

如果只看绝对 QPS，LiteIM 明显落后于成熟开源 IM 项目。

但这个结论需要分层表达：

| 维度 | 判断 |
| --- | --- |
| 和成熟生产 IM 项目比绝对吞吐 | 落后，差距很大 |
| 和普通课程级聊天室 toy project 比 | 不落后，因为 LiteIM 已经有多 Reactor、业务线程池、MySQL/Redis、TLV、CLI/E2E/benchmark 和 sanitizer 测试体系 |
| 和自己的项目阶段目标比 | 当前结果合理，Step 43 的目标是建立可复现压测入口，不是冲击极限吞吐 |
| 面试表达风险 | 不能吹百万连接；应该强调真实链路、指标口径、边界和后续优化方向 |

可以这样对面试官说：

> 如果只拿 QPS 数字和 OpenIM、WuKongIM 这种成熟项目比，我的项目肯定不是一个量级。我的 LiteIM 当前是单机 C++ 后端项目，压测走的是真实私聊链路：客户端注册登录，发送 TLV 私聊，服务端写 MySQL、更新 Redis、在线 push，再返回 response。这个链路在本机 10 连接下约 580 QPS、p99 约 9.5ms，30 连接下约 758 QPS、p99 约 48ms，三组测试都是 0 错误。我不会把它包装成生产级高并发 IM，但它能说明我理解 Reactor、线程隔离、协议编解码、存储缓存和 benchmark 方法，而不是只写了一个内存聊天室。

如果面试官继续问“为什么比公开项目低这么多”，可以回答：

> 公开项目的 benchmark 往往是多节点、高配置、专门压测机，或者统计 fanout pushed messages/s。LiteIM 当前统计的是 sender request/response QPS，而且每条私聊都走 MySQL/Redis 本地 Docker 依赖。我目前更重视指标可复现和不夸大。后续如果要优化，会从异步批量写入、数据库连接池参数、消息落库模型、批量 fanout、压测机分离、server profiling 和跨节点路由几个方向推进。

## 8. 当前性能瓶颈初步判断

从本次结果看：

- 10 连接时 p99 约 9.5ms，server CPU 平均约 35%。
- 30 连接时 QPS 只从约 580 提升到约 758，但 p99 上升到约 48ms，server CPU 平均约 39.8%。

这说明当前瓶颈不一定是纯 CPU 打满，更可能包括：

- sender 同步 request/response 模型导致每连接吞吐受 RTT 限制。
- 每条消息写 MySQL，并处理 offline/online 逻辑，数据库路径影响尾延迟。
- Redis 在线状态、未读计数等缓存调用是阻塞调用，虽然在业务线程池中，但会影响业务线程排队。
- MySQL/Redis 和 server 同机运行，Docker 与 server 抢资源。
- 当前业务线程数是 4，30 连接压力下业务线程池可能出现排队。

这些判断需要进一步用 `perf`、MySQL slow log、Redis latency monitor、业务线程池队列长度指标和 server 侧 tracing 验证。

## 9. 后续优化路线

如果后续要把 LiteIM 的 benchmark 做得更强、更适合简历展示，建议按这个顺序推进：

1. 干净基准：在 clean worktree 上重新跑 3 轮，取平均 QPS、p95、p99 和最大 p99。
2. 分离压测机：让 bench 客户端和 server 不在同一台机器上，避免互相抢 CPU。
3. 采集 server 指标：记录 server RSS、CPU、业务线程池队列长度、MySQL/Redis 耗时。
4. 数据库参数实验：调整 MySQL pool size、Redis pool size、business_threads，对比 4/8/16 线程。
5. flamegraph/profiling：用 `perf` 找 CPU 热点，区分编解码、锁、系统调用、数据库等待。
6. 场景拆分：分别压私聊在线 push、私聊离线落库、群聊 fanout、纯 echo 网络路径。
7. 报告归档：把每轮命令、环境、commit、结果和图表保存为固定格式，避免口说无凭。

## 10. 面试可用精简版

30 秒版本：

> 我做过 LiteIM 的本机真实链路压测，不是 mock。当前 server 是 C++17 Reactor + business thread pool，消息走 TLV 协议、MySQL 持久化和 Redis 在线状态。本机 Docker MySQL/Redis 下，10 个长连接、128B 消息、10 秒压测约 580 QPS，p99 约 9.5ms；30 个连接更高压力下约 758 QPS，p99 约 48ms，三组测试都是 0 错误。我知道这个吞吐和 OpenIM、WuKongIM 这类成熟项目差很多，所以我不会夸大，只把它定位成可复现的单机学习型 benchmark。

2 分钟版本：

> LiteIM 的压测工具 `liteim_bench` 是我自己写的，它不是调用内部接口，而是像真实客户端一样建立 TCP 连接，注册、登录，然后发送普通私聊 TLV 包。server 端会经过 AuthService、ChatService、MySQL、Redis、OnlineService 和 Session push。压测报告里我记录了环境、commit、Docker 依赖、server 线程数、命令和 p50/p95/p99。结果是 10 连接约 580 QPS、p99 约 9.5ms，30 连接约 758 QPS、p99 约 48ms，错误数都是 0。和网上成熟项目相比，比如 OpenIM 和 WuKongIM，绝对吞吐不在一个量级，但它们很多是群聊 fanout、多节点、高配置或者专门网关模型，口径不同。我的重点是证明自己能把网络、协议、存储、缓存、测试和 benchmark 串成完整工程闭环，并且知道指标边界和下一步优化方向。

如果被问“你落后吗”：

> 绝对吞吐上落后，这个我不会回避；但项目阶段和测试口径不同。我目前完成的是可运行、可测试、可压测的单机 IM 后端，不是生产级 IM 集群。下一步如果追性能，我会先做压测机分离、server profiling、数据库耗时统计，再优化业务线程池、MySQL/Redis 调用和 fanout 路径。
