# Step 40：HeartbeatService 心跳 TTL 刷新

## 0. 本 Step 结论

- 目标：Step 40 把 `HeartbeatRequest` 从 `MessageRouter` 默认 inline 回包升级为业务层 `HeartbeatService`。
- 前置依赖：依赖 Step 32 `OnlineService` 的 session/user 绑定和 Redis 在线状态刷新能力，依赖 Step 33 `MessageRouter` 的 business thread dispatch。
- 主要交付：新增 `HeartbeatService`、service 测试、server runtime 注册、README 和本文档。
- 线程边界：`HeartbeatService` 通过 business `ThreadPool` 执行，Redis TTL 刷新不进入 Reactor I/O 线程。
- 语义边界：`HeartbeatResponse` 只表示服务端成功收到并处理合法心跳包，不保证 Redis 在线状态 TTL 一定刷新成功。
- 范围控制：本 Step 不修改 `Session::last_active_time` 语义；完整合法入站包已经在 `Session` 读路径刷新连接活跃时间。

## 1. 为什么需要这个 Step

Step 18 已经有 `timerfd` 周期扫描，用于关闭长时间没有合法入站包的连接。Step 33 的 `MessageRouter` 也有默认 `HeartbeatRequest -> HeartbeatResponse`，但它只是 inline 空回包，不知道当前连接是否已登录，也不会刷新 Redis 在线状态 TTL。

Step 40 解决的问题是：

- 未登录连接可以发心跳，服务端确认协议链路正常。
- 已登录连接发心跳时，服务端尝试刷新 `online:user:<user_id>` 的 Redis TTL。
- Redis TTL 刷新失败不污染客户端心跳协议，只记录 warning，依赖后续心跳恢复。
- Redis 阻塞调用不进入 I/O 线程，继续保持业务线程池隔离边界。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `HeartbeatService`。
- 注册 `MessageType::HeartbeatRequest`。
- `HeartbeatRequest` 通过 business `ThreadPool` 执行。
- 未登录 session 返回 `HeartbeatResponse`，不写 Redis。
- 已登录 session 调用 `OnlineService::refreshUserOnline(user_id, session_id)`。
- Redis TTL 刷新失败时记录 warning，仍返回 `HeartbeatResponse`。
- 响应保留请求 `seq_id`。

### 本 Step 不做

- 不直接更新 `Session::last_active_time`。
- 不把 Redis TTL 刷新失败返回给客户端。
- 不关闭连接，不清理 `SessionManager` 绑定。
- 不新增心跳 request / response TLV 字段。
- 不新增 metrics 模块；当前第一版通过 warning 日志暴露 Redis 刷新失败。
- 不实现客户端重连策略、断线提示 UI、BotGateway 或跨节点在线状态路由。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/service/HeartbeatService.hpp` | 新增 | 声明心跳业务 service、handler 注册和处理函数 |
| `src/service/HeartbeatService.cpp` | 新增 | 实现未登录回包、已登录 Redis TTL 刷新和失败降级日志 |
| `src/service/CMakeLists.txt` | 修改 | 把 `HeartbeatService.cpp` 编入 `liteim_service` |
| `server/main.cpp` | 修改 | 创建 `HeartbeatService` 并注册 `HeartbeatRequest` handler |
| `tests/service/heartbeat_service_test.cpp` | 新增 | 覆盖 Step 40 心跳语义和 router 注册 |
| `tests/CMakeLists.txt` | 修改 | 接入 Step 40 service 测试 |
| `README.md` | 更新 | 记录 Step 40 runtime 和 Redis TTL 降级语义 |
| `tutorials/step40_heartbeat_service.md` | 新增 | 讲解心跳业务层语义 |
| `task_plan.md / findings.md / progress.md` | 更新 | 记录 Step 40 过程、边界和验证结果 |

## 4. 核心接口与契约

### `HeartbeatService`

```cpp
class HeartbeatService {
public:
    explicit HeartbeatService(OnlineService& online_service);

