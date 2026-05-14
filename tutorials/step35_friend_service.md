# Step 35：FriendService 好友业务

Step 35 的目标是在注册登录之后补齐第一块 IM 关系业务：`AddFriendRequest` 和 `ListFriendsRequest` 经 `MessageRouter` 投递到业务线程池，由 `FriendService` 校验登录态、访问 MySQL 好友关系，并把 Redis 在线状态写进响应。

本 Step 不实现好友申请审批、黑名单、备注名、删除好友、私聊、群聊、离线消息、历史消息、HeartbeatService 或 BotGateway。

## 1. 概念

好友业务需要同时用到三类前置能力：

```text
Step 31 MySqlStorage / RedisCache
Step 32 SessionManager / OnlineService
Step 33 MessageRouter
```

`FriendService` 把它们组合起来：

```text
AddFriendRequest
    -> 当前 Session 必须已登录
    -> 读取 TargetUserId
    -> 查询目标用户是否存在
    -> 检查是否已经是好友
    -> IStorage::addFriendship()
    -> 查询目标用户 Redis 在线状态
    -> AddFriendResponse

ListFriendsRequest
    -> 当前 Session 必须已登录
    -> IStorage::getFriends()
    -> 对每个好友查询 Redis 在线状态
    -> ListFriendsResponse
```

关键边界：

- MySQL / Redis 调用只在 business `ThreadPool` handler 中执行。
- `FriendService` 不直接读写 fd、`Channel` 或 `Buffer`。
- 当前登录用户来自 `OnlineService::getUserBySession(session_id)`，不是客户端自己传 `UserId`。
- 好友在线状态使用 `TlvType::OnlineStatus`，`uint64` 值 `1` 表示在线，`0` 表示离线。
- 第一版好友关系是直接双向添加，不做申请审批。

## 2. 本 Step 新增 / 修改文件

新增：

```text
include/liteim/service/FriendService.hpp
src/service/FriendService.cpp
tests/service/friend_service_test.cpp
tutorials/step35_friend_service.md
```

修改：

```text
include/liteim/protocol/Tlv.hpp
src/protocol/Tlv.cpp
src/service/CMakeLists.txt
server/main.cpp
tests/CMakeLists.txt
tests/protocol/tlv_type_test.cpp
README.md
tutorials/step03_protocol_types.md
task_plan.md
findings.md
progress.md
/home/yolo/jianli/PROJECT_MEMORY.md
```

本 Step 不修改 MySQL schema，也不新增 `MessageType`。好友消息类型 `AddFriendRequest`、`AddFriendResponse`、`ListFriendsRequest`、`ListFriendsResponse` 已在 Step 3 定义。

## 3. hpp 接口说明

### FriendService

```cpp
class FriendService {
public:
    FriendService(IStorage& storage, ICache& cache, OnlineService& online_service);

    Status registerHandlers(MessageRouter& router);
    Status handleAddFriend(const MessageRouter::RouterRequest& request, Packet& response);
    Status handleListFriends(const MessageRouter::RouterRequest& request, Packet& response);

private:
    Status currentUserId(const MessageRouter::RouterRequest& request, std::uint64_t& user_id);
    Status appendFriendFields(const UserProfileRecord& friend_profile, Packet& response);
};
```

`FriendService` 不拥有 `IStorage`、`ICache` 或 `OnlineService`，只保存引用。`server/main.cpp` 负责保证这些对象生命周期长于 Router handler 和业务线程任务。

### `FriendService(IStorage&, ICache&, OnlineService&)`

构造函数注入三个业务依赖：

- `IStorage`：创建好友关系、查询好友列表、查询目标用户资料。
- `ICache`：查询好友是否在线。
- `OnlineService`：根据当前 `Session` 找到已登录 `user_id`。

真实运行时传入 `MySqlStorage`、`RedisCache` 和 `OnlineService`。单元测试传入 fake storage/cache。

### `registerHandlers()`

```cpp
Status registerHandlers(MessageRouter& router);
```

注册两个 handler：

```text
AddFriendRequest   -> FriendService::handleAddFriend(), BusinessThread
ListFriendsRequest -> FriendService::handleListFriends(), BusinessThread
```

这样 `getFriends()`、`addFriendship()` 和 `isUserOnline()` 都不会在 Reactor I/O 线程执行。

