# Step 28：RedisClient 和 RedisPool

## 0. 本 Step 结论

- 目标：Step 28 的目标是在 Step 22 的 Redis 服务之上，实现 hiredis 阻塞客户端和固定大小 Redis 连接池。
- 前置依赖：依赖 Step 0-27 已建立的工程、协议或运行时基础。
- 主要交付：`RedisClient 和 RedisPool` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

Step 28 的目标是在 Step 22 的 Redis 服务之上，实现 hiredis 阻塞客户端和固定大小 Redis 连接池。

到 Step 27 为止，LiteIM 的 MySQL DAO 已经能访问用户、消息、好友和群组，但在线状态、未读计数、登录失败限制属于短期状态，更适合 Redis。Step 28 解决的问题是：

```text
C++ 业务线程如何安全执行基础 Redis 命令，并复用 Redis 连接？
```

答案是 `RedisClient + RedisPool + RedisConnectionGuard`。

### 概念

Redis 在 LiteIM 第一版中负责短期状态：

- 在线状态 TTL。
- 未读计数。
- 登录失败限制。

Step 28 只实现基础 Redis 访问能力：

```text
RedisClient
    owns redisContext*
    connect / auth / select db / command execution

RedisPool
    owns fixed RedisClient objects
    acquire / release / ping / reconnect / close
```

这一层仍然是阻塞 API，只能在 business `ThreadPool` 使用。Reactor I/O 线程不能直接执行 Redis 命令。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `RedisClient 和 RedisPool` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/cache/RedisClient.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/cache/RedisPool.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/cache/RedisClient.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/cache/RedisPool.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/cache/redis_client_pool_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tutorials/step28_redis_client_pool.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/cache/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

```cpp
class RedisClient {
public:
    RedisClient() = default;
    ~RedisClient();

    RedisClient(RedisClient&& other) noexcept;
    RedisClient& operator=(RedisClient&& other) noexcept;

    Status connect(const RedisConfig& config);
    Status ping();
    Status setex(const std::string& key, const std::string& value, std::chrono::seconds ttl);
    Status get(const std::string& key, std::optional<std::string>& value);
    Status del(const std::string& key, std::uint64_t& removed_count);
    Status incr(const std::string& key, std::int64_t& value);
    Status expire(const std::string& key, std::chrono::seconds ttl, bool& updated);
    Status eval(const std::string& script,
                const std::vector<std::string>& keys,
                const std::vector<std::string>& args,
                std::optional<std::string>& value);

    void close() noexcept;
    bool isConnected() const noexcept;
};
```

### 所有权和生命周期

- `RedisClient` 拥有一个 `redisContext* context_`。
- 析构时自动 `close()`。
- 禁止拷贝，允许移动。
- 移动后旧对象不再拥有 context。

关键成员变量：

- `redisContext* context_{nullptr}`：hiredis 连接上下文。
- `bool connected_{false}`：连接是否成功建立。

线程边界：

- 单个 `RedisClient` 不加锁。
- 一次只应由一个业务线程使用。
- 连接池通过借出机制保证不会并发使用同一个 client。

### `connect()`

```cpp
Status connect(const RedisConfig& config);
```

流程：

1. 关闭旧 context。
2. 校验 host 非空、port 非 0。
3. 使用 `redisConnectWithTimeout()` 连接，超时固定为 2 秒。
4. 如果 password 非空，执行 `AUTH`。
5. 如果 `db != 0`，执行 `SELECT`。
6. 成功后保持 connected。

失败时会关闭 context 并返回 `IoError` 或 `InvalidArgument`。

### `ping()`

执行 `PING`，期望返回 `PONG`。

连接池借出 client 前会调用它检测连接是否还活着。

### `setex()`

```cpp
Status setex(const std::string& key, const std::string& value, std::chrono::seconds ttl);
```

执行 Redis `SETEX key ttl value`。

输入要求：

- key 非空。
- ttl 必须大于 0。

Step 29 在线状态会用它写 `online:user:<id>` 并设置 TTL。

### `get()`

```cpp
Status get(const std::string& key, std::optional<std::string>& value);
```

执行 `GET`。

语义：

- key 不存在时返回 ok，并把 `value` 置为 `std::nullopt`。
- key 存在时返回字符串。
- 返回类型不是 string/nil 时返回 `IoError`。

### `del()`

```cpp
Status del(const std::string& key, std::uint64_t& removed_count);
```

执行 `DEL`，输出删除数量。

