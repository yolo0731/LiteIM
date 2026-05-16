# Step 40：HeartbeatService 心跳 TTL 刷新

## 0. 本 Step 结论

- 目标：Step 40 把已经存在的 `HeartbeatRequest` 变成完整的业务层心跳服务。
- 前置依赖：依赖 Step 32 `OnlineService` 的登录态绑定、Step 33 `MessageRouter` 的异步分发，以及 Step 29 `RedisCache` 的在线状态 TTL 能力。
- 主要交付：新增 `HeartbeatService`、service 单元测试、server runtime handler 注册、README 和本文档。
- 线程边界：`HeartbeatRequest` 通过 business `ThreadPool` 执行，Redis 刷新不进入 Reactor I/O 线程。
- 范围控制：只做“已登录刷新 Redis TTL，未登录直接返回成功”。不改 `Session::last_active_time`，不改 `timerfd` 心跳超时，不改客户端 UI，不改 reconnect 策略。

## 1. 为什么需要这个 Step

Step 33 已经给 `MessageRouter` 留了一个 inline 的 `HeartbeatRequest` fallback，只是返回空 `HeartbeatResponse`。那种实现能保住协议通路，但还没有把“登录态在线刷新”接进来。

Step 40 解决的问题是：

- 已登录用户发心跳时，刷新 Redis `online:user:<user_id>` 的 TTL。
- 未登录连接发心跳时，保持轻量响应，不写 Redis。
- 心跳失败要有明确语义，不把 Redis 故障悄悄吞掉。
- 心跳语义和 TCP 连接活跃时间保持分工清晰，避免把两套状态混在一起。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `HeartbeatService`。
- 注册 `MessageType::HeartbeatRequest`。
- 未登录心跳直接返回 `HeartbeatResponse`。
- 已登录心跳通过 `OnlineService::refreshUserOnline()` 刷新 Redis TTL。
- Redis 刷新失败按方案 A 返回错误，让 `MessageRouter` 转成 `ErrorResponse`。
- 继续让 `Session` 读路径负责 TCP 连接活跃时间，不在 service 层改 `Session::last_active_time`。

### 本 Step 不做

- 不改 `timerfd` 超时关闭逻辑。
- 不改 `Session` 的输入缓冲活跃时间刷新规则。
- 不改客户端重连策略。
- 不新增协议字段。
- 不改 MySQL schema。
- 不做 BotGateway。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/service/HeartbeatService.hpp` | 新增 | 声明心跳 service、注册接口和 handler |
| `src/service/HeartbeatService.cpp` | 新增 | 实现未登录直返、已登录刷新 TTL、失败回传 |
| `src/service/CMakeLists.txt` | 修改 | 把 `HeartbeatService.cpp` 编进 `liteim_service` |
| `server/main.cpp` | 修改 | 创建 `HeartbeatService` 并注册 `HeartbeatRequest` handler |
| `tests/service/heartbeat_service_test.cpp` | 新增 | 覆盖 Step 40 service 行为和 router 注册 |
| `tests/CMakeLists.txt` | 修改 | 接入 Step 40 service 测试 |
| `README.md` | 更新 | 记录 Step 40 runtime 和验证命令 |
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

契约：

- `registerHandlers()` 把 `HeartbeatRequest` 注册成 `BusinessThread` handler。
- `handleHeartbeat()` 只处理业务层心跳，不直接触碰 `Session` 内部状态。
- `request.session == nullptr` 或 `request.session->closed()` 时返回 `InvalidArgument`。
- 登录态查不到时，视为未登录连接，直接返回成功的 `HeartbeatResponse`。
- 登录态存在时，调用 `OnlineService::refreshUserOnline(user_id, session_id)` 刷新 Redis TTL。
- Redis 刷新失败时返回错误，由 `MessageRouter` 编码成 `ErrorResponse`。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

Alice 已经登录成功：

```text
user_id = 1001
session_id = 7001
seq_id = 4001
```

客户端周期性发送 `HeartbeatRequest`。服务端确认 session 仍绑定 `user_id=1001` 后，刷新 `online:user:1001` 的 TTL。

如果一个未登录连接也发了心跳，服务端只回一个 `HeartbeatResponse`，不写 Redis。

### 2. 上下层调用连接

```text
客户端
    -> HeartbeatRequest
    -> TcpServer / Session
    -> MessageRouter
    -> business ThreadPool 执行 HeartbeatService::handleHeartbeat()
    -> OnlineService::getUserBySession()
    -> 必要时调用 OnlineService::refreshUserOnline()
    -> MessageRouter 发回 HeartbeatResponse 或 ErrorResponse
