# LiteIM 压测报告：Step58 本机闭环基准

## 1. 报告结论

本报告记录 2026-05-20 在本机对 LiteIM 进行的 Step58 压测结果。当前代码已经补齐离线 ACK、`client_msg_id` 幂等、私聊 delivery ACK、真实 peer IP 登录限流验证、业务线程池队列上限、好友申请和私聊好友权限。

本次 benchmark 仍然是本机闭环压测，不是极限容量测试：

```text
liteim_bench TCP/TLV clients
    -> liteim_server
    -> business ThreadPool
    -> MySQL message persistence
    -> Redis online/unread/rate-limit state
    -> response / private push path
```

核心结果：

- 三组样本均为 `error_count = 0`。
- 4 连接 smoke：约 `110.558 QPS`，p99 约 `9.537 ms`。
- 10 连接基准样本：约 `561.682 QPS`，p99 约 `11.006 ms`。
- 30 连接压力样本：约 `712.749 QPS`，p99 约 `49.918 ms`。
- 相比旧 Step43/Former Step53 报告，当前 benchmark setup 多了好友申请/接受前置，但计时区间仍只统计普通私聊发送阶段。
- 面试时应该表达为“本机可复现的真实链路样本”，不能说成生产容量上限。

一句话面试口径：

> 我没有把 LiteIM 宣称成百万连接项目，而是做了真实链路压测：在本机 Docker MySQL/Redis 环境下，当前可靠投递和好友权限版本，10 个长连接、128B 消息、10 秒压测约 562 QPS，p99 约 11ms；30 连接压力样本约 713 QPS，p99 约 50ms，三组测试 0 错误。这个结果主要用于证明链路可复现、指标不夸大、后续改动可回归比较。

## 2. 测试环境

| 项 | 值 |
| --- | --- |
| 测试时间 | 2026-05-20 CST |
| 项目路径 | `/home/yolo/jianli/LiteIM` |
| Git HEAD | `947f9fc` |
| 说明 | `947f9fc` 是 Step57 功能提交；Step58 只刷新 README、报告、截图和教程材料 |
| OS | Linux 6.8.0-111-generic x86_64 |
| CPU | 13th Gen Intel Core i9-13900HX，32 logical CPUs |
| 内存 | 31 GiB total，测试前约 20 GiB available |
| 编译器 | g++ 13.3.0 |
| CMake build type | Release |
| MySQL | Docker `mysql:8.0`，healthy，`127.0.0.1:33060` |
| Redis | Docker `redis:7.2-alpine`，healthy，`127.0.0.1:63790` |
| Server 端口 | 临时 server 监听 `0.0.0.0:19058` |
| Server 线程 | `server.io_threads = 4`，`server.business_threads = 4` |
| Business queue | `server.business_queue_capacity = 1024` |
| Benchmark 工具 | `./build/bench/liteim_bench` |

依赖状态：

```text
mysql  mysql:8.0          healthy  127.0.0.1:33060->3306/tcp
redis  redis:7.2-alpine   healthy  127.0.0.1:63790->6379/tcp
```

## 3. 压测模型

`liteim_bench` 使用一个 receiver 连接和 `connections - 1` 个 sender 连接。每个连接都是真实 TCP/TLV 客户端。

计时前 setup：

- 为 receiver 和 senders 注册唯一用户。
- 所有用户登录。
- 每个 sender 向 receiver 发送 `AddFriendRequest`。
- receiver 对每个 sender 发送 `AcceptFriendRequest`。

计时区间：

- sender 循环发送普通 `PrivateMessageRequest`。
- server 按真实业务流程保存消息、检查好友权限、判断在线接收方、响应 sender，并尝试 push receiver。
- benchmark 统计 sender 的 request/response RTT，不统计 setup 耗时，也不统计 receiver push 到达后的 delivery ACK 延迟。

因此这份报告适合回答：

- 真实 TCP/TLV 链路是否能跑通。
- MySQL/Redis 依赖下的本机 request/response 延迟大概是多少。
- 可靠性/权限 hardening 后是否出现明显错误回归。

这份报告不适合回答：

- LiteIM 的生产最大连接数。
- 多节点 fanout 能力。
- 群聊全员 ACK 延迟。
- 服务器独立 CPU/RSS 上限。

## 4. 验证命令

构建：

```bash
cd /home/yolo/jianli/LiteIM
cmake --build build --target liteim_server liteim_bench liteim_tests -j2
```

启动依赖：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
docker compose -f docker/docker-compose.yml ps
```

临时 server 配置：

```text
server.port = 19058
server.io_threads = 4
server.business_threads = 4
server.business_queue_capacity = 1024
```

## 5. 本机压测结果

### 5.1 Smoke：4 连接，64B，20ms，1 秒

命令：

```bash
./build/bench/liteim_bench \
  --host 127.0.0.1 \
  --port 19058 \
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
| request_success | 111 |
| error_count | 0 |
| elapsed_ms | 1004 |
| qps | 110.558 |
| average_latency_us | 7044.14 |
| p50_us | 7005 |
| p95_us | 8770 |
| p99_us | 9537 |
| bench_cpu_percent | 0.996016 |
| bench_memory_kb | 4328 |

### 5.2 Baseline：10 连接，128B，10ms，10 秒

命令：

```bash
./build/bench/liteim_bench \
  --host 127.0.0.1 \
  --port 19058 \
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
| request_success | 5623 |
| error_count | 0 |
| elapsed_ms | 10011 |
| qps | 561.682 |
| average_latency_us | 5946.29 |
| p50_us | 5763 |
| p95_us | 8725 |
| p99_us | 11006 |
| bench_cpu_percent | 4.39517 |
| bench_memory_kb | 4548 |

换算：

| 指标 | 值 |
| --- | ---: |
| 平均延迟 | 5.946 ms |
| p50 | 5.763 ms |
| p95 | 8.725 ms |
| p99 | 11.006 ms |

### 5.3 Stress sample：30 连接，128B，5ms，10 秒

命令：

```bash
./build/bench/liteim_bench \
  --host 127.0.0.1 \
  --port 19058 \
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
| request_success | 7156 |
| error_count | 0 |
| elapsed_ms | 10040 |
| qps | 712.749 |
| average_latency_us | 35536.3 |
| p50_us | 35289 |
| p95_us | 44082 |
| p99_us | 49918 |
| bench_cpu_percent | 5.27888 |
| bench_memory_kb | 4668 |

换算：

| 指标 | 值 |
| --- | ---: |
| 平均延迟 | 35.536 ms |
| p50 | 35.289 ms |
| p95 | 44.082 ms |
| p99 | 49.918 ms |

## 6. 解释和风险边界

当前结果和 2026-05-18 报告相比略低，原因不能简单归结为某一处代码变化：

- 当前版本增加了好友权限、ACK、幂等和队列限制等真实业务边界。
- benchmark setup 必须建立 accepted friendship，虽然 setup 不计入发送阶段，但会改变测试前数据库状态。
- server、bench、MySQL、Redis 仍然同机运行，尾延迟会受到本机后台负载影响。
- 本报告没有采集 server 进程的独立 `pidstat` 样本，只记录 benchmark 客户端 CPU/RSS。

可靠表达：

- 可以说：当前版本在本机真实 MySQL/Redis 链路下，三组 benchmark 均 0 错误。
- 可以说：当前本机 10 连接样本约 562 QPS，p99 约 11ms。
- 不应该说：LiteIM 支持百万连接、生产级吞吐、跨节点可靠投递或完整已读回执。