    Status registerHandlers(MessageRouter& router);
    Status handleHeartbeat(const MessageRouter::RouterRequest& request, Packet& response);
};
```

构造语义：

- `HeartbeatService` 不拥有 `OnlineService`，只保存引用。
- `OnlineService` 的生命周期必须长于 `HeartbeatService`。
- `OnlineService` 内部继续协调 `SessionManager` 和 `ICache`，`HeartbeatService` 不直接访问 Redis 组件。

`registerHandlers()` 契约：

- 注册 `MessageType::HeartbeatRequest`。
- dispatch mode 是 `BusinessThread`。
- 这样已登录心跳里的 Redis TTL 刷新不会阻塞 I/O 线程。
- 这个注册会覆盖 `MessageRouter` 构造时提供的默认 inline 心跳 handler。

`handleHeartbeat()` 契约：

- request 必须来自有效 session。
- 响应类型固定为 `HeartbeatResponse`。
- 响应 `seq_id` 跟请求 `seq_id` 一致。
- 请求 body 第一版为空，不读取额外 TLV。
- 未登录 session 返回成功，不写 Redis。
- 已登录 session 尝试刷新 Redis 在线 TTL。
- Redis 刷新失败只记录 warning，返回值仍是 `Status::ok()`。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

Alice 登录成功后：

```text
user_id = 1001
session_id = 42
Redis key = online:user:1001
TTL = 60s
```

客户端每隔一段时间发送：

```text
HeartbeatRequest seq_id=7
```

服务端收到合法完整包后，`Session::handleRead()` 已经刷新连接活跃时间。随后 `MessageRouter` 把请求交给 `HeartbeatService`，业务线程尝试刷新 `online:user:1001` 的 TTL，然后返回：

```text
HeartbeatResponse seq_id=7
```

### 2. 上下层调用连接

```text
客户端
    -> HeartbeatRequest
    -> TcpServer / Session
    -> Session 读路径刷新 last_active_time
    -> MessageRouter
    -> business ThreadPool 执行 HeartbeatService::handleHeartbeat()
    -> OnlineService::getUserBySession()
    -> OnlineService::refreshUserOnline()
    -> MessageRouter 发回 HeartbeatResponse
```

网络层只判断连接是否有合法入站包；Redis 在线状态刷新属于业务层。

### 3. 整体运行链路

1. 客户端发来完整合法的 `HeartbeatRequest`。
2. `Session` 解码出完整 `Packet`，刷新 `last_active_time`。
3. `TcpServer` 把 packet 交给 `MessageRouter`。
4. `MessageRouter` 解析 TLV 并把 handler 投递到 business pool。
5. `HeartbeatService` 先构造 `HeartbeatResponse`。
6. `HeartbeatService` 用 `OnlineService::getUserBySession(session_id)` 判断是否已登录。
7. 未登录时直接返回心跳响应。
8. 已登录时调用 `OnlineService::refreshUserOnline(user_id, session_id)`。
9. Redis 刷新失败时写 warning，不返回 `ErrorResponse`。
10. `MessageRouter` 把 `HeartbeatResponse` 发回当前 session。

### 4. 自身内部运行流程

`handleHeartbeat()` 的核心顺序是：

```text
validate session
    -> response.msg_type = HeartbeatResponse
    -> response.seq_id = request.seq_id
    -> online_service_.getUserBySession(session_id)
    -> 未登录: return ok
    -> 已登录: online_service_.refreshUserOnline(user_id, session_id)
    -> refresh 失败: warning + return ok
```

### 5. 该项目代码在实际应用中的具体数据例子

未登录连接：

```text
session_id = 41
HeartbeatRequest seq_id = 7

OnlineService::getUserBySession(41) -> NotFound
HeartbeatResponse seq_id = 7
Redis 不写入
```

已登录连接：

```text
session_id = 42
user_id = 1001
HeartbeatRequest seq_id = 8

OnlineService::getUserBySession(42) -> 1001
OnlineService::refreshUserOnline(1001, 42)
Redis EXPIRE online:user:1001 60
HeartbeatResponse seq_id = 8
```

Redis 刷新失败：

```text
session_id = 42
user_id = 1001
HeartbeatRequest seq_id = 9

refreshUserOnline(1001, 42) -> InternalError("redis refresh failed")
warning: heartbeat redis ttl refresh failed...
HeartbeatResponse seq_id = 9
```

客户端不能把 `HeartbeatResponse` 理解成 Redis TTL 一定刷新成功；它只说明服务端成功处理了合法心跳包。

## 6. 关键实现点

### 1. 心跳响应和 Redis TTL 解耦

Step 40 选用降级语义：

```text
合法 HeartbeatRequest
    -> 返回 HeartbeatResponse
    -> 如果已登录，尽力刷新 Redis TTL
    -> Redis 失败只影响在线状态展示，不影响心跳协议
