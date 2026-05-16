# Step 38：GroupService 群聊

## 0. 本 Step 结论

- 目标：Step 38 把前面已经准备好的群表、群成员 DAO、群消息协议和消息落库能力接成真正的群聊业务。
- 前置依赖：依赖 Step 27 `GroupDao`、Step 31 `IStorage` / `ICache` 适配层、Step 32 `OnlineService`、Step 33 `MessageRouter`、Step 36 私聊投递模式和 Step 37 离线消息拉取能力。
- 主要交付：新增 `GroupService`、扩展 `IStorage::findGroupById()` / `getGroupsForUser()`、新增 service 测试、server runtime handler 注册和本文档。
- 线程边界：所有群聊 handler 都通过 business `ThreadPool` 执行，MySQL / Redis 阻塞调用不进入 Reactor I/O 线程。
- 范围控制：第一版只做基础群聊，不做复杂权限、公告、禁言、已读回执、广播优化、可靠 ACK、跨节点路由或 BotGateway。

## 1. 为什么需要这个 Step

Step 27 已经有 `chat_groups` 和 `group_members`，Step 26 / Step 31 已经能保存 `ConversationType::kGroup` 的消息，但这些只是存储能力。客户端发来的群聊请求还没有 service 入口：

```text
CreateGroupRequest
JoinGroupRequest
ListGroupsRequest
GroupMessageRequest
```

如果没有 `GroupService`，这些协议类型虽然已经定义，但 `MessageRouter` 找不到对应 handler，运行时只能返回 `ErrorResponse`。Step 38 的作用就是把“群相关 DAO + 消息落库 + 在线状态 + 离线消息”串成一个可运行闭环。

这个 Step 还补了一个 storage 抽象缺口：`ListGroupsRequest` 需要“按当前登录用户列出我的群”，所以新增 `IStorage::getGroupsForUser(user_id, groups)`，让 service 层继续只依赖 `IStorage`，不直接碰 `GroupDao`。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `GroupService`。
- 注册 `CreateGroupRequest`、`JoinGroupRequest`、`ListGroupsRequest` 和 `GroupMessageRequest`。
- 建群时使用当前登录 user id 作为 owner，不信任客户端传 owner。
- 加群时校验 group 存在，再写入 `group_members`。
- 列群时通过 `IStorage::getGroupsForUser()` 返回当前用户所在群。
- 发群消息前校验当前用户是群成员。
- 群消息先保存到 MySQL `messages`，并为离线群成员写 `offline_messages`。
- 在线群成员收到 `GroupMessagePush`。
- 离线群成员 Redis unread +1；如果 unread 递增失败，只记录 warning，不把已保存消息变成发送失败。

### 本 Step 不做

