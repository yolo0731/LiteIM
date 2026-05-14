# Step 24：MySqlPool 和 ConnectionGuard

## 0. 本 Step 结论

- 目标：Step 24 的目标是在 Step 23 的单连接封装之上，实现固定大小 MySQL 连接池和 RAII 借还对象。
- 前置依赖：依赖 Step 0-23 已建立的工程、协议或运行时基础。
- 主要交付：`MySqlPool 和 ConnectionGuard` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不实现 DAO。

## 1. 为什么需要这个 Step

Step 24 的目标是在 Step 23 的单连接封装之上，实现固定大小 MySQL 连接池和 RAII 借还对象。

到 Step 23 为止，LiteIM 已经能用一个 `MySqlConnection` 执行 prepared statement。但后续业务线程会并发处理多个请求，不能每个请求都新建连接，也不能让调用方手动记住 release。Step 24 解决的问题是：

```text
多个业务线程如何安全复用有限数量的 MySQL 连接？
```

答案是 `MySqlPool + ConnectionGuard`。

### 概念

连接池的核心职责是：

- 启动时创建固定数量连接。
- 业务线程 acquire 一个空闲连接。
- 连接用完后由 ConnectionGuard 自动归还
- 连接耗尽时等待一段时间。
- 等不到连接时返回超时错误。
- 借出前检查连接是否还活着，失效则重连。
- close 后唤醒等待线程并停止继续借出。

`ConnectionGuard` 是 RAII 借还边界：

```text
MySqlPool::acquire(...)
    -> guard owns one borrowed MySqlConnection*
    -> DAO uses *guard
    -> guard destructor/reset()
    -> connection returns to pool
```

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `MySqlPool 和 ConnectionGuard` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不实现 DAO。
- 不实现 连接最大寿命。
- 不实现 后台保活线程。
- 不实现 动态扩缩容。
- 不实现 prepared statement 缓存。
- 不实现 I/O 线程 MySQL 调用。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/storage/MySqlPool.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/MySqlPool.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/storage/mysql_pool_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tutorials/step24_mysql_pool.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

### `ConnectionGuard`

```cpp
class ConnectionGuard {
public:
    ConnectionGuard() = default;
    ~ConnectionGuard();

    ConnectionGuard(ConnectionGuard&& other) noexcept;
    ConnectionGuard& operator=(ConnectionGuard&& other) noexcept;

    MySqlConnection* get() noexcept;
    const MySqlConnection* get() const noexcept;
    MySqlConnection& operator*() noexcept;
    MySqlConnection* operator->() noexcept;
    explicit operator bool() const noexcept;

    void reset() noexcept;
};
```

所有权：

- `ConnectionGuard` 不拥有 `MySqlConnection` 对象本身。
- 它拥有“借用权”：只要 guard 持有 `connection_`，这条连接就不在 pool 的 idle 队列里。
- 析构或 `reset()` 时把连接归还给 pool。
- 禁止拷贝，允许移动，避免两个 guard 同时归还同一条连接。

关键成员变量：

- `std::shared_ptr<MySqlPoolState> state_`：连接池共享状态。
- `MySqlConnection* connection_{nullptr}`：当前借出的连接。

为什么用 `shared_ptr<MySqlPoolState>`：

如果 pool 对象先析构，而某个业务函数里还持有 guard，guard 归还时不能访问悬空的 `MySqlPool*`。共享状态让已借出的 guard 仍能安全判断池是否关闭，并在必要时直接关闭连接。

常用函数：

- `get()`：返回裸指针，可能为空。
- `operator*()` / `operator->()`：方便 DAO 写 `guard->executeSimple(...)`。
- `operator bool()`：判断是否持有连接。
- `reset()`：主动归还连接，析构时也会调用。

private helper：

- `reset(std::shared_ptr<MySqlPoolState>, MySqlConnection*)` 只给 `MySqlPool` 使用，用来把刚借出的连接交给 guard。

失败语义：

- `ConnectionGuard` 自己的 `reset()` 不返回错误，也不抛异常。
- acquire 的失败由 `MySqlPool::acquire()` 返回 `Status`。

### `MySqlPool`

```cpp
class MySqlPool {
public:
    explicit MySqlPool(MySqlConfig config);
    ~MySqlPool();

    Status start();
    Status acquire(std::chrono::milliseconds timeout, ConnectionGuard& guard);
    void close() noexcept;

    bool started() const noexcept;
    bool closed() const noexcept;
    std::size_t size() const noexcept;
};
```

所有权：

- `MySqlPool` 拥有固定数量的 `std::unique_ptr<MySqlConnection>`。
- 对外只借出非 owning 指针，借还由 `ConnectionGuard` 管理。
- pool 禁止拷贝和移动，避免内部指针队列失效。

`start()`：

- 检查不能重复启动。
- 检查 pool size 必须大于 0。
- 按 `MySqlConfig::pool_size` 创建并连接 MySQL。
- 成功后把所有连接放入 idle 队列。
- 任意连接创建失败则返回失败 `Status`，不进入 started 状态。

`acquire(timeout, guard)`：

- timeout 不能为负数。
- 传入 guard 必须为空，避免覆盖已有借用。
- pool 未 start 或已 close 返回 `InvalidArgument`。
- 没有空闲连接时通过 `condition_variable::wait_for()` 等待。
- 超时返回 `IoError`，不能永久阻塞业务线程。
- 借出前调用 `ping()`；失败时 close 旧连接并按原配置重连。
- 如果检查期间 pool 被 close，关闭连接并返回失败。

`close()`：

- 标记 pool closed。
- 关闭当前 idle 队列里的连接。
- 唤醒所有等待 acquire 的线程。
- 已经借出去的连接归还时不会重新进入 idle 队列，而是直接关闭。

状态查询：

