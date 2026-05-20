# Step 57：好友申请和私聊权限

## 0. 本 Step 结论

- 目标：把“知道 user_id 就能私聊”的演示版权限改成 accepted 好友才能私聊。
- 前置依赖：依赖 Step 35 的 FriendService、Step 36 的 ChatService、Step 53-55 的可靠投递扩展和 Step 56 的业务线程池保护。
- 主要交付：好友添加改为 pending 申请，新增接受/拒绝协议，MySQL 新增 `friend_requests` 表，私聊发送前检查 accepted 好友关系。
- 线程边界：好友和私聊权限检查仍在 business `ThreadPool` 中执行，I/O 线程不访问 MySQL / Redis。
- 范围控制：不做黑名单、备注、好友分组、群审批、禁言、踢人或复杂隐私风控。

## 1. 为什么需要这个 Step

前面的好友和私聊链路能跑通，但权限语义仍偏演示版：

```text
Alice 知道 Bob 的 user_id
    -> 直接 PrivateMessageRequest
    -> 服务端只检查 Bob 是否存在
    -> 保存或推送消息
```

真实 IM 至少要防止任意用户靠猜测 user_id 发私聊。Step 57 做最小权限收口：

```text
AddFriendRequest
    -> friend_requests pending
AcceptFriendRequest
    -> friendships accepted 双向关系
PrivateMessageRequest
    -> areFriends(sender, receiver) == true 才允许发送
RejectFriendRequest
    -> friend_requests rejected，不创建 friendships
```

这样面试时可以明确区分“关系申请状态”和“已接受好友关系”，也能说明为什么私聊权限不能只靠客户端 UI 限制。

## 2. 本 Step 边界

### 本 Step 做

- `AddFriendRequest` 从“直接加好友”改成“发送好友申请”。
- 新增 `AcceptFriendRequest` / `AcceptFriendResponse`。
- 新增 `RejectFriendRequest` / `RejectFriendResponse`。
- 新增 `TlvType::FriendRequestStatus`，用 `0/1/2` 表示 pending / accepted / rejected。
- MySQL 新增 `friend_requests` 表，`friendships` 只存 accepted 双向关系。
- `IStorage` / `FriendDao` / `MySqlStorage` 新增 friend request 和 `areFriends()` 接口。
- `ChatService` 私聊发送前检查 accepted 好友关系。
- CLI 和 Python E2E 覆盖申请、接受、拒绝和未授权私聊失败。

### 本 Step 不做

- 不做黑名单、备注名、好友分组、删除好友。
- 不做好友申请通知 push，也不做待处理申请列表。
- 不做群加入审批、管理员、禁言、踢人。
- 不做陌生人风控、举报、限频策略。
- 不做多设备好友申请同步。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/protocol/MessageType.hpp` / `src/protocol/MessageType.cpp` | 新增 accept/reject friend message types | 固定好友申请审批协议编号和分类 |
| `include/liteim/protocol/Tlv.hpp` / `src/protocol/Tlv.cpp` | 新增 `FriendRequestStatus` | 响应中表达 pending / accepted / rejected |
| `include/liteim/storage/StorageTypes.hpp` | 新增 `FriendRequestStatus` / `FriendRequestRecord` | 让 storage 层有明确申请状态 DTO |
| `include/liteim/storage/IStorage.hpp` | 新增 friend request 和 `areFriends()` 接口 | 业务层不直接依赖 MySQL DAO |
| `include/liteim/storage/FriendDao.hpp` / `src/storage/FriendDao.cpp` | 新增申请、接受、拒绝、好友检查 | 访问 `friend_requests` / `friendships` |
| `include/liteim/storage/MySqlStorage.hpp` / `src/storage/MySqlStorage.cpp` | 转发新接口到 `FriendDao` | 保持 service 只依赖 `IStorage` |
| `scripts/init_mysql.sql` / `scripts/migrations/057_friend_requests.sql` | 新增 `friend_requests` 表 | 支持已有开发库迁移 |
| `include/liteim/service/FriendService.hpp` / `src/service/FriendService.cpp` | 新增 accept/reject handler，AddFriend 改为 pending request | 关系业务入口 |
| `src/service/ChatService.cpp` | 私聊前调用 `areFriends()` | 防止非好友私聊 |
| `client_cli/ClientCli.cpp` | 新增 `accept-friend` / `reject-friend` | 手工联调好友审批 |
| `tests/` | 扩展协议、CLI、service、DAO、storage、E2E 测试 | 固定 Step57 行为 |
| `README.md` / `docs/tutorials/` / process 文件 | 更新 | 同步当前运行时语义 |

## 4. 核心接口与契约

### 协议契约

好友相关消息类型：

```cpp
AddFriendRequest = 200,
AddFriendResponse = 201,
ListFriendsRequest = 202,
ListFriendsResponse = 203,
AcceptFriendRequest = 204,
AcceptFriendResponse = 205,
RejectFriendRequest = 206,
RejectFriendResponse = 207,
```

`AddFriendRequest(TargetUserId=1002)` 的含义是：当前登录用户向 `user_id=1002` 发起好友申请。它不再直接写 `friendships`。

`AcceptFriendRequest(TargetUserId=1001)` 的含义是：当前登录用户接受 `user_id=1001` 发来的申请。这里复用 `TargetUserId`，但语义是“申请发起者 id”，不是当前用户要添加的目标。

`RejectFriendRequest(TargetUserId=1001)` 的含义是：当前登录用户拒绝 `user_id=1001` 发来的申请。

响应会返回对方公开资料和申请状态：

```text
FriendId
Username
Nickname
OnlineStatus
FriendRequestStatus
```

其中 `FriendRequestStatus`：

- `0`：pending
- `1`：accepted
- `2`：rejected

### Storage 契约

`IStorage` 新增：

```cpp
Status createFriendRequest(std::uint64_t requester_id,
                           std::uint64_t target_user_id,
                           FriendRequestRecord& request);

