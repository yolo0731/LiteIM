# Step 27：FriendDao 和 GroupDao

## 0. 本 Step 结论

- 目标：Step 27 的目标是在 MySQL 存储层补齐好友关系和群组成员 DAO。
- 前置依赖：依赖 Step 0-26 已建立的工程、协议或运行时基础。
- 主要交付：`FriendDao 和 GroupDao` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

Step 27 的目标是在 MySQL 存储层补齐好友关系和群组成员 DAO。

到 Step 26 为止，LiteIM 已经能访问用户表、保存消息和管理离线消息，但联系人和群聊还缺少关系数据访问。Step 27 解决的问题是：

```text
好友关系、建群、加群、退群和成员列表如何通过 DAO 稳定落 MySQL？
```

答案是实现 `FriendDao` 和 `GroupDao`。

### 概念

好友和群组属于 IM 的关系层数据。

好友关系第一版使用直接双向关系：

```text
addFriendship(1001, 1002)
    -> insert (1001, 1002)
    -> insert (1002, 1001)
```

群组第一版只区分 owner 和普通成员：

```text
chat_groups.owner_id
group_members(group_id, user_id)
```

不做：

- 好友申请审批。
- 拉黑、备注、分组。
- 群管理员。
- 群禁言。
- 群公告。
- 入群审批。

Step 27 只做 DAO，不接入 ChatService，也不做 Redis 缓存。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `FriendDao 和 GroupDao` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/storage/FriendDao.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/storage/GroupDao.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/FriendDao.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/GroupDao.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/storage/friend_group_dao_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/tutorials/step27_friend_group_dao.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

```cpp
class FriendDao {
public:
    explicit FriendDao(MySqlPool& pool,
                       std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status addFriendship(std::uint64_t user_id, std::uint64_t friend_id);
    Status getFriends(std::uint64_t user_id, std::vector<UserProfileRecord>& friends);
};
```

### 构造函数

保存非 owning `MySqlPool*` 和 acquire timeout。

关键成员：

- `MySqlPool* pool_`。
- `std::chrono::milliseconds acquire_timeout_`。

线程边界：

- 会阻塞 MySQL，只能在 business 线程使用。
- 不直接操作网络层对象。

### `addFriendship()`

```cpp
Status addFriendship(std::uint64_t user_id, std::uint64_t friend_id);
```

输入要求：

- `user_id != 0`。
- `friend_id != 0`。
- 两者不能相等。
- 参数按 MySQL `BIGINT UNSIGNED` 绑定。

数据库语义：

- 在同一事务中写入两个方向：
  - `(user_id, friend_id)`
  - `(friend_id, user_id)`
- 使用 `ON DUPLICATE KEY UPDATE created_at_ms = friendships.created_at_ms` 保持幂等。
- 重复添加不会生成脏数据，也不会刷新旧关系时间。

失败语义：

- 参数错误返回 `InvalidArgument`。
- 外键不存在或 MySQL 错误返回对应 MySQL `Status`。
- 任意插入失败都会 `ROLLBACK`。

### `getFriends()`

```cpp
Status getFriends(std::uint64_t user_id, std::vector<UserProfileRecord>& friends);
```

查询某用户的好友列表。

SQL 语义：

- 从 `friendships` join `users`。
- 返回公开 `UserProfileRecord`，只包含 `user_id`、`username`、`nickname` 和 `created_at_ms`。
- 不返回 `password_hash` / `password_salt`，避免好友列表响应误带认证字段。
- 按 `u.user_id ASC` 排序，保持测试和接口稳定。

输出：

- 调用开始先 clear。
- 没有好友返回 ok + 空 vector。

### private helper

`.cpp` 内部 helper 包括：

- `validateUserId()`：检查非 0。
- `bindUserId()`：校验后绑定参数。
- `rowToUserProfileRecord()`：把 join 后的 users 行转换成公开资料 DTO。
- `rollbackSilently()`：事务失败时尽力回滚。