- `started()`：是否已经启动过。
- `closed()`：是否已经关闭。
- `size()`：当前保存的连接数量。

关键成员变量：

- `std::shared_ptr<MySqlPoolState> state_`：保存配置、mutex、condition、连接数组、idle 队列和状态位。

### `MySqlPoolState`

`MySqlPoolState` 在 `.cpp` 内部定义，不暴露给头文件使用者。它包含：

- `MySqlConfig config`：重连时复用。
- `std::mutex mutex`：保护队列和状态。
- `std::condition_variable condition`：等待空闲连接。
- `std::vector<std::unique_ptr<MySqlConnection>> connections`：拥有所有连接。
- `std::deque<MySqlConnection*> idle_connections`：可借出的连接指针。
- `bool started` / `bool closed`：生命周期状态。

它是 Step 24 最关键的 private 结构。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

`MySqlPool` 是后续所有 MySQL DAO 的下层依赖。

DAO 不自己创建连接，而是在每个方法内部：

```text
ConnectionGuard guard;
pool.acquire(timeout, guard);
PreparedStatement statement(*guard);
...
```

业务线程可以并发调用 DAO；连接池负责限制同时打到 MySQL 的连接数量。

### 2. 上下层调用连接

```text
business ThreadPool worker
    -> UserDao / MessageDao / FriendDao / GroupDao
    -> MySqlPool::acquire(timeout, guard)
    -> ConnectionGuard
    -> MySqlConnection
    -> PreparedStatement
    -> MySQL
```

上层只看到 `Status` 和 guard，下层连接创建和重连都由 pool 处理。

### 3. 整体运行链路

服务启动时：

1. 读取 `Config::defaults().mysql` 或配置文件。
2. 构造 `MySqlPool pool(config.mysql)`。
3. 在业务层初始化阶段调用 `pool.start()`。
4. pool 创建 `pool_size` 条连接。
5. 每条连接成功后放入 idle 队列。

业务请求到来时：

1. business worker 调用 DAO。
2. DAO 创建空 `ConnectionGuard`。
3. DAO 调用 `pool.acquire(500ms, guard)`。
4. pool 等待并弹出一条 idle 连接。
5. pool 借出前 `ping()`，必要时重连。
6. DAO 用 `*guard` 执行 prepared statement。
7. 函数返回时 guard 析构，连接回到 idle 队列。

关闭时：

1. 上层调用 `pool.close()`。
2. pool 关闭 idle 连接并唤醒等待者。
3. 已借出连接由 guard 后续归还时关闭。

### 4. 自身内部运行流程

`acquire()` 内部可以拆成 4 段：

```text
参数检查
    -> 加锁检查 started / closed
    -> wait_for 等待 idle_connections 非空
    -> 弹出连接并解锁
    -> ping / reconnect
    -> 把连接交给 ConnectionGuard
```

`releaseConnection()` 内部可以拆成 3 段：

```text
guard reset/destructor
    -> 加锁检查 pool 是否仍 started 且未 closed
    -> 未关闭：push_back 到 idle_connections 并 notify_one
    -> 已关闭：直接 close connection
```

重连逻辑在借出前执行，而不是后台线程执行：

```text
if !connection.isConnected():
    connect(config)
else if ping() failed:
    close()
    connect(config)
```

这样实现简单，且只有真正需要连接时才做恢复。

### 5. 该项目代码在实际应用中的具体数据例子

如果连接池大小是 2，ChatService 可以同时借出两个 `ConnectionGuard`：一个保存 Alice 到 Bob 的 `message_id=5001` 相关消息，另一个查询 `user_id=1001` 的好友列表。第三个业务任务会在 `acquire(timeout)` 等待；guard 析构或 `reset()` 后连接归还池中，避免线程忘记 release 导致连接永久耗尽。

## 6. 关键实现点

- 保持模块职责单一。
- 失败时返回清晰错误，不吞掉异常状态。
- 不跨越本 Step 边界提前实现后续业务。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `MySqlPool 和 ConnectionGuard` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

测试覆盖：

- 头文件自包含。
- pool size 为 0 时启动失败。
- `start()` 创建固定数量连接。
- `acquire()` 能拿到连接并 `ping()`。
- 所有连接借出后再次 acquire 会超时。
- `ConnectionGuard` 析构或 `reset()` 后连接回到 pool。
- 多线程并发 borrow/release 正常。
- pool close 后 acquire 失败。
- 借出的连接被手动 close 后，归还再借出时能重连。

## 8. 验证命令

```bash
cmake --build build
docker compose -f docker/docker-compose.yml ps
ctest --test-dir build -R "MySqlPool" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

### 一句话

Step 24 在 MySQL 单连接封装之上实现固定大小连接池。

### 展开说

可以这样说：

> Step 24 在 MySQL 单连接封装之上实现固定大小连接池。`MySqlPool` 启动时创建 N 条连接，内部用 mutex、condition_variable 和 idle 队列管理可借连接。业务线程调用 `acquire(timeout, guard)`，拿不到连接会超时返回，拿到连接前会 ping 并在失效时重连。`ConnectionGuard` 是 move-only RAII 对象，析构时自动归还连接；如果 pool 已关闭，归还时直接关闭连接，避免访问悬空 pool。

### 容易被追问

- ConnectionGuard 解决了什么问题？
- 为什么 acquire 要有 timeout？

## 10. 面试常见追问

### Q1：ConnectionGuard 解决了什么问题？

它把借连接和还连接绑定到 RAII 生命周期。业务函数中途返回错误时，guard 析构也会归还连接，不会把池耗尽。

### Q2：为什么 acquire 要有 timeout？

MySQL 是阻塞资源。如果连接都被占用，业务线程不能无限挂住；timeout 可以把资源压力转成可观察的 Status。
