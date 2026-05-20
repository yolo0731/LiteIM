# Step 56：ThreadPool 队列上限和限流验证

## 0. 本 Step 结论

- 目标：给业务 `ThreadPool` 增加 pending task 队列上限，避免 MySQL / Redis 慢或请求突增时无限堆内存。
- 前置依赖：依赖 Step 17 `ThreadPool`、Step 33 `MessageRouter`、Step 34 `AuthService` 登录限流和 post-review peer IP 修复。
- 主要交付：`ThreadPool(worker_count, max_pending_tasks)`、`ErrorCode::ResourceExhausted`、`server.business_queue_capacity` 配置项、Router submit 失败回 `ErrorResponse` 的测试、真实 Redis peer-IP 限流验证。
- 运行边界：I/O 线程仍只做协议解析和任务提交；业务线程池满时请求被拒绝，不在 I/O 线程里执行阻塞业务兜底。
- 范围控制：不引入熔断框架、不做全局 QPS 限流、不改网络层 output high-water mark。

## 1. 为什么需要这个 Step

Step 17 的第一版 `ThreadPool` 是固定 worker 数，但任务队列没有容量上限。正常情况下没有问题；一旦 MySQL / Redis 卡住，或者客户端持续发大量业务请求，就会出现：

```text
I/O thread 持续收到 LoginRequest / PrivateMessageRequest / HistoryRequest
    -> MessageRouter 持续 submit
    -> business workers 被慢 MySQL / Redis 占住
    -> pending task queue 持续增长
    -> 进程内存被排队任务吃满
```

这不是协议语法错误，而是抗压边界缺失。服务端应该在业务队列满时明确拒绝新业务任务，让客户端收到 `ErrorResponse`，而不是无限排队。

本 Step 把业务线程池变成有界队列：

```text
ThreadPool worker_count = 4
max_pending_tasks = 1024

pending < 1024 -> submit 成功
pending == 1024 -> submit 返回 ResourceExhausted
MessageRouter -> ErrorResponse(ResourceExhausted)
```

同时补一条真实 Redis 集成测试，确认登录失败限制已经按 `username + Session::peerIp()` 生效，而不是退回旧的 `default_remote_ip`。

## 2. 本 Step 边界

本 Step 做：

- 新增 `ErrorCode::ResourceExhausted`。
- `ThreadPool` 构造函数增加 `max_pending_tasks`，默认 0 表示测试/嵌入场景保持无界兼容。
- `ThreadPool::submit()` 在 pending 队列达到上限时返回 `ResourceExhausted`。
- 新增 `ThreadPool::maxPendingTaskCount()` 查询接口。
- `Config` 新增 `business_queue_capacity`，默认 1024。
- 配置文件支持 `server.business_queue_capacity`，并拒绝 0。
- `server/main.cpp` 使用 `ThreadPool(config.business_threads, config.business_queue_capacity)`。
- `MessageRouter` 继续复用已有 submit 失败路径，把 `ResourceExhausted` 转成 `ErrorResponse`。
- AuthService Redis integration 测试验证不同真实 peer IP 的登录失败窗口互相隔离。

本 Step 不做：

- 不实现动态扩缩容。
- 不实现优先级队列。
- 不实现复杂熔断、降级开关或全局令牌桶。
- 不改变 `Session` output high-water mark。
- 不改变 Redis login limiter key 格式。
- 不改变业务 handler 的线程归属。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/base/ErrorCode.hpp` / `src/base/ErrorCode.cpp` | 修改 | 新增 `ResourceExhausted` 错误码 |
| `include/liteim/concurrency/ThreadPool.hpp` / `src/concurrency/ThreadPool.cpp` | 修改 | 增加 pending 队列容量、满队列拒绝和查询接口 |
| `include/liteim/base/Config.hpp` / `src/base/Config.cpp` | 修改 | 增加 `server.business_queue_capacity` 配置项 |
| `server/main.cpp` | 修改 | 用配置容量创建业务线程池 |
| `tests/concurrency/thread_pool_test.cpp` | 修改 | 覆盖满队列拒绝和 0 上限兼容语义 |
| `tests/service/message_router_test.cpp` | 修改 | 覆盖业务 submit 失败返回 `ErrorResponse` |
| `tests/base/config_test.cpp` | 修改 | 覆盖默认值、文件覆盖和 0 容量拒绝 |
| `tests/service/auth_service_test.cpp` | 修改 | 覆盖真实 Redis + peer IP 登录限流隔离 |
| `README.md` / `docs/tutorials/step17_thread_pool.md` / `docs/tutorials/step33_message_router.md` / `docs/tutorials/step34_auth_service.md` / process 文件 | 修改 | 同步队列上限、配置和验证说明 |

## 4. 核心接口与契约

### `ErrorCode::ResourceExhausted`

```cpp
enum class ErrorCode {
    ...
    ResourceExhausted,
};
```

用于表达运行时资源不足，例如业务线程池 pending 队列已满。它不是请求参数错误，所以不使用 `InvalidArgument`。

### `ThreadPool`

```cpp
explicit ThreadPool(std::size_t worker_count,
                    std::size_t max_pending_tasks = 0);