- 不做群主/管理员复杂权限。
- 不做群公告。
- 不做群禁言。
- 不做群消息已读回执。
- 不做大群广播优化或消息队列。
- 不做可靠 ACK / 重试 / client message id 去重。
- 不做跨节点路由或 BotGateway。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/service/GroupService.hpp` | 新增 | 声明群聊 service、四个 handler 和内部 helper 边界 |
| `src/service/GroupService.cpp` | 新增 | 实现建群、加群、列群、群消息保存、在线 push 和离线 unread |
| `src/service/CMakeLists.txt` | 修改 | 把 `GroupService.cpp` 编入 `liteim_service` |
| `server/main.cpp` | 修改 | 创建 `GroupService` 并注册群聊 handler |
| `include/liteim/storage/IStorage.hpp` | 修改 | 暴露 `findGroupById()` 和 `getGroupsForUser()` 给 service 层 |
| `include/liteim/storage/GroupDao.hpp` | 修改 | 声明 `getGroupsForUser()` |
| `src/storage/GroupDao.cpp` | 修改 | 从 `group_members` join `chat_groups` 查询当前用户所在群 |
| `include/liteim/storage/MySqlStorage.hpp` | 修改 | 暴露新增 storage facade 方法 |
| `src/storage/MySqlStorage.cpp` | 修改 | 转发新增 group 查询到 `GroupDao` |
| `tests/service/group_service_test.cpp` | 新增 | 覆盖 Step 38 service 行为和 router 注册 |
| `tests/storage/storage_interface_test.cpp` | 修改 | 固定新增 `IStorage` 接口契约 |
| `tests/storage/friend_group_dao_test.cpp` | 修改 | 覆盖 `GroupDao::getGroupsForUser()` |
| `tests/CMakeLists.txt` | 修改 | 接入 Step 38 service 测试 |
| `README.md` | 更新 | 记录 Step 38 runtime 和验证命令 |
| `docs/tutorials/step38_group_service.md` | 新增 | 讲解群聊业务闭环 |
| `docs/process/task_plan.md / docs/process/findings.md / docs/process/progress.md` | 更新 | 记录 Step 38 过程、边界和验证结果 |

## 4. 核心接口与契约

### `IStorage`

```cpp
virtual Status findGroupById(std::uint64_t group_id, GroupRecord& group) = 0;
virtual Status getGroupsForUser(std::uint64_t user_id, std::vector<GroupRecord>& groups) = 0;
```

契约：

- `findGroupById()` 用于区分 group 不存在和其他错误。
- `getGroupsForUser()` 返回当前用户所在的群，按 `group_id` 升序排列。
- service 层不直接依赖 `GroupDao`，只依赖 `IStorage`。

### `GroupService`

```cpp
class GroupService {
public:
    GroupService(IStorage& storage, ICache& cache, OnlineService& online_service);

    Status registerHandlers(MessageRouter& router);
    Status handleCreateGroup(const MessageRouter::RouterRequest& request, Packet& response);
    Status handleJoinGroup(const MessageRouter::RouterRequest& request, Packet& response);
    Status handleListGroups(const MessageRouter::RouterRequest& request, Packet& response);
    Status handleGroupMessage(const MessageRouter::RouterRequest& request, Packet& response);
};
```

它只依赖三个对象：

- `IStorage`：群资料、群成员、消息和离线消息。
- `ICache`：离线群成员 unread 递增。
- `OnlineService`：根据 session 查当前 user id，并查群成员是否有本进程在线 session。

请求和响应字段：

- `CreateGroupRequest`：读取 `GroupName`，返回 `CreateGroupResponse` 的 `GroupId` / `GroupName`。
- `JoinGroupRequest`：读取 `GroupId`，返回 `JoinGroupResponse` 的 `GroupId` / `GroupName`。
- `ListGroupsRequest`：body 为空，返回重复 `GroupId` / `GroupName`。
- `GroupMessageRequest`：读取 `GroupId` / `MessageText`，返回 `GroupMessageResponse`，并向在线成员发送 `GroupMessagePush`。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

Alice 创建一个群 `dev_group`，Bob 加入这个群。之后 Alice 发一条群消息：

```text
GroupId = 8801
Alice user_id = 1001
Bob   user_id = 1002
Charlie user_id = 1003
MessageText = "今晚同步一下进度"
```

如果 Bob 在线，Bob 会收到 `GroupMessagePush`。如果 Charlie 也是群成员但离线，MySQL 会为 Charlie 写 `offline_messages`，Redis 会给 Charlie 的群会话 unread +1。

### 2. 上下层调用连接

```text
客户端
    -> GroupMessageRequest
    -> TcpServer / Session
    -> MessageRouter
    -> business ThreadPool 执行 GroupService::handleGroupMessage()
    -> OnlineService::getUserBySession()
    -> IStorage::findGroupById()
    -> IStorage::getGroupMembers()
    -> IStorage::saveMessageWithOfflineRecipients()
    -> Session::sendPacket(GroupMessagePush) 给在线成员
    -> ICache::incrUnread() 给离线成员
    -> MessageRouter 发回 GroupMessageResponse