Step 29 下线删除在线状态会用它。

### `incr()`

```cpp
Status incr(const std::string& key, std::int64_t& value);
```

执行 `INCR`，输出递增后的整数值。

Step 30 未读计数和登录失败计数会复用。

### `expire()`

```cpp
Status expire(const std::string& key, std::chrono::seconds ttl, bool& updated);
```

执行 `EXPIRE`，输出是否成功刷新已有 key：

- Redis 返回 1：key 存在并刷新 TTL，`updated=true`。
- Redis 返回 0：key 不存在，`updated=false`。

Step 29 心跳续期会用它。

### `eval()`

```cpp
Status eval(const std::string& script,
            const std::vector<std::string>& keys,
            const std::vector<std::string>& args,
            std::optional<std::string>& value);
```

执行 `EVAL`。

用途：

- 给 Step 30 登录失败限制或更复杂原子操作预留。
- 支持返回 nil、string/status、integer。
- integer 会转成字符串输出。

### 命令执行 helper

`.cpp` 内部所有命令都走 `redisCommandArgv()`，而不是拼接命令字符串。

原因：

- key、value、script 可能包含空格、冒号或特殊字符。
- argv 方式能按参数边界传给 Redis，避免命令拼接歧义。

内部 helper 包括：

- `runCommand()`：统一构造 argv 和 argv length。
- `expectStatusOk()`：检查 `OK`。
- `expectIntegerReply()`：检查整数回复。
- `redisContextError()` / `redisReplyError()`：统一转 `Status`。

### `RedisConnectionGuard`

```cpp
class RedisConnectionGuard {
public:
    RedisConnectionGuard() = default;
    ~RedisConnectionGuard();

    RedisConnectionGuard(RedisConnectionGuard&& other) noexcept;
    RedisConnectionGuard& operator=(RedisConnectionGuard&& other) noexcept;

    RedisClient* get() noexcept;
    const RedisClient* get() const noexcept;
    RedisClient& operator*() noexcept;
    RedisClient* operator->() noexcept;
    explicit operator bool() const noexcept;

    void reset() noexcept;
};
```

它和 MySQL 的 `ConnectionGuard` 作用一致：

- move-only。
- 持有借出的 `RedisClient*`。
- 析构或 `reset()` 时归还 client。
- 内部保存 `std::shared_ptr<RedisPoolState>`，避免 pool 对象析构后 guard 访问悬空指针。

### `RedisPool`

```cpp
class RedisPool {
public:
    explicit RedisPool(RedisConfig config);
    ~RedisPool();

    Status start();
    Status acquire(std::chrono::milliseconds timeout, RedisConnectionGuard& guard);
    void release(RedisConnectionGuard& guard) noexcept;
    void close() noexcept;

    bool started() const noexcept;
    bool closed() const noexcept;
    std::size_t size() const noexcept;
};
```

`start()`：

- 检查 pool size 必须大于 0。
- 创建固定数量 `RedisClient`。
- 每个 client 按 `RedisConfig` connect/auth/select。
- 全部成功后进入 started 状态。

`acquire()`：

- timeout 不能为负数。
- guard 必须为空。
- 未 start 或 closed 返回错误。
- 没有空闲 client 时等待，超时返回 `IoError`。
- 借出前 `ping()`；失败则 close 并重连。
- pool 被 close 时等待者会被唤醒。

`release()`：

- 是显式包装，内部调用 guard.reset()。
- 也可以依赖 guard 析构自动归还。

`close()`：

- 关闭 idle clients。
- 唤醒等待 acquire 的线程。
- 已借出 client 归还时如果 pool 已关闭，会直接 close。

内部 `RedisPoolState` 包含：

- `RedisConfig config`。
- `std::mutex mutex`。
- `std::condition_variable condition`。
- `std::vector<std::unique_ptr<RedisClient>> clients`。
- `std::deque<RedisClient*> idle_clients`。
- `started` / `closed` 状态。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

Step 28 是所有 Redis 业务 cache 的底座。

后续模块会这样使用：

- `OnlineStatusCache`：`setex`、`get`、`expire`、`del`。
- `UnreadCounter`：`incr`、`get`、`del` 或 `expire`。
- `LoginRateLimiter`：`incr`、`expire`、`eval`。

这些操作必须由 business 线程调用，不允许 I/O 线程直接阻塞在 Redis。

### 2. 上下层调用连接

