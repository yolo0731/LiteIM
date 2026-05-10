# Step 21：定义 IStorage / ICache 抽象

本 Step 只做存储和缓存的接口层，不连接真实 MySQL，也不连接真实 Redis。

前面 Step 已经完成网络底座：

```text
TcpServer -> Session -> Packet/TLV -> business ThreadPool
```

后续登录、好友、私聊、群聊、离线消息和历史消息都需要访问数据。但业务代码不应该直接依赖 MySQL C API 或 Redis 命令字符串。Step 21 先定义抽象边界：

```text
business service
  -> IStorage / ICache
  -> later MySQL DAO / Redis client
```

这样 Step 31 之后写业务服务时，只需要面向接口编程；Step 22-30 再逐步补真实 MySQL / Redis 实现。

## 1. 本 Step 新增文件

```text
include/liteim/storage/StorageTypes.hpp
include/liteim/storage/IStorage.hpp
include/liteim/cache/CacheTypes.hpp
include/liteim/cache/ICache.hpp
src/storage/CMakeLists.txt
src/cache/CMakeLists.txt
tests/storage/storage_interface_test.cpp
tests/cache/cache_interface_test.cpp
tutorials/step21_storage_cache_interfaces.md
```

同时更新：

```text
src/CMakeLists.txt
tests/CMakeLists.txt
README.md
task_plan.md
findings.md
progress.md
```

## 2. StorageTypes.hpp 接口说明

`StorageTypes.hpp` 定义业务存储层需要传递的数据结构。

核心类型：

- `ConversationType`：区分私聊和群聊。
- `ConversationKey`：表示一个会话，包含类型和 id。
- `CreateUserRequest`：创建用户时传入 username、hash、salt、nickname。
- `UserRecord`：MySQL 中的用户记录。
- `CreateGroupRequest` / `GroupRecord`：建群请求和群记录。
- `GroupMemberRecord`：群成员展示记录。
- `MessageRecord`：消息记录，包含会话、发送者、接收者、文本和时间。
- `OfflineMessageRecord`：离线消息记录。
- `HistoryQuery`：历史消息查询条件。

这些结构体只表达业务数据，不包含 SQL、连接池、事务或 Redis key。

## 3. IStorage.hpp 接口说明

`IStorage` 是未来 MySQL 持久化层的抽象接口。

它覆盖后续 IM 业务需要的几组能力：

- 用户：`createUser()`、`findUserByUsername()`、`findUserById()`。
- 好友：`addFriendship()`、`getFriends()`。
- 群组：`createGroup()`、`addGroupMember()`、`removeGroupMember()`、`getGroupMembers()`。
- 消息：`saveMessage()`、`saveOfflineMessage()`、`getOfflineMessages()`、`markOfflineDelivered()`、`getHistory()`。

接口统一返回 `Status`，查询结果通过输出参数返回。

这里没有引入 `Result<T>` 模板，是为了保持当前项目风格简单：已有基础设施只有 `Status`，后续 DAO 实现也可以沿用同样的失败语义。

`IStorage` 是纯虚类，业务层只依赖这个接口。后续真实实现可以是 `MySqlStorage`，测试里可以是 fake/mock。

## 4. CacheTypes.hpp / ICache.hpp 接口说明

`ICache` 是未来 Redis 状态层的抽象接口。

它覆盖三类缓存状态：

- 在线状态：`setUserOnline()`、`refreshUserOnline()`、`setUserOffline()`、`isUserOnline()`、`getOnlineSession()`。
- 未读计数：`incrUnread()`、`getUnread()`、`clearUnread()`。
- 登录失败限制：`allowLoginAttempt()`、`recordLoginFailure()`、`clearLoginFailure()`。

`OnlineSession` 记录用户在哪个 `session_id`、哪个 server 上在线。

`UnreadKey` 使用 `ConversationKey`，表示某个用户在某个会话上的未读计数。

`LoginAttemptKey` 使用 username 和 remote ip 组成登录失败限制 key。

本 Step 不规定 Redis key 字符串格式，也不实现 token bucket。Redis key 设计和真实命令封装留给后续 Redis Step。

## 5. 作用场景和运行流程

后续业务服务的大致调用链是：

```text
Session 收到 Packet
  -> TcpServer message callback
  -> submit 到 business ThreadPool
  -> AuthService / ChatService
  -> IStorage / ICache
  -> 生成响应 Packet
  -> 投递回 Session owner EventLoop
```

重要边界：

- I/O 线程不能直接访问 MySQL 或 Redis。
- `IStorage` / `ICache` 的真实实现应该在业务线程池里调用。
- 业务线程不能直接改 `Session` 内部状态。
- MySQL 是消息和用户实体的最终持久化来源。
- Redis 只保存在线状态、未读计数和登录失败限制等状态。

## 6. 测试

新增测试证明两件事：

- `IStorage` 可以被本地 `FakeStorage` 实现，业务层可以只依赖接口。
- `ICache` 可以被本地 `NullCache` 实现，测试可以不依赖真实 Redis。

运行：

```bash
cmake --build build
ctest --test-dir build -R "(StorageInterfaceTest|CacheInterfaceTest)" --output-on-failure
ctest --test-dir build --output-on-failure
```

## 7. 提交

本 Step 提交信息：

```text
feat(storage): define storage and cache interfaces
```