```

网络层只负责收包和发包，阻塞的 MySQL / Redis 操作都在 business 线程执行。

### 3. 整体运行链路

1. 客户端登录成功，`OnlineService` 建立 `session_id -> user_id` 绑定。
2. 客户端发送 `CreateGroupRequest`，server 用当前 user id 建群并把 owner 自动加入 `group_members`。
3. 其他客户端发送 `JoinGroupRequest`，server 校验群存在后写入 `group_members`。
4. 客户端发送 `ListGroupsRequest`，server 通过 `getGroupsForUser()` 返回当前用户所在群。
5. 群成员发送 `GroupMessageRequest`。
6. handler 校验当前 session 已登录，group 存在，发送者是群成员。
7. handler 遍历群成员，跳过发送者，把在线成员 session 和离线成员 user id 分开。
8. handler 先调用 `saveMessageWithOfflineRecipients()` 保存群消息和离线记录。
9. handler 给在线成员发送 `GroupMessagePush`。
10. handler 给离线成员 unread +1，最后返回 `GroupMessageResponse`。

### 4. 自身内部运行流程

`handleGroupMessage()` 的核心顺序是：

```text
currentUserId()
    -> get GroupId / MessageText
    -> storage_.findGroupById()
    -> storage_.getGroupMembers()
    -> sender must be in members
    -> split online_sessions / offline_user_ids
    -> storage_.saveMessageWithOfflineRecipients()
    -> cache_.incrUnread() for offline users
    -> send GroupMessagePush to online sessions
    -> append GroupMessageResponse
```

注意这里先落库，再 push。这样即使 push 前后 server 异常退出，消息事实仍在 MySQL，后续可以通过离线消息或历史消息补偿。

### 5. 该项目代码在实际应用中的具体数据例子

假设：

```text
group_id = 8801
group_name = "dev_group"
Alice user_id = 1001
Bob user_id = 1002
Charlie user_id = 1003
message_id = 9001
created_at_ms = 1800000000000
```

群成员表：

```text
group_members:
  (group_id=8801, user_id=1001)
  (group_id=8801, user_id=1002)
  (group_id=8801, user_id=1003)
```

Alice 发群消息时，MySQL `messages` 写入：

```text
conversation_type = 2
conversation_id   = 8801
sender_id         = 1001
receiver_id       = 8801
message_text      = "今晚同步一下进度"
```

如果 Bob 在线、Charlie 离线：

```text
Bob 收到 GroupMessagePush
Charlie offline_messages 增加 message_id=9001
Redis unread:user:1003:conversation:2:8801 +1
Alice 收到 GroupMessageResponse
```

## 6. 关键实现点

### 登录身份只信任 Session

`GroupService` 不读取客户端传入的 `UserId` 作为发送者或 owner。当前用户统一来自：

```cpp
online_service_.getUserBySession(request.session->id(), user_id);
```

这和 `AuthService` / `FriendService` / `ChatService` 的边界一致。

### `ListGroupsRequest` 需要 storage 抽象扩展

不能让 `GroupService` 直接依赖 `GroupDao`，否则 service 层会绕过 `IStorage`。所以本 Step 扩展：

```cpp
IStorage::getGroupsForUser(user_id, groups)
```

`MySqlStorage` 再转发到 `GroupDao`，保持 service 层和具体 MySQL DAO 解耦。

### 群消息使用 group id 作为 conversation id

群消息的会话 key 是：

```text
ConversationType = kGroup
ConversationId   = group_id
```

`receiver_id` 也写成 `group_id`，和前面 `MessageDao::saveGroupMessage()` 的规则一致。

### 在线与离线成员分开处理

`GroupService` 遍历群成员：

- 当前发送者跳过，不给自己再 push 一份。
- `OnlineService::getSessionByUser()` 查到 session 的成员加入在线投递列表。
- 查不到 session 的成员加入离线 user id 列表。

第一版只支持本进程在线 session，不做跨节点路由。

### unread 失败不覆盖消息保存成功

群消息和 `offline_messages` 已经写入 MySQL 后，如果 Redis unread +1 失败，server 只记录 warning。原因和私聊一致：MySQL 是消息事实来源，不能让发送方因为 Redis 计数失败而重试制造重复消息。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `IStorage` 接口扩展破坏 fake/mock | `StorageInterfaceTest.CanBeImplementedByFakeStorage` 覆盖新增接口 |
| `getGroupsForUser()` SQL 查询错误 | `FriendGroupDaoIntegrationTest.GetGroupsForUserReturnsOwnedAndJoinedGroups` 覆盖 owner 群和 joined 群 |
| 建群错误使用客户端 owner | `CreateGroupUsesLoggedInUserAndReturnsGroupFields` 校验 owner 来自登录 session |
| 加群没有校验 group 存在 | `JoinGroupAddsCurrentUserAndReturnsGroupFields` 校验先 `findGroupById()` 后 add member |
| 列群返回非当前用户群 | `ListGroupsReturnsCurrentUserGroups` 校验只返回当前用户所在群 |
| 非成员可以发群消息 | `GroupMessageRejectsNonMember` 校验失败且不保存消息 |
| 在线成员收不到 push | `GroupMessagePushesOnlineMemberAndStoresOfflineRecipient` 读取真实 socket packet |
| 离线成员没有 offline/unread | 同一测试校验 `offline_user_ids` 和 `incrUnread()` |
| Redis unread 失败诱导重复发送 | `OfflineUnreadFailureStillReturnsSenderSuccess` 校验仍返回成功 |
| runtime 没有注册 group handler | `RegisteredHandlerSendsCreateGroupResponseThroughRouter` 通过 `MessageRouter` 路径发请求 |

## 8. 验证命令

单独验证 Step 38：

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "GroupService|FriendGroupDao|StorageInterface" --output-on-failure
```