Status acceptFriendRequest(std::uint64_t requester_id,
                           std::uint64_t target_user_id);

Status rejectFriendRequest(std::uint64_t requester_id,
                           std::uint64_t target_user_id);

Status areFriends(std::uint64_t user_id,
                  std::uint64_t friend_id,
                  bool& are_friends);
```

失败语义：

- user id 为 0 或双方相同：`InvalidArgument`。
- 待处理申请不存在：`NotFound`。
- 重复申请、重复接受、已拒绝后再次处理：`AlreadyExists`。
- 已是 accepted 好友时再次申请或接受：`AlreadyExists`。

### MySQL 契约

`friend_requests` 表保存申请状态：

```sql
PRIMARY KEY (requester_id, target_user_id)
status IN (0, 1, 2)
```

`friendships` 表仍只保存 accepted 关系，且继续写双向两行：

```text
(1001, 1002)
(1002, 1001)
```

接受申请时，`FriendDao::acceptFriendRequest()` 在同一事务里把申请状态改为 accepted，并插入双向 friendships 行。

## 5. 运行流程

### 1. 好友申请流程

```text
Alice session_id=42, user_id=1001
    -> AddFriendRequest(TargetUserId=1002)
    -> FriendService::handleAddFriend()
    -> MySqlStorage::createFriendRequest(1001, 1002)
    -> friend_requests(1001, 1002, pending)
    -> AddFriendResponse(FriendId=1002, FriendRequestStatus=0)
```

此时 Alice 和 Bob 还不是好友，`ListFriendsRequest` 不会返回 Bob。

### 2. 接受申请流程

```text
Bob session_id=43, user_id=1002
    -> AcceptFriendRequest(TargetUserId=1001)
    -> FriendService::handleAcceptFriend()
    -> MySqlStorage::acceptFriendRequest(1001, 1002)
    -> update friend_requests.status = accepted
    -> insert friendships(1001, 1002)
    -> insert friendships(1002, 1001)
    -> AcceptFriendResponse(FriendId=1001, FriendRequestStatus=1)
```

接受后，Alice 和 Bob 的好友列表都能查到对方。

### 3. 拒绝申请流程

```text
Bob user_id=1002
    -> RejectFriendRequest(TargetUserId=1001)
    -> friend_requests(1001, 1002).status = rejected
    -> 不写 friendships
    -> RejectFriendResponse(FriendId=1001, FriendRequestStatus=2)
```

拒绝后，Alice 再给 Bob 发私聊会得到 `ErrorResponse`。

### 4. 私聊权限流程

```text
PrivateMessageRequest(ReceiverId=1002, MessageText="hello")
    -> ChatService::currentUserId() 得到 sender_id=1001
    -> findUserById(1002)
    -> areFriends(1001, 1002)
    -> false: 返回 InvalidArgument
    -> true: 继续 client_msg_id 幂等、保存消息、在线 push 或离线记录