```cpp
class GroupDao {
public:
    explicit GroupDao(MySqlPool& pool,
                      std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status createGroup(const CreateGroupRequest& request, GroupRecord& created_group);
    Status addGroupMember(std::uint64_t group_id, std::uint64_t user_id);
    Status removeGroupMember(std::uint64_t group_id, std::uint64_t user_id);
    Status getGroupMembers(std::uint64_t group_id, std::vector<GroupMemberRecord>& members);
    Status findGroupById(std::uint64_t group_id, GroupRecord& group);
};
```

### 构造函数

和 `FriendDao` 一样，保存非 owning pool 指针和 acquire timeout。

### `createGroup()`

```cpp
Status createGroup(const CreateGroupRequest& request, GroupRecord& created_group);
```

输入要求：

- `request.owner_id != 0`。
- `request.group_name` 不能为空。
- owner id 按 MySQL `BIGINT UNSIGNED` 绑定。

事务语义：

```text
START TRANSACTION
    -> INSERT chat_groups
    -> SELECT LAST_INSERT_ID()
    -> INSERT owner into group_members
COMMIT
```

这样避免出现“群已创建，但 owner 不在成员表”的半成品。

输出：

- `created_group` 是查回的完整 `GroupRecord`。

### `addGroupMember()`

```cpp
Status addGroupMember(std::uint64_t group_id, std::uint64_t user_id);
```

语义：

- 校验 group_id / user_id。
- 向 `group_members` 插入成员。
- 使用 `ON DUPLICATE KEY UPDATE joined_at_ms = group_members.joined_at_ms` 保持幂等。
- 重复加群不刷新入群时间。

### `removeGroupMember()`

```cpp
Status removeGroupMember(std::uint64_t group_id, std::uint64_t user_id);
```

流程：

1. 校验 group_id / user_id。
2. 查询 `chat_groups.owner_id`。
3. 如果要移除 owner，返回 `InvalidArgument`。
4. 删除 `group_members` 里的普通成员。
5. affected rows 为 0 时返回 `NotFound`。

为什么 owner 不能被普通移除：

`chat_groups.owner_id` 仍指向 owner。如果把 owner 从 members 删除，会出现“群主不是群成员”的不一致状态。

### `getGroupMembers()`

```cpp
Status getGroupMembers(std::uint64_t group_id, std::vector<GroupMemberRecord>& members);
```

查询群成员列表。

SQL 语义：

- 从 `group_members` join `users`。
- 返回 `user_id`、`username`、`nickname`、`joined_at_ms`。
- 按 `joined_at_ms ASC, user_id ASC` 排序。

输出：

- 开始先 clear。
- 没有成员返回 ok + 空 vector。

### `findGroupById()`

```cpp
Status findGroupById(std::uint64_t group_id, GroupRecord& group);
```

按 group id 查询群资料。

失败语义：

- 参数非法返回 `InvalidArgument`。
- 群不存在返回 `NotFound`。
- 多行或行格式异常返回 `InternalError`。

### private helper

`.cpp` 内部 helper 包括：

- `validateId()` / `bindId()`：统一校验 group_id / user_id。
- `rowToGroupRecord()`：解析群资料。
- `rowToGroupMemberRecord()`：解析成员列表行。
- `querySingleGroup()`：统一处理群不存在、多行和正常单行。
- `queryLastInsertedGroup()`：建群后查回自增 id。
- `queryGroupOwner()`：移除成员前查询 owner。
- `insertGroupMember()`：建群和加成员共用的幂等插入。
- `rollbackSilently()`：事务失败时回滚。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

后续业务 service 会使用这些 DAO：

- FriendService：添加好友、拉取好友列表。
- GroupService：建群、加群、退群、拉成员列表、查询群资料。
- ChatService：发送群消息前可以查询群成员。
- HistoryService：展示会话信息时可以补充群名或成员信息。

### 2. 上下层调用连接

```text
Friend/Group Packet
    -> Session
    -> business ThreadPool
    -> FriendService / GroupService
    -> FriendDao / GroupDao
    -> MySqlPool
    -> friendships / chat_groups / group_members
```