完整验证：

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

server smoke：

```bash
timeout 1s ./build/server/liteim_server || test $? -eq 124
```

## 9. 面试表达

一句话：

> 我在 LiteIM 里实现了基础群聊 service，把群 DAO、消息落库、在线 session push、离线消息和 Redis unread 串成了一个 business-thread handler 闭环。

展开说：

> 群聊请求先经过 `MessageRouter`，然后在业务线程里进入 `GroupService`。建群和加群通过 `IStorage` 操作 MySQL；列群通过新增的 `getGroupsForUser()` 保持 service 层不直接依赖 DAO；发群消息时先校验发送者是群成员，再把消息保存到 MySQL，在线成员通过当前进程 session 收到 push，离线成员写入 `offline_messages` 并递增 unread。整个过程中 Reactor I/O 线程不做 MySQL / Redis 阻塞调用。

容易被追问：

> 第一版为什么直接遍历群成员？因为当前目标是单机基础闭环和清晰边界，不做大群广播优化。后续如果进入大群场景，可以把成员查询、批量投递、异步队列和跨节点路由拆成独立优化点。

## 10. 面试常见追问

### 1. 为什么 `ListGroupsRequest` 要新增 `IStorage::getGroupsForUser()`？

因为 service 层应该依赖存储抽象，而不是直接依赖 `GroupDao`。如果 `GroupService` 直接拿 DAO，后续换 mock、换 storage adapter 或做集成测试都会变难。

### 2. 群消息为什么 `receiver_id` 写 group id？

私聊里 `receiver_id` 是用户 id；群聊里没有单个接收者，所以用 group id 表示接收目标。真正的会话身份还是 `ConversationType::kGroup + ConversationId=group_id`。

### 3. 为什么不给发送者也 push 一份？

发送者会收到 `GroupMessageResponse`，里面已经包含最终 `message_id`、时间戳和消息内容。再 push 给发送者会让客户端去重更复杂，第一版先跳过发送者。

### 4. 非群成员发消息为什么失败？

群消息必须以 `group_members` 为权限边界。否则只要知道 group id 就能向群里写消息，会破坏最基本的群聊隔离。

### 5. Redis unread 失败为什么不返回发送失败？

因为 MySQL `messages` 和 `offline_messages` 已经成功保存。此时返回失败会诱导客户端重试，制造重复消息。Unread 是可修复的缓存状态，消息事实来源是 MySQL。