```

原因是 Redis 短暂抖动不应该让客户端误判 TCP 连接不健康，也不应该触发无意义的重连、重登和 UI 抖动。

### 2. 不直接更新 `Session::last_active_time`

`Session::last_active_time` 的语义是：

```text
收到完整、合法、成功解码的入站 Packet
```

因此 HeartbeatService 不再额外更新时间戳。否则业务层会和网络层重复维护同一份连接活跃语义。

### 3. 用 business thread 跑 Redis

`OnlineService::refreshUserOnline()` 最终会访问 `ICache::refreshUserOnline()`，实际实现是 Redis 操作。它必须在业务线程池执行，不能放到 I/O 线程。

### 4. 覆盖默认 inline handler

`MessageRouter` 构造时仍保留默认 inline 心跳 handler，方便没有注册业务服务的早期测试。Step 40 的 server runtime 会调用：

```cpp
heartbeat_service.registerHandlers(router);
```

这会把 `HeartbeatRequest` handler 覆盖成 business-thread 版本。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| 新服务头文件和接口不稳定 | `HeartbeatServiceTest.HeaderIsSelfContained` 做自包含和函数签名检查 |
| 未登录心跳错误访问 Redis | `UnauthenticatedHeartbeatReturnsSuccess` 验证返回成功且 refresh 调用为 0 |
| 已登录心跳没有刷新 Redis TTL | `AuthenticatedHeartbeatRefreshesRedisTtl` 验证 user id、TTL 和 refresh 调用 |
| response 丢失请求序号 | `HeartbeatResponseKeepsSeqId` 验证响应保留 `seq_id` |
| Redis 抖动污染心跳协议 | `RedisRefreshFailureStillReturnsHeartbeatResponse` 验证 refresh 失败仍返回心跳响应且不清理绑定 |
| server runtime 仍走默认 inline 心跳 | `RegisteredHandlerRefreshesTtlAndSendsResponseThroughRouter` 验证注册后 router 会刷新 TTL 并回包 |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R HeartbeatService --output-on-failure
cmake --build build -j2
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build --output-on-failure
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
timeout 2s ./build/server/liteim_server || test $? -eq 124
```

## 9. 面试表达

一句话：

> 我把心跳分成连接保活和在线状态刷新两个语义：合法心跳包证明连接活跃，已登录用户额外刷新 Redis 在线 TTL。

展开说：

> `Session` 读路径在成功解出完整合法 packet 时刷新连接活跃时间，`HeartbeatService` 不重复改这个时间戳。业务层心跳 handler 在 business 线程里执行，未登录连接直接回 `HeartbeatResponse`，已登录连接调用 `OnlineService::refreshUserOnline()` 刷新 Redis TTL。如果 Redis 短暂失败，服务端记录 warning，但仍返回心跳响应，因为心跳响应代表协议层处理成功，不代表 Redis 在线状态一定刷新成功。

容易被追问：

> 为什么 Redis 失败不返回错误？因为 Redis TTL 属于在线展示状态，和 TCP 协议层保活不是同一个语义。短暂 Redis 抖动如果变成客户端心跳错误，会导致重连、重登和 UI 抖动，反而放大故障影响。

## 10. 面试常见追问

**Q1：HeartbeatService 为什么不能直接更新 `Session::last_active_time`？**

因为 `last_active_time` 已经在 `Session` 读路径按“完整合法入站 packet”刷新。业务层再改会让连接活跃语义分散到两个地方。

**Q2：为什么 Redis 刷新失败还返回 HeartbeatResponse？**

`HeartbeatResponse` 只表示服务端收到了合法心跳包并完成协议处理。Redis TTL 是在线状态展示的最终一致性刷新，失败时记录 warning，依赖后续心跳恢复。

**Q3：为什么 HeartbeatService 要跑在业务线程池？**

因为已登录心跳可能调用 Redis。Redis 是阻塞 I/O，不能在 Reactor I/O 线程执行。

**Q4：未登录连接发心跳有什么意义？**

它证明 TCP 连接和协议解码仍然正常，但未登录连接没有用户在线状态，所以不写 Redis。

**Q5：持续 Redis 刷新失败怎么办？**

当前第一版通过 warning 日志暴露问题；持续失败会导致在线状态 TTL 过期，好友列表可能显示离线。后续接入 metrics/告警时，应统计心跳 TTL 刷新失败率。