DAO 不向客户端推送通知，也不缓存群成员；这些是后续 service 和 Redis cache 的职责。

### 3. 整体运行链路

添加好友链路：

1. FriendService 校验请求者身份。
2. 调用 `FriendDao::addFriendship(user_id, friend_id)`。
3. DAO 在事务中写两个方向。
4. service 组装响应。

创建群链路：

1. GroupService 收到建群请求。
2. 调用 `GroupDao::createGroup({owner_id, group_name}, group)`。
3. DAO 在事务中插入群，再插入 owner 成员。
4. service 返回群资料。

移除成员链路：

1. GroupService 校验操作者权限。
2. 调用 `removeGroupMember(group_id, user_id)`。
3. DAO 查询 owner。
4. owner 不允许移除；普通成员可删除。

### 4. 自身内部运行流程

好友双向写入流程：

```text
validate user_id/friend_id
    -> reject same id
    -> pool.acquire()
    -> START TRANSACTION
    -> INSERT two rows with ON DUPLICATE KEY no-op
    -> COMMIT
```

建群流程：

```text
validate owner and group_name
    -> pool.acquire()
    -> START TRANSACTION
    -> INSERT chat_groups
    -> SELECT LAST_INSERT_ID()
    -> INSERT owner into group_members
    -> COMMIT
```

成员查询流程：

```text
validate group_id
    -> pool.acquire()
    -> SELECT group_members JOIN users
    -> rowToGroupMemberRecord()
    -> sorted vector
```

### 5. 该项目代码在实际应用中的具体数据例子

seed 数据里 Alice (`user_id=1001`) 和 Bob (`user_id=1002`) 是好友，`FriendDao::getFriends(1001)` 返回 Bob 的公开资料而不是 password hash。群 `group_id=2001` 的 owner 是 Alice，成员有 `1001`、`1002`；`GroupDao::removeGroupMember(2001, 1001)` 会拒绝移除 owner，避免群仍指向 owner_id 但成员表缺失 owner。

## 6. 关键实现点

- 保持模块职责单一。
- 失败时返回清晰错误，不吞掉异常状态。
- 不跨越本 Step 边界提前实现后续业务。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `FriendDao 和 GroupDao` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

测试覆盖：

- 头文件自包含。
- 添加好友后双方关系存在。
- 重复添加好友不产生重复关系。
- 创建群后群资料存在，owner 是成员。
- 重复添加群成员幂等。
- 移除普通成员成功。
- 查询不存在群返回 `NotFound`。

测试用例在 MySQL 中创建独立用户和群，避免依赖 seed 数据的可变状态。

## 8. 验证命令

```bash
cmake --build build
docker compose -f docker/docker-compose.yml ps
ctest --test-dir build -R "FriendGroupDao" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

### 一句话

Step 27 实现好友和群组 DAO。

### 展开说

可以这样说：

> Step 27 实现好友和群组 DAO。好友关系第一版用 friendships 表的两行表达双向关系，`addFriendship()` 在一个事务里插入两个方向，并用 `ON DUPLICATE KEY UPDATE` 做幂等 no-op。`getFriends()` 返回公开 `UserProfileRecord`，不把密码 hash 或 salt 带出 DAO。群组用 `chat_groups.owner_id` 表达群主，用 `group_members` 表达成员；建群时在一个事务里插入群资料和 owner 成员，移除成员前会查询 owner，拒绝把群主从成员表删除。DAO 只做数据访问，不做好友审批、群权限系统或网络通知。

### 容易被追问

- 为什么好友关系写双向两行？
- 为什么不能移除群 owner？

## 10. 面试常见追问

### Q1：为什么好友关系写双向两行？

第一版好友关系是互为好友，不做申请审批。写双向行后，查询任意一方好友列表都能走 `user_id` 条件，不需要额外推导。

### Q2：为什么不能移除群 owner？

`chat_groups.owner_id` 仍指向 owner。如果成员表把 owner 删掉，会让群资料和成员关系不一致，所以 DAO 第一版直接拒绝。
