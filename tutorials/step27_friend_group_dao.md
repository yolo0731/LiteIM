# Step 27：FriendDao 和 GroupDao

## 1. 概念

Step 27 把 Step 22 建好的 `friendships`、`chat_groups`、`group_members` 三张表封装成 DAO。它仍然是阻塞 MySQL 数据访问层，后续只能放到 business `ThreadPool` 中调用，不能直接放进 Reactor I/O 线程。

本 Step 只做数据访问：

- `FriendDao` 添加双向好友关系、查询某个用户的好友列表。
- `GroupDao` 创建群、添加群成员、移除普通成员、查询群成员、按 group id 查询群资料。
- DAO 不持有 `Session`，不操作 `EventLoop`，不做登录态、在线状态、未读数或消息路由。

第一版好友关系没有申请审批，调用 `addFriendship()` 就直接建立双向关系。第一版群权限只区分 owner 和 normal member：owner 存在 `chat_groups.owner_id`，普通成员和 owner 都在 `group_members` 里。

## 2. hpp 接口说明

### `FriendDao`

`FriendDao` 依赖 `MySqlPool`：

```cpp
explicit FriendDao(MySqlPool& pool,
                   std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));
```

public 方法：

- `addFriendship(user_id, friend_id)`：在一个事务里插入两个方向的好友关系。
- `getFriends(user_id, friends)`：查询某个用户的好友列表，并返回完整 `UserRecord`。

`addFriendship()` 会拒绝 `0` id 和自己加自己。重复添加同一对好友是幂等的：已有行不会更新 `created_at_ms`，也不会插入重复数据。

### `GroupDao`

`GroupDao` 同样依赖 `MySqlPool`：

```cpp
explicit GroupDao(MySqlPool& pool,
                  std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));
```

public 方法：

- `createGroup(request, created_group)`：创建群，并把 owner 同步写入 `group_members`。
- `addGroupMember(group_id, user_id)`：添加成员，重复添加保持幂等。
- `removeGroupMember(group_id, user_id)`：移除普通成员。
- `getGroupMembers(group_id, members)`：查询群成员，join `users` 返回 username 和 nickname。
- `findGroupById(group_id, group)`：按 id 查询群资料。

`createGroup()` 用事务包住 `chat_groups` 插入、`LAST_INSERT_ID()` 查回和 owner 成员插入。这样失败时会整体 `ROLLBACK`，不会留下只有群资料、没有 owner 成员的半成品。

`removeGroupMember()` 会先查询 `chat_groups.owner_id`。如果要移除的是 owner，返回 `InvalidArgument`，避免 owner 仍在群资料里但不在成员表。

## 3. 作用场景和运行流程

后续添加好友的大致流程会是：

```text
business ThreadPool
  -> FriendDao::addFriendship(user_id, friend_id)
  -> EventLoop::queueInLoop() 返回 AddFriendResponse
```

DAO 内部添加好友：

```text
MySqlPool::acquire(timeout, guard)
  -> START TRANSACTION
  -> INSERT INTO friendships (user_id, friend_id)
  -> INSERT INTO friendships (friend_id, user_id)
  -> ON DUPLICATE KEY UPDATE no-op
  -> COMMIT
  -> 失败时 ROLLBACK
  -> ConnectionGuard 析构归还连接
```

后续建群的大致流程会是：

```text
business ThreadPool
  -> GroupDao::createGroup(request, created_group)
  -> EventLoop::queueInLoop() 返回 CreateGroupResponse
```

DAO 内部建群：

```text
MySqlPool::acquire(timeout, guard)
  -> START TRANSACTION
  -> INSERT INTO chat_groups ...
  -> SELECT ... WHERE group_id = LAST_INSERT_ID()
  -> INSERT INTO group_members (group_id, owner_id)
  -> COMMIT
  -> 失败时 ROLLBACK
```

查询群成员：

```text
MySqlPool::acquire(timeout, guard)
  -> SELECT group_members JOIN users
  -> GroupMemberRecord(user_id, username, nickname, joined_at_ms)
```

## 4. 测试

新增 `tests/storage/friend_group_dao_test.cpp`：

- `FriendGroupDaoTest.HeadersAreSelfContained`：`FriendDao` / `GroupDao` 头文件可独立使用。
- `FriendGroupDaoIntegrationTest.AddFriendshipCreatesBidirectionalRelationship`：添加好友后双方好友列表都能看到对方。
- `FriendGroupDaoIntegrationTest.RepeatedAddFriendshipDoesNotCreateDuplicates`：重复添加和反向重复添加不产生重复关系。
- `FriendGroupDaoIntegrationTest.CreateGroupPersistsGroupAndOwnerMembership`：创建群成功，能按 id 查回群资料，owner 自动进入成员表。
- `FriendGroupDaoIntegrationTest.AddGroupMemberIsIdempotent`：重复加入同一成员不会产生重复成员。
- `FriendGroupDaoIntegrationTest.RemoveGroupMemberRemovesNormalMember`：移除普通成员后成员列表只剩 owner。
- `FriendGroupDaoIntegrationTest.FindMissingGroupReturnsNotFound`：不存在的 group id 返回 `NotFound`。

测试使用 `step27_` 用户名和群名，SetUp/TearDown 只清理自己的测试用户、好友关系、群组和群成员，不修改 seed 用户 `alice`、`bob`、`mira_bot`。

## 5. 验证命令

```bash
docker compose -f docker/docker-compose.yml up -d --wait
cmake --build build
ctest --test-dir build -R "FriendGroupDaoTest|FriendGroupDaoIntegrationTest" --output-on-failure
ctest --test-dir build --output-on-failure
```

本 Step 不实现 Redis client、FriendService、GroupService、AuthService、ChatService、user-session 绑定或网络层运行时路由。它只把好友和群组关系封装成 MySQL DAO。
