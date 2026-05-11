# Step 28：RedisClient 和 RedisPool

## 1. 概念

Step 28 把 Step 22 启动的 Redis 服务封装成 C++ 阻塞客户端和固定连接池。Redis 在 LiteIM 里只保存在线状态、未读数、登录失败限制这类 cache/state，不保存最终消息实体；消息、用户、好友、群组仍以 MySQL 为准。

本 Step 只做 Redis 基础设施：

- `RedisClient` 封装 hiredis `redisContext` 和基础命令。
- `RedisPool` 管理固定数量 Redis 连接。
- `RedisConnectionGuard` 用 RAII 自动归还借出的连接。
- `liteim_cache` 从接口 target 升级为静态库，并链接 `hiredis`。

Redis 命令是阻塞调用，后续只能放在 business `ThreadPool` 中执行，不能放进 Reactor I/O 线程。I/O 线程仍然只做 socket、epoll、协议编解码和轻量连接生命周期。

本 Step 不实现 `OnlineStatusCache`、未读计数、登录失败 limiter、Redis key 命名规范、业务 service 或网络运行时接入。

## 2. hpp 接口说明

### `RedisClient`

`RedisClient` 拥有一个 hiredis `redisContext*`：

```cpp
class RedisClient {
public:
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

构造后默认未连接。`connect()` 使用 `RedisConfig` 的 host、port、password 和 db：本地默认是 `127.0.0.1:63790`，密码 `6`，db `0`。如果 Redis 不可用、认证失败或选择 DB 失败，返回 `IoError`，不会崩溃。

命令语义：

- `ping()`：验证连接仍可用。
- `setex()`：设置 key/value 和 TTL，TTL 必须为正。
- `get()`：key 不存在时输出 `std::nullopt`。
- `del()`：输出删除的 key 数量。
- `incr()`：输出 Redis 递增后的整数。
- `expire()`：输出是否成功刷新已有 key 的 TTL。
- `eval()`：执行 Lua 脚本，支持 string/status/integer/nil 返回值；integer 会转成字符串输出。

`RedisClient` 不可拷贝，可以 move。析构和 `close()` 都会释放 `redisContext`。

### `RedisPool` 和 `RedisConnectionGuard`

`RedisPool` 依赖 `RedisConfig`：

```cpp
explicit RedisPool(RedisConfig config);

Status start();
Status acquire(std::chrono::milliseconds timeout, RedisConnectionGuard& guard);
void release(RedisConnectionGuard& guard) noexcept;
void close() noexcept;
```

`start()` 一次性创建 `pool_size` 个 `RedisClient` 并连接 Redis。`pool_size == 0` 返回 `InvalidArgument`。

`acquire()` 是阻塞等待接口：

- 有空闲连接时，借出给 `RedisConnectionGuard`。
- 连接耗尽且超过 timeout 时，返回 `IoError`。
- 借出前会 `ping()`；如果连接已经失效，先关闭旧 context，再按原配置重连。
- 如果传入的 guard 已经持有连接，返回 `InvalidArgument`。

`RedisConnectionGuard` 是 move-only RAII 对象。析构、`reset()` 或 `RedisPool::release()` 都会把连接归还池中；如果 pool 已经关闭，归还时关闭连接而不是放回空闲队列。

## 3. 作用场景和运行流程

后续登录成功后的在线状态写入会像这样运行：

```text
business ThreadPool
  -> RedisPool::acquire(timeout, guard)
  -> RedisClient::setex("online:user:<id>", session_info, ttl)
  -> EventLoop::queueInLoop() 返回 LoginResponse
```

连接池借出流程：

```text
RedisPool::acquire(timeout, guard)
  -> 检查 pool 已 start 且未 close
  -> 等待 idle_clients 非空
  -> 超时返回 IoError
  -> 取出 RedisClient*
  -> ping 检查连接
  -> 失败则 close + connect(config) 重连
  -> guard 持有 client
```

命令执行流程：

```text
RedisClient::setex/get/incr/expire/eval
  -> 检查已连接
  -> 检查 key / ttl 等参数
  -> redisCommandArgv()
  -> hiredis reply 转成 Status + 输出参数
  -> freeReplyObject()
```

这里使用 `redisCommandArgv()`，不是手写字符串拼接。原因是 key、value、Lua script 都可能包含空格、引号或特殊字符，argv 方式能把每个参数作为独立 Redis bulk string 发送。

## 4. 测试

新增 `tests/cache/redis_client_pool_test.cpp`：

- `RedisClientTest.HeaderIsSelfContained`：`RedisClient`、`RedisPool`、`RedisConnectionGuard` 头文件可独立使用。
- `RedisClientTest.UnavailableRedisReturnsErrorStatus`：Redis 不可用时返回错误状态，不崩溃。
- `RedisPoolTest.RejectsZeroPoolSize`：拒绝 0 大小连接池。
- `RedisIntegrationTest.ConnectsAndPingsLocalRedis`：连接 Docker Redis 并 PING 成功。
- `RedisIntegrationTest.SetexAndGetRoundTripValue`：`SETEX` 后能 `GET` 回原值。
- `RedisIntegrationTest.GetMissingKeyReturnsEmptyOptional`：不存在 key 返回空 optional。
- `RedisIntegrationTest.ExpireRefreshesTtl`：`EXPIRE` 能刷新已有 key 的 TTL。
- `RedisIntegrationTest.IncrReturnsIncrementedInteger`：`INCR` 返回递增后的整数。
- `RedisIntegrationTest.DelRemovesExistingKey`：`DEL` 删除 key 后再 `GET` 不存在。
- `RedisIntegrationTest.EvalCanReadRedisKey`：`EVAL` 能用 Lua 读取 key。
- `RedisPoolIntegrationTest.AcquiresConnectedRedisClient`：连接池能借出可用 client。
- `RedisPoolIntegrationTest.AcquireTimesOutWhenAllClientsAreBorrowed`：连接耗尽后按 timeout 返回错误。
- `RedisPoolIntegrationTest.ReleaseReturnsClientToPool`：显式 `release()` 后连接可再次借出。
- `RedisPoolIntegrationTest.MultipleThreadsAcquireAndReleaseClients`：多线程借还正常。
- `RedisPoolIntegrationTest.ReconnectsClientThatWasClosedWhileBorrowed`：借出连接被关闭后，下次借出前能重连。

Redis 集成测试使用 `Config::defaults().redis`，也就是 Step 22 的 Docker Redis。Redis 不可用时测试 skip，避免普通无 Docker 环境直接失败。测试 key 使用 `liteim:step28:<pid>:...` 前缀，并在 TearDown 中删除自己创建的 key。

## 5. 验证命令

```bash
docker compose -f docker/docker-compose.yml up -d --wait
cmake --build build
ctest --test-dir build -R "RedisClientTest|RedisIntegrationTest|RedisPoolTest|RedisPoolIntegrationTest" --output-on-failure
ctest --test-dir build --output-on-failure
```

本 Step 不实现 `ICache` 的真实业务方法，不定义线上 key 规范，不接入 AuthService、ChatService、ThreadPool、TcpServer 或 Session。它只提供后续 cache/service 层可以调用的 Redis 基础能力。