```

### 3. 整体运行链路

1. 客户端发 `HeartbeatRequest`。
2. `MessageRouter` 把请求分发到 `HeartbeatService`。
3. handler 检查 session 是否存在且未关闭。
4. handler 查询当前 session 是否绑定登录用户。
5. 未登录则直接返回成功响应。
6. 已登录则刷新 Redis TTL。
7. 成功时返回 `HeartbeatResponse`。
8. 失败时返回错误，交给 router 编码为 `ErrorResponse`。

### 4. 自身内部运行流程

`handleHeartbeat()` 的核心顺序是：

```text
validateSession()
    -> lookupUserBySession()
    -> refreshOnlineTtlIfNeeded()
    -> fillResponse()
```

未登录和已登录的分支只在 Redis 刷新这一步不同。

### 5. 该项目代码在实际应用中的具体数据例子

已登录用户心跳：

```text
HeartbeatRequest:
  seq_id = 4001
  session_id = 7001
  bound user_id = 1001

HeartbeatResponse:
  seq_id = 4001
```

Redis 侧对应的在线 key 会继续保持活跃，例如：

```text
online:user:1001
TTL = 60s
```

## 6. 关键实现点

- `MessageRouter` 构造函数里原本有 inline heartbeat fallback；Step 40 通过 `HeartbeatService::registerHandlers()` 覆盖它。
- `HeartbeatService` 不自己维护连接活跃时间，避免把业务心跳和 TCP 活跃时间混成一个状态。
- 已登录心跳先查 `OnlineService::getUserBySession()`，再走 `refreshUserOnline()`，这样旧 session 不能误刷新新登录态。
- 方案 A 的错误返回让 Redis 故障可见，避免客户端误判心跳成功。
- `server/main.cpp` 里 heartbeat handler 仍然在 business pool 注册，不把 Redis 阻塞调用塞进 I/O 线程。

## 7. 测试设计

- header 自包含：确认 `HeartbeatService` 构造和接口签名稳定。
- 未登录心跳：返回 `HeartbeatResponse`，不触发 Redis refresh。
- 已登录心跳：刷新 TTL，保持 seq_id。
- 活跃时间分工：确认心跳 handler 不改 `Session::last_active_time`。
- Redis 失败：已登录场景返回错误，不伪装成成功。
- router 接入：`MessageRouter` 走 business thread 时能发送 `HeartbeatResponse`。
- router 错误路径：Redis refresh 失败时发送 `ErrorResponse`。

## 8. 验证命令

```bash
cmake --build build
ctest --test-dir build -R HeartbeatService --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
timeout 1s ./build/server/liteim_server || test $? -eq 124
```

本机集成验证前先启动 Docker MySQL / Redis：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
```

## 9. 面试表达

可以这样讲：

“这个 Step 负责把心跳从协议占位变成真正的在线续期。未登录心跳只回响应，不碰 Redis；已登录心跳会先校验 session 仍绑定当前用户，再刷新 Redis 在线 TTL。业务心跳和 TCP 连接活跃时间是分开的，连接活跃时间仍由 `Session` 的合法入站包路径维护。”

## 10. 面试常见追问

### Q1：为什么心跳失败要返回错误，而不是静默成功？

因为 Step 40 的核心价值就是 Redis 在线 TTL 续期。刷新失败还返回成功，会让客户端误以为在线状态已经续上，实际上 Redis 可能已经过期。

### Q2：为什么不直接改 `Session::last_active_time`？

那是 TCP 连接活跃时间，应该只由完整、合法的入站 Packet 刷新。HeartbeatService 管的是业务在线状态，不是底层 socket 活跃时间。

### Q3：为什么未登录心跳也要回 `HeartbeatResponse`？

这样协议路径保持稳定，客户端不用把“连接可用”和“已登录在线”两件事混在一起判断。未登录连接发心跳仍然可以得到一个轻量响应，但不会产生 Redis 状态副作用。