```text
AuthService / ChatService
    -> business ThreadPool
    -> OnlineStatusCache / UnreadCounter / LoginRateLimiter
    -> RedisPool::acquire()
    -> RedisConnectionGuard
    -> RedisClient
    -> hiredis redisContext
    -> Docker Redis
```

`RedisClient` 不知道在线状态 key 格式；它只执行通用 Redis 命令。

### 3. 整体运行链路

服务启动时：

1. 读取 `Config::defaults().redis`。
2. 构造 `RedisPool pool(config.redis)`。
3. 调用 `pool.start()`。
4. pool 创建固定数量 Redis 连接。
5. 每条连接完成 AUTH / SELECT 后进入 idle 队列。

业务请求使用 Redis 时：

1. cache 组件创建 `RedisConnectionGuard`。
2. 调用 `pool.acquire(timeout, guard)`。
3. pool 等待 idle client。
4. 借出前 `ping()`，失败则重连。
5. cache 组件调用 `guard->setex()` / `get()` / `expire()`。
6. guard 析构归还 client。

关闭时：

1. 上层调用 `pool.close()`。
2. pool 关闭 idle clients。
3. 等待者被唤醒并返回 pool closed。
4. 已借出 client 归还时直接 close。

### 4. 自身内部运行流程

Redis 命令流程：

```text
validate client connected
    -> validate key/ttl/script
    -> build argv + argv_lengths
    -> redisCommandArgv()
    -> check REDIS_REPLY_ERROR
    -> convert reply to C++ output parameter
```

连接池借出流程：

```text
validate timeout and empty guard
    -> wait_for idle client
    -> pop idle client
    -> ping
    -> reconnect on ping failure
    -> guard.reset(state, client)
```

归还流程：

```text
guard.reset/destructor
    -> if pool started and not closed: push idle and notify_one
    -> else: close client
```

### 5. 该项目代码在实际应用中的具体数据例子

Redis 开发实例在 `127.0.0.1:63790`，密码 `6`。`RedisClient::setex("online:user:1002", 90, value)` 可以写 Bob 的在线状态，`get()` 读回 value，`expire()` 刷新 TTL。多个业务线程通过 `RedisPool` 借连接，借出前 `PING`，如果测试中关闭了底层连接，下次借出会按原配置重连。

## 6. 关键实现点

- 保持模块职责单一。
- 失败时返回清晰错误，不吞掉异常状态。
- 不跨越本 Step 边界提前实现后续业务。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `RedisClient 和 RedisPool` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

测试覆盖：

- `RedisClient.hpp` / `RedisPool.hpp` 头文件自包含。
- Redis 不可用时返回错误。
- pool size 为 0 时启动失败。
- 真实 Redis `PING`。
- `SETEX` / `GET` round trip。
- missing key 返回 `std::nullopt`。
- `EXPIRE` 刷新 TTL。
- `INCR` 返回递增整数。
- `DEL` 删除 key。
- `EVAL` 读取 key。
- pool acquire / timeout / release。
- 多线程 acquire/release。
- 借出的 client 被 close 后，归还再借出时能重连。

## 8. 验证命令

```bash
cmake --build build
docker compose -f docker/docker-compose.yml ps
ctest --test-dir build -R "RedisClientTest|RedisIntegrationTest|RedisPoolTest|RedisPoolIntegrationTest" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

### 一句话

Step 28 用 hiredis 实现 Redis 阻塞客户端和固定大小连接池。

### 展开说

可以这样说：

> Step 28 用 hiredis 实现 Redis 阻塞客户端和固定大小连接池。`RedisClient` 拥有 `redisContext*`，负责 connect/auth/select、ping、setex、get、del、incr、expire 和 eval。所有命令都用 `redisCommandArgv()` 传参，避免拼接字符串。`RedisPool` 和 `RedisConnectionGuard` 负责线程安全借还、超时等待、借出前 ping 和失败重连。Redis API 是阻塞的，所以只能在 business 线程池里使用，不能放到 Reactor I/O 线程。

### 容易被追问

- 为什么 RedisClient 也是阻塞 API？
- 为什么借出前要 ping/reconnect？

## 10. 面试常见追问

### Q1：为什么 RedisClient 也是阻塞 API？

第一版 Redis 用在 business 线程里，目标是清楚可测的状态访问，不在 Reactor I/O 线程直接调用。异步 Redis 或 pipeline 不是当前边界。

### Q2：为什么借出前要 ping/reconnect？

本地 Redis 或连接可能重启失效。借出前探活可以把坏连接替换掉，让调用方拿到尽量可用的 client。