### `handleAddFriend()`

输入字段：

```text
TargetUserId
```

执行步骤：

1. 调用 `currentUserId()` 从当前 session 找登录用户。
2. 读取 `TargetUserId`，要求非 0 且不能等于当前用户。
3. 调用 `IStorage::findUserById()` 确认目标用户存在并拿公开资料。
4. 调用 `IStorage::getFriends()` 检查是否已经是好友。
5. 重复好友返回 `ErrorCode::AlreadyExists`。
6. 调用 `IStorage::addFriendship()` 写 MySQL 双向好友关系。
7. 调用 `ICache::isUserOnline()` 查询目标用户在线状态。
8. 返回 `AddFriendResponse`。

响应 body 写入：

```text
FriendId
Username
Nickname
OnlineStatus
```

### `handleListFriends()`

输入字段：

```text
无
```

执行步骤：

1. 调用 `currentUserId()` 从当前 session 找登录用户。
2. 调用 `IStorage::getFriends(user_id)` 查询好友公开资料。
3. 对每个好友调用 `ICache::isUserOnline(friend_id)`。
4. 返回 `ListFriendsResponse`。

响应 body 对每个好友重复写入：

```text
FriendId
Username
Nickname
OnlineStatus
```

这些字段按重复顺序一一对应。比如第 2 个 `FriendId` 对应第 2 个 `Username`、第 2 个 `Nickname` 和第 2 个 `OnlineStatus`。

### `currentUserId()`

```cpp
Status currentUserId(const MessageRouter::RouterRequest& request, std::uint64_t& user_id);
```

这是登录态边界 helper。

它不信任客户端传来的任何用户 ID，而是使用：

```text
request.session->id()
    -> OnlineService::getUserBySession(session_id)
```

如果 session 为空、已关闭或没有绑定用户，返回 `InvalidArgument`，错误消息是 `session is not logged in`。

### `appendFriendFields()`

```cpp
Status appendFriendFields(const UserProfileRecord& friend_profile, Packet& response);
```

把一个好友的响应字段追加到 `Packet::body`：

1. `FriendId`
2. `Username`
3. `Nickname`
4. `OnlineStatus`

`OnlineStatus` 来自 `ICache::isUserOnline()`。如果 Redis/cache 返回错误，当前请求直接失败，避免把损坏缓存或 Redis 故障静默包装成“离线”。

## 4. 作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

Alice 登录成功后，当前 TCP session 已绑定：

```text
session_id=42 -> user_id=1001
```

Alice 添加 Bob：

```text
AddFriendRequest
    TargetUserId=1002
```

服务端返回：

```text
AddFriendResponse
    FriendId=1002
    Username=bob
    Nickname=Bob
    OnlineStatus=1
```

Alice 拉取好友列表：

```text
ListFriendsRequest
```

如果 Bob 在线、Charlie 离线，响应可以是：

```text
ListFriendsResponse
    FriendId=1002, Username=bob, Nickname=Bob, OnlineStatus=1
    FriendId=1003, Username=charlie, Nickname=Charlie, OnlineStatus=0
```

### 2. 上下层调用连接

完整 runtime 链路：

```text
Session::handleRead()
    -> FrameDecoder::feed()
    -> TcpServer message callback
    -> MessageRouter::route()
    -> business ThreadPool
    -> FriendService::handleAddFriend() / handleListFriends()
    -> MySqlStorage / RedisCache / OnlineService
    -> Session::sendPacket()
    -> EventLoop::queueInLoop()
```

`MessageRouter` 负责 TLV parse、错误响应和 seq_id 保留；`FriendService` 只负责好友业务。

### 3. 整体运行链路

server main 现在会创建：

```text
MySqlPool
RedisPool
MySqlStorage
RedisCache
SessionManager
OnlineService
ThreadPool
MessageRouter
AuthService
FriendService
TcpServer
```

注册顺序：

```text
AuthService::registerHandlers()
FriendService::registerHandlers()
ThreadPool::start()
TcpServer::start()
```

`SignalWatcher::start()` 仍然早于业务线程池启动，避免 SIGINT/SIGTERM 被 worker 按默认动作处理。

### 4. FriendService 自身内部运行流程

添加好友内部流程：