```

权限检查发生在保存消息前，因此非好友私聊不会产生 message row、offline row 或 unread 计数。

## 6. 关键实现点

### 1. `friend_requests` 和 `friendships` 分表

pending / rejected 是申请状态，不应该出现在好友列表中。把它们塞进 `friendships` 会让 `getFriends()` 每次都要带状态过滤，也容易让业务误把 pending 当好友。

当前设计保持：

- `friend_requests`：审批流程。
- `friendships`：已接受关系。

### 2. `AddFriendRequest` 语义改变

旧语义是直接添加好友；新语义是创建 pending 申请。这样协议编号不变，旧 CLI 命令仍叫 `add-friend`，但服务端行为更接近真实 IM。

### 3. 接受申请必须是事务

接受申请有两个动作：

1. 把 `friend_requests.status` 改成 accepted。
2. 写双向 `friendships`。

如果只做其中一半，系统会出现“申请已同意但不能私聊”或“能私聊但申请状态没变”的不一致。因此 `FriendDao::acceptFriendRequest()` 使用同一条 MySQL 连接和事务。

### 4. 私聊权限放在 service 层

`ChatService` 负责业务语义：当前登录用户能不能给 receiver 发消息。DAO 只负责查询关系。这样未来增加黑名单、陌生人私信策略或好友删除时，仍然可以在 `ChatService` 这一层组合规则。

### 5. 重复操作给明确结果

重复申请、重复接受、已拒绝后再次处理都返回明确错误。第一版不支持 rejected 后重新申请，避免在 Step57 引入更复杂的申请生命周期。

## 7. 测试设计

| 风险 | 测试覆盖 |
| --- | --- |
| 新协议类型没有被分类 | `MessageTypeTest` 覆盖 accept/reject request/response |
| 新 TLV 字段不可读 | `TlvTypeTest` 覆盖 `FriendRequestStatus` |
| CLI 构包错误 | `ClientCliCommandTest.AcceptFriendCommandBuildsRequest` / `RejectFriendCommandBuildsRequest` |
| `AddFriendRequest` 仍然直接创建好友 | `FriendServiceFixture.AddFriendCreatesPendingRequestAndReturnsFriendOnlineStatus` |
| 接受申请没写双向好友 | `FriendServiceFixture.AcceptFriendRequestCreatesAcceptedFriendship` 和 DAO 集成测试 |
| 拒绝申请仍可私聊 | Python E2E rejected 流程 |
| 非好友私聊被保存 | `ChatServiceFixture.PrivateMessageRequiresAcceptedFriendship` |
| 重复申请/接受结果不明确 | `FriendGroupDaoIntegrationTest.RepeatedFriendRequestAndAcceptReturnClearErrors` |
| MySQL 聚合适配没有转发新接口 | `MySqlStorageIntegrationTest.ImplementsIStorageForUsersFriendsAndPublicProfiles` |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests liteim_server -j2

docker compose -f docker/docker-compose.yml up -d --wait
mysql -h127.0.0.1 -P33060 -uliteim -p6 liteim < scripts/migrations/057_friend_requests.sql

ctest --test-dir build -R "MessageType|TlvType|ClientCliCommandTest|FriendService|ChatService|FriendGroupDaoIntegrationTest|MySqlStorageIntegrationTest|LiteIME2E.test_private_chat" --output-on-failure

ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

一句话：

> 我把 LiteIM 的好友关系从“直接添加”收紧成“申请、接受、拒绝”，并且在私聊保存前强制检查 accepted 好友关系，避免任意用户知道 user_id 就能发私聊。

展开说：

> 我把申请状态和好友关系拆成两张表。`friend_requests` 保存 pending / accepted / rejected，`friendships` 只保存 accepted 后的双向好友关系。`AddFriendRequest` 只创建 pending 申请，`AcceptFriendRequest` 在事务里更新申请状态并写双向好友行，`RejectFriendRequest` 只标记 rejected。`ChatService` 在保存私聊消息前调用 `IStorage::areFriends()`，非好友直接返回错误，不会产生消息、离线记录或未读计数。

容易被追问：

> 为什么不把 pending 放进 `friendships`？为什么接受申请要事务？为什么权限检查放在 `ChatService` 而不是客户端？拒绝后能不能重新申请？这些问题要围绕“好友列表只展示 accepted 关系”和“第一版控制复杂度”回答。

## 10. 面试常见追问

### Q1：为什么新增 `friend_requests`，不直接给 `friendships` 加 status？

因为 `friendships` 当前语义已经很清楚：只表示 accepted 好友。好友列表、私聊权限和历史权限都可以直接查它。如果把 pending/rejected 混进去，每个使用方都要记得过滤状态，容易出错。

### Q2：为什么 `AcceptFriendRequest` 里的 `TargetUserId` 表示 requester？

第一版为了少引入协议字段，复用已有 `TargetUserId`。请求语义由 message type 决定：在 `AddFriendRequest` 中它表示被申请用户，在 `AcceptFriendRequest` / `RejectFriendRequest` 中它表示申请发起者。

### Q3：为什么接受申请要写双向 friendships？

这样查询任意一方好友列表都能用 `WHERE user_id = ?` 命中主键前缀，逻辑简单，读路径稳定。写入时只在接受申请这一刻多写一行。

### Q4：为什么非好友私聊返回 `InvalidArgument`？

当前 `ErrorCode` 还没有专门的 permission code。第一版用 `InvalidArgument` 表达“这个 receiver 对当前 sender 不是合法私聊目标”。后续如果补权限错误码，可以把这里改成更精确的 `PermissionDenied`。

### Q5：拒绝后为什么第一版不允许重新申请？

为了控制 Step57 范围。重新申请需要定义冷却时间、撤销、覆盖 rejected、通知和重复申请策略。当前先保证拒绝后不能私聊、重复操作有明确错误；复杂生命周期留到后续隐私/风控 Step。

### Q6：群聊也要做类似权限吗？

群聊已经通过群成员关系控制能否发群消息。Step57 不做群审批、禁言、踢人和管理员权限，因为那些是群管理模型，不是私聊好友权限的最小修复。