std::size_t maxPendingTaskCount() const noexcept;
Status submit(Task task);
```

契约：

- `worker_count` 仍是固定 worker 数。
- `max_pending_tasks > 0` 表示最多允许多少个任务等待执行。
- `max_pending_tasks == 0` 表示无界队列，主要用于单元测试和历史构造兼容。
- 容量只计算 `tasks_` 队列中的 pending 任务，不包含正在 worker 中执行的任务。
- 队列满时 `submit()` 不入队，返回 `ResourceExhausted`。

### `Config`

```cpp
std::uint32_t business_queue_capacity{1024};
```

配置文件字段：

```text
server.business_queue_capacity = 1024
```

服务端配置不允许 0，因为生产 runtime 需要明确背压边界。`ThreadPool` 构造函数保留 0 无界只是为了兼容局部测试和嵌入使用，不作为服务端默认。

## 5. 运行流程

正常路径：

```text
I/O loop 收到 LoginRequest(seq_id=7)
    -> MessageRouter::route()
    -> business_pool.submit(task)
    -> pending queue 未满，submit ok
    -> worker 执行 AuthService
    -> MessageRouter 统一回 LoginResponse
```

队列满路径：

```text
business workers 正在执行慢任务
pending queue 已达到 server.business_queue_capacity
I/O loop 收到新的 LoginRequest(seq_id=8)
    -> MessageRouter::route()
    -> business_pool.submit(task)
    -> ThreadPool 返回 ResourceExhausted
    -> MessageRouter 立即发送 ErrorResponse(seq_id=8)
```

登录限流验证路径：

```text
Session(peer_ip=198.51.100.10)
    -> username=alice 连续输错密码
    -> Redis key login:failure:...:198.51.100.10 计数增长

Session(peer_ip=198.51.100.11)
    -> username=alice 使用正确密码
    -> 不受 198.51.100.10 的失败窗口影响
```

## 6. 关键实现点

### 1. 队列容量只限制 pending，不限制正在执行

`pendingTaskCount()` 表示还在队列里等待 worker 取走的任务。正在执行的任务已经占用 worker，不算在 pending 容量里。这样 `worker_count` 和 `business_queue_capacity` 两个参数表达清楚：

- `worker_count`：同时执行多少个业务任务。
- `business_queue_capacity`：最多排队多少个等待执行的业务任务。

### 2. Router 不降级到 I/O 线程执行业务

队列满时不能在 I/O 线程直接执行 handler。否则 MySQL / Redis 阻塞又会回到 Reactor 线程，破坏前面的架构边界。正确做法是返回 `ErrorResponse`，让客户端稍后重试。

### 3. session close cleanup 也受队列上限保护

`server/main.cpp` 的 session close cleanup 也通过 `business_pool.submit()` 做 Redis online cleanup。队列满时会记录 warning，不在 I/O callback 里同步访问 Redis。

### 4. peer IP 限流只验证，不重写

post-review hardening 已经让 `AuthService` 优先使用 `Session::peerIp()`。本 Step 加真实 Redis integration 测试，确认同一 username 在不同 peer IP 下是不同限流窗口。

## 7. 测试设计

| 风险 | 测试 |
| --- | --- |
| 队列满仍继续入队 | `BoundedQueueRejectsWhenPendingTaskLimitIsReached` |
| 构造兼容性破坏旧测试 | `ZeroPendingTaskLimitKeepsQueueUnbounded` |
| Router submit 失败没有回包 | `BusinessThreadSubmitFailureReturnsErrorResponse` |
| 配置默认值/解析漂移 | `ConfigTest` 覆盖默认 1024、文件覆盖 64、0 值拒绝 |
| 真实 IP 限流退回旧默认 IP | `LoginRateLimiterSeparatesRealPeerIpWithRedisCache` |
| public 接口形状漂移 | `ConcurrencyInterfaceTest.ThreadPoolHeaderIsSelfContained` |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests liteim_server -j2
ctest --test-dir build -R "ThreadPool|MessageRouter|ConfigTest|AuthService" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

可以这样讲：

> 我给业务线程池补了有界 pending 队列。I/O 线程收到业务包后仍只做 TLV 解析和 `ThreadPool::submit()`；如果业务 worker 都忙且 pending 队列达到 `server.business_queue_capacity`，`submit()` 返回 `ResourceExhausted`，`MessageRouter` 会给客户端回 `ErrorResponse`。这样 MySQL/Redis 慢的时候，服务端不会无限堆积任务导致内存被打爆，也不会把阻塞业务降级回 I/O 线程执行。

更短版本：

> Step56 解决业务层背压：线程池排队有上限，满了就明确拒绝，而不是无限吃内存。

## 10. 面试常见追问

### 为什么队列满时不直接在 I/O 线程执行？

因为这会破坏最重要的架构边界：I/O 线程不能执行 MySQL / Redis 阻塞调用。队列满说明业务层已经过载，正确策略是拒绝或让客户端重试。

### 为什么 `max_pending_tasks = 0` 表示无界？

这是为了兼容已有单元测试和局部嵌入场景。真正 server runtime 通过 `Config` 使用默认 1024，并且配置文件拒绝 0。

### 为什么错误码叫 `ResourceExhausted`？

队列满不是客户端字段格式错，也不是业务对象不存在，而是服务端当前资源不足。单独错误码比复用 `InvalidArgument` 更利于客户端和日志区分。

### 这个队列上限和 output high-water mark 有什么区别？

`business_queue_capacity` 保护业务任务排队内存；`server.output_high_water_mark_bytes` 保护单个慢连接的输出缓冲内存。一个在业务线程池入口，一个在网络发送缓冲。

### 这算完整限流吗？

不是。它是业务线程池背压边界。完整限流还可以按 IP、用户、命令类型做 QPS 或令牌桶；本 Step 只做最关键的“不要无限排队”。

### peer IP 登录限流验证了什么？

验证 `AuthService` 真实使用 `Session::peerIp()` 参与 Redis login failure key。同一 username 在 `198.51.100.10` 输错密码触发限制后，`198.51.100.11` 的正确登录仍可成功，说明不是所有请求都落到旧的默认 IP。