```text
currentUserId()
    -> get TargetUserId
    -> findUserById(target)
    -> getFriends(current_user)
    -> duplicate check
    -> addFriendship(current_user, target)
    -> isUserOnline(target)
    -> append AddFriendResponse fields
```

好友列表内部流程：

```text
currentUserId()
    -> getFriends(current_user)
    -> for each friend:
           isUserOnline(friend_id)
           append FriendId / Username / Nickname / OnlineStatus
```

重复好友为什么在 service 层先查？

`FriendDao::addFriendship()` 是幂等 DAO，重复插入不会产生脏数据。但 Step 35 的业务语义需要“重复添加返回已存在”，所以 service 在调用 `addFriendship()` 前先通过 `getFriends()` 做业务判断。

### 5. 该项目代码在实际应用中的具体数据例子

假设 Alice 已登录：

```text
user_id=1001
session_id=42
online:user:1001 -> session_id=42
```

Bob 已登录：

```text
user_id=1002
session_id=43
online:user:1002 -> session_id=43
```

Alice 发送：

```text
Packet header:
    msg_type=AddFriendRequest
    seq_id=7

TLV body:
    TargetUserId=1002
```

`FriendService` 会：

1. 从 `SessionManager` 查到 `session_id=42` 当前绑定 `user_id=1001`。
2. 从 MySQL 查到 Bob 的公开资料。
3. 检查 Alice 当前好友列表里没有 Bob。
4. 写入 `friendships(1001, 1002)` 和 `friendships(1002, 1001)`。
5. 查询 `online:user:1002`，得到在线。
6. 返回 `seq_id=7` 的 `AddFriendResponse`。

响应 body：

```text
FriendId=1002
Username=bob
Nickname=Bob
OnlineStatus=1
```

## 5. 测试

新增测试文件：

```text
tests/service/friend_service_test.cpp
```

覆盖：

- `FriendService` 头文件自包含和 public API。
- 未登录 session 添加好友失败。
- 添加好友成功写入好友关系，并返回好友在线状态。
- 重复添加好友返回 `AlreadyExists`。
- 好友列表返回公开资料和在线 / 离线状态。
- 真实 `MySqlStorage` + `RedisCache` 集成下添加好友并拉取在线好友列表。

同步协议测试：

```text
tests/protocol/tlv_type_test.cpp
```

覆盖：

- `TlvType::OnlineStatus` 的字符串名是 `ONLINE_STATUS`。

验证命令：

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "FriendService|TlvType" --output-on-failure
```

本 Step 的集成测试依赖本地 MySQL / Redis。如果本地依赖不可用，测试会 skip；完整验证时先启动：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
```

## 6. 面试常见追问

### Q1：为什么 FriendService 不直接依赖 FriendDao？

因为业务层应该依赖稳定抽象。`FriendService` 只需要“添加好友、查好友、查用户、查在线状态”这些能力，所以依赖 `IStorage` 和 `ICache`。真实运行时注入 `MySqlStorage` / `RedisCache`，测试时注入 fake。

### Q2：为什么当前用户不能从请求 body 里读 UserId？

客户端传来的 `UserId` 不可信。登录成功后，服务端已经维护 `session_id -> user_id` 的绑定。好友操作必须使用当前连接对应的服务端登录态，否则客户端可以伪造别人的 user_id 添加好友。

### Q3：为什么重复添加好友返回 AlreadyExists，而 DAO 又是幂等的？

DAO 幂等是为了保护数据库不产生重复行，也让底层写入可以安全重试。业务层返回 `AlreadyExists` 是用户可见语义：重复添加不是一次新的成功关系变更。

### Q4：为什么在线状态用 OnlineStatus TLV，而不是复用 SessionId？

好友列表只需要告诉客户端“在线 / 离线”。暴露 `session_id` 会把服务端内部连接细节泄漏给客户端，也会让后续多端和多进程扩展更难。`OnlineStatus=1/0` 更稳定。

### Q5：为什么 Redis 查询失败不直接当作离线？

Redis 故障、value 损坏和真正离线不是同一件事。如果把错误静默当离线，客户端会看到错误状态，服务端也会掩盖缓存问题。当前选择是让请求失败并由 `MessageRouter` 返回 `ErrorResponse`。
