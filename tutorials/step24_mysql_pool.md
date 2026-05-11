# Step 24：MySqlPool 和 ConnectionGuard

## 1. 概念

Step 23 只有单个 `MySqlConnection`。如果每次业务查询都临时创建 TCP/MySQL 连接，会反复做认证、握手和资源分配；如果多个业务线程共享同一个连接，又会产生并发使用同一 `MYSQL*` 的问题。

Step 24 增加固定大小的 MySQL 连接池：

- `MySqlPool` 启动时创建固定数量连接。
- `acquire(timeout, guard)` 从池里借一个连接，连接用完时由 `ConnectionGuard` 自动归还。
- 如果池里没有空闲连接，`acquire()` 最多等到 timeout，然后返回错误，不永久阻塞业务线程。
- 借出前先 `ping()`，连接失效时关闭旧连接并重连。
- `close()` 后不再借出连接；空闲连接立即关闭，已借出的连接在 guard 归还时关闭。

本 Step 仍然不做 DAO、不做 AuthService / ChatService，也不把 MySQL 调用接入网络 I/O 线程。后续业务服务必须在 business `ThreadPool` 里调用连接池，不能在 Reactor 线程里执行阻塞数据库操作。

## 2. hpp 接口说明

### `ConnectionGuard`

`ConnectionGuard` 表达“当前线程临时独占一个 MySQL 连接”：

- 默认构造为空 guard。
- 不可拷贝，可移动，避免两个 guard 同时归还同一连接。
- `get()` 返回当前连接指针，空 guard 返回 `nullptr`。
- `operator->()` / `operator*()` 让调用方像使用 `MySqlConnection` 一样使用借出的连接。
- `operator bool()` 判断是否持有连接。
- `reset()` 手动归还连接；析构函数也会自动归还。

guard 内部保存连接指针和连接池共享状态。即使 `MySqlPool::close()` 已经执行，guard 析构时也能把连接归还到共享状态里，由池决定关闭而不是重新放回空闲队列。调用方仍然应该保证业务代码使用连接的时间短，不要跨请求长期持有 guard。

### `MySqlPool`

`MySqlPool` 负责固定连接数、线程安全借还和关闭：

- `explicit MySqlPool(MySqlConfig config)`：保存 MySQL host、port、user、password、database、pool_size。
- `start()`：创建 `pool_size` 个 `MySqlConnection` 并连接 MySQL；`pool_size == 0` 返回 `InvalidArgument`。
- `acquire(std::chrono::milliseconds timeout, ConnectionGuard& guard)`：等待空闲连接，成功时写入 guard；timeout 为负数或 guard 已持有连接会返回 `InvalidArgument`。
- `close()`：关闭连接池，唤醒正在等待的 acquire；空闲连接立即关闭。
- `started()` / `closed()` / `size()`：测试和诊断用状态查询。

失败语义：

- 未 start 或已 close 时 acquire 返回 `InvalidArgument`。
- 连接池耗尽并等待超时返回 `IoError`。
- MySQL 连接或重连失败时返回底层 `MySqlConnection::connect()` 的错误。

## 3. 作用场景和运行流程

业务层后续典型流程是：

```text
business ThreadPool worker
  -> MySqlPool::acquire(timeout, guard)
  -> PreparedStatement statement(*guard)
  -> prepare / bind / execute
  -> guard 析构自动归还连接
  -> 通过 EventLoop::queueInLoop() 把响应投递回 Session 所属 I/O loop
```

连接池内部流程：

```text
MySqlPool::start()
  -> 创建 N 个 MySqlConnection
  -> 全部 connect() 成功后放入 idle queue

MySqlPool::acquire(timeout, guard)
  -> 等待 idle queue 非空或 pool close
  -> 取出一个连接
  -> ping() 检查
  -> ping 失败则 close() + connect() 重连
  -> 写入 ConnectionGuard

ConnectionGuard::~ConnectionGuard()
  -> releaseConnection()
  -> pool 未关闭：放回 idle queue 并 notify_one()
  -> pool 已关闭：直接 close() 连接
```

小例子：

```cpp
liteim::MySqlPool pool(config.mysql);
const auto start_status = pool.start();

liteim::ConnectionGuard guard;
const auto acquire_status = pool.acquire(std::chrono::milliseconds(200), guard);
if (!acquire_status.isOk()) {
    return acquire_status;
}

liteim::PreparedStatement statement(*guard);
```

线程边界：

- `MySqlPool::acquire()` 可能阻塞等待连接，只能在业务线程池里用。
- 借出的 `MySqlConnection` 在 guard 生命周期内只属于当前借用方，不跨线程共享。
- Reactor I/O 线程不直接调用连接池；数据库结果要投递回对应 `EventLoop`。

## 4. 测试

新增 `tests/storage/mysql_pool_test.cpp`：

- `MySqlPoolTest.HeaderIsSelfContained`：头文件可独立 include，空 guard 初始无连接。
- `MySqlPoolTest.RejectsZeroPoolSize`：拒绝 0 大小连接池。
- `MySqlPoolIntegrationTest.AcquiresConnectedConnection`：启动池并借出可 ping 的连接。
- `MySqlPoolIntegrationTest.AcquireTimesOutWhenAllConnectionsAreBorrowed`：连接耗尽时 acquire 超时返回错误。
- `MySqlPoolIntegrationTest.ConnectionGuardReturnsConnectionOnDestruction`：guard 析构后连接可再次借出。
- `MySqlPoolIntegrationTest.CloseMakesAcquireFail`：close 后 acquire 失败。
- `MySqlPoolIntegrationTest.MultipleThreadsAcquireAndReleaseConnections`：多个线程并发借还连接。
- `MySqlPoolIntegrationTest.ReconnectsConnectionThatWasClosedWhileBorrowed`：借出的连接被关闭后，归还再借出会重连。

这些测试使用 Docker MySQL 默认地址 `127.0.0.1:33060`、用户 `liteim`、密码 `6`。如果本地 MySQL 不可用，集成测试按现有规则 skip。

## 5. 验证命令

```bash
docker compose -f docker/docker-compose.yml up -d --wait
cmake --build build
ctest --test-dir build -R "MySqlPoolTest|MySqlPoolIntegrationTest" --output-on-failure
ctest --test-dir build --output-on-failure
```

本 Step 不改变 `TcpServer`、`Session`、`ThreadPool` 或 MySQL schema。它只把 Step 23 的单连接封装提升成后续 DAO 可以安全复用的连接池边界。
