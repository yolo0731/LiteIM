# Step 15：实现 SQLiteStorage

本步骤目标：把 Step 14 定义的 `IStorage` 抽象落地成 SQLite 实现。

Step 15 做三件事：

- 补 `sql/init.sql` 表结构。
- 新增 `SQLiteStorage`。
- 用测试验证用户、好友、群组、消息、离线消息和文件持久化。

本 Step 不实现注册、登录、私聊转发、群聊转发，也不让 `MessageRouter` 依赖 storage。业务逻辑从 Step 16 开始再接入。

## 1. 为什么需要 SQLiteStorage

Step 14 只有接口：

```text
service layer
    ↓ depends on
IStorage
```

接口能让业务层先不关心数据库，但项目最终还是需要真实持久化：

- 用户注册后要保存账号。
- 登录时要查用户。
- 私聊和群聊消息要保存。
- 用户离线时要能查未投递消息。
- 群组和群成员关系要持久化。

所以 Step 15 增加具体实现：

```text
service layer
    ↓ depends on
IStorage
    ↑ implemented by
SQLiteStorage
    ↓ uses
SQLite
```

这样后续业务层仍然只依赖 `IStorage`，不会直接包含 `sqlite3.h`。

## 2. 本 Step 新增和修改的文件

新增：

```text
include/liteim/storage/SQLiteStorage.hpp
src/storage/SQLiteStorage.cpp
tests/test_sqlite_storage.cpp
tutorials/step15_sqlite_storage.md
```

修改：

```text
src/CMakeLists.txt
tests/CMakeLists.txt
tests/test_main.cpp
sql/init.sql
README.md
docs/architecture.md
docs/database.md
docs/project_layout.md
docs/interview_notes.md
tutorials/README.md
```

## 3. sql/init.sql 讲解

Step 15 把 `sql/init.sql` 从占位文件变成真实 schema。

### PRAGMA foreign_keys

```sql
PRAGMA foreign_keys = ON;
```

SQLite 默认不一定启用外键检查。这里在 schema 中写一遍，`SQLiteStorage` 构造时也会对当前连接执行一遍。

### users

`users` 表保存用户：

- `id`：自增主键。
- `username`：唯一用户名。
- `nickname`：昵称。
- `password_salt`：密码 salt。
- `password_hash`：密码 hash。
- `user_type`：普通用户或 Bot。
- `created_at`：创建时间戳。

`username` 有唯一约束，重复创建会失败。

### friendships

`friendships` 表保存好友关系：

- `user_id`
- `friend_id`
- `created_at`

主键是 `(user_id, friend_id)`，并且有 `CHECK (user_id <> friend_id)` 防止自己加自己。

当前 `SQLiteStorage::addFriendship()` 会写入双向关系：

```text
alice -> bob
bob   -> alice
```

这样双方查好友列表时都能查到对方。

### groups

`groups` 表保存群组：

- `id`
- `name`
- `owner_id`
- `created_at`

`owner_id` 外键指向 `users(id)`。

### group_members

`group_members` 表保存群成员关系：

- `group_id`
- `user_id`
- `joined_at`

主键是 `(group_id, user_id)`，避免重复成员。

### messages

`messages` 表同时保存私聊和群聊消息：

- `message_type = 0` 表示私聊。
- `message_type = 1` 表示群聊。
- 私聊使用 `receiver_id`。
- 群聊使用 `group_id`。
- `delivered` 当前用于查询私聊离线消息。

表里有 `CHECK` 约束，保证私聊必须有 `receiver_id` 且没有 `group_id`，群聊必须有 `group_id` 且没有 `receiver_id`。

## 4. SQLiteStorage.hpp 讲解

### SQLiteStorage()

```cpp
explicit SQLiteStorage(
    std::string db_path = "liteim.db",
    std::string schema_path = "sql/init.sql");
```

作用：

- 打开 SQLite 数据库。
- 开启 foreign key 检查。
- 执行 schema 文件。

输入：

- `db_path`：数据库路径。默认是 `liteim.db`。
- `schema_path`：schema 文件路径。默认是 `sql/init.sql`。

输出：

- 构造函数没有返回值。

副作用：

- 如果数据库文件不存在，会创建文件。
- 会执行 `CREATE TABLE IF NOT EXISTS`。

边界情况：

- 打不开数据库会抛异常。
- schema 文件不存在会抛异常。
- schema SQL 执行失败会抛异常。

### ~SQLiteStorage()

```cpp
~SQLiteStorage() override;
```

作用：

- 关闭 `sqlite3*` 数据库连接。

为什么需要：

- `sqlite3_open_v2()` 得到的是 C API 资源。
- C++ 对象析构时必须释放它，避免连接泄漏。

### IStorage 方法实现

`SQLiteStorage` 覆盖了 `IStorage` 的所有方法：

- `createUser()`
- `findUserByUsername()`
- `findUserById()`
- `addFriendship()`
- `getFriends()`
- `createGroup()`
- `addGroupMember()`
- `removeGroupMember()`
- `getGroupMembers()`
- `findGroupById()`
- `savePrivateMessage()`
- `saveGroupMessage()`
- `getPrivateHistory()`
- `getGroupHistory()`
- `getOfflineMessages()`

这些函数的业务语义已经在 Step 14 讲过。Step 15 的重点是：它们现在真正通过 SQL 操作 SQLite。

## 5. SQLiteStorage.cpp 关键实现

### Statement RAII

`.cpp` 内部定义了一个 `Statement` 类，负责管理 `sqlite3_stmt*`。

作用：

- 构造时调用 `sqlite3_prepare_v2()`。
- 析构时调用 `sqlite3_finalize()`。
- 提供 `bindInt64()`、`bindInt()`、`bindText()` 和 `step()`。

为什么要这样做？

prepared statement 是 SQLite 的 C 资源。如果中间出现异常，手动 `finalize()` 容易遗漏。RAII 可以保证对象离开作用域时自动释放。

### executeSchemaFile()

```cpp
void executeSchemaFile(const std::string& schema_path) const;
```

作用：

- 读取 `sql/init.sql`。
- 调用 `sqlite3_exec()` 执行整份 schema。

边界：

- 读不到文件会抛异常。
- SQL 执行失败会抛异常。

### createUser()

实现逻辑：

```text
检查 username 是否为空
    ↓
INSERT INTO users (...)
    ↓
成功后 sqlite3_last_insert_rowid()
    ↓
findUserById(id)
```

如果用户名重复，会返回 `std::nullopt`。

### addFriendship()

实现逻辑：

```text
检查两个用户都存在，且不是同一个人
    ↓
BEGIN IMMEDIATE
    ↓
INSERT OR IGNORE alice -> bob
INSERT OR IGNORE bob -> alice
    ↓
COMMIT
```

用事务是为了保证双向关系一起成功或一起失败。

### createGroup()

实现逻辑：

```text
检查群主存在
    ↓
BEGIN IMMEDIATE
    ↓
INSERT INTO groups
    ↓
INSERT INTO group_members，把群主加入群
    ↓
COMMIT
```

创建群时自动加入群主，这样后续查询群成员时不会漏掉 owner。

### savePrivateMessage()

私聊消息会写入 `messages`：

```text
message_type = 0
sender_id = 发送者
receiver_id = 接收者
group_id = NULL
delivered = 请求传入值
```

如果发送者或接收者不存在，外键约束失败，函数返回 `std::nullopt`。

### saveGroupMessage()

群聊消息也写入 `messages`：

```text
message_type = 1
sender_id = 发送者
receiver_id = NULL
group_id = 群 ID
delivered = 1
```

当前没有在 `SQLiteStorage` 中检查发送者是否是群成员。这个属于业务规则，后续 `GroupService` 处理。

### getPrivateHistory()

查询两个用户之间的双向私聊历史：

```sql
(sender_id = A AND receiver_id = B)
OR
(sender_id = B AND receiver_id = A)
```

排序方式：

```text
created_at ASC, id ASC
```

这样同一时间戳下也能用自增 ID 稳定排序。

### getGroupHistory()

按 `group_id` 查询群消息历史，同样按 `created_at ASC, id ASC` 排序。

### getOfflineMessages()

查询条件：

```text
message_type = private
receiver_id = user_id
delivered = 0
```

当前只查询私聊离线消息。群离线消息策略后续再设计。

## 6. CMake 变化

`src/CMakeLists.txt` 新增：

```cmake
find_package(SQLite3 REQUIRED)
```

`liteim_storage` 现在编译：

```cmake
storage/NullCache.cpp
storage/SQLiteStorage.cpp
```

并链接：

```cmake
target_link_libraries(liteim_storage PRIVATE
    SQLite::SQLite3
)
```

SQLite 依赖只留在 storage 模块内部，业务层不直接链接 SQLite。

测试目标增加：

```cmake
target_compile_definitions(liteim_tests PRIVATE
    LITEIM_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
```

这样 `ctest` 从 build 目录运行时，测试仍然能找到源码目录下的 `sql/init.sql`。

## 7. 测试说明

本 Step 新增：

```text
tests/test_sqlite_storage.cpp
```

### 测试 1：用户创建和查询

用例名：

```text
SQLiteStorage creates and finds users
```

验证内容：

- 能创建用户。
- 用户有数据库生成的 ID 和时间戳。
- 能按用户名查询。
- 能按 ID 查询。
- 重复用户名返回空。
- 空用户名返回空。

为什么要测：

用户表是后续注册登录的基础。如果用户创建和查询不稳定，Step 16 无法可靠实现。

### 测试 2：好友和群组

用例名：

```text
SQLiteStorage handles friends and groups
```

验证内容：

- 添加好友后双方都能查到对方。
- 自己加自己失败。
- 创建群会自动加入群主。
- 可以添加和移除群成员。
- 群组可按 ID 查询。
- 未知 owner 创建群失败。

为什么要测：

好友和群成员是后续私聊、群聊权限判断的基础。

### 测试 3：消息、历史和离线消息

用例名：

```text
SQLiteStorage handles messages and offline queries
```

验证内容：

- 私聊消息可以保存。
- 无效接收者保存失败。
- 私聊历史按时间顺序返回。
- `limit` / `offset` 生效。
- 未投递消息能通过 `getOfflineMessages()` 查到。
- 群消息可以保存和分页查询。
- 无效群组保存失败。

为什么要测：

这是 Step 15 的核心持久化能力，后续私聊、群聊和离线消息都依赖它。

### 测试 4：文件数据库持久化

用例名：

```text
SQLiteStorage persists across file connections
```

验证内容：

- 第一个 `SQLiteStorage` 连接写入临时文件数据库。
- 连接析构后重新打开同一个数据库文件。
- 仍然能查到之前写入的用户。

为什么要测：

`:memory:` 只能验证 SQL 行为，不能证明文件持久化。这个测试证明 SQLite 文件数据库确实能跨连接保留数据。

### 如何运行测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

测试通过说明：

- SQLite3 正确链接。
- `sql/init.sql` 能执行。
- `SQLiteStorage` 完整实现了当前 `IStorage`。
- 内存库和文件库都能正常工作。

## 8. 面试时怎么讲

可以这样说：

> 我把存储层拆成接口和实现两层。业务层只依赖 `IStorage`，Step 15 用 `SQLiteStorage` 实现这个接口。`SQLiteStorage` 构造时打开数据库并执行 `sql/init.sql`，所有读写都用 prepared statement 和参数绑定。资源上用 RAII 管理 `sqlite3*` 和 `sqlite3_stmt*`，避免连接和 statement 泄漏。这样业务逻辑和 SQLite 细节分离，测试也可以分别用 `:memory:` 和临时文件数据库覆盖。

重点讲清楚：

- `IStorage` 是业务层依赖的抽象。
- `SQLiteStorage` 是 storage 层的具体实现。
- SQL schema 只在 storage 层。
- prepared statement 避免拼接 SQL。
- expected constraint failure 返回空/false，非预期 SQLite API 错误抛异常。

## 9. 面试常见追问

### 为什么不让 AuthService 直接写 SQLite？

因为 AuthService 应该只负责注册、登录、密码校验等业务规则。SQL、连接、statement、schema 属于存储细节。直接耦合会让业务测试依赖数据库，也让以后替换存储很难。

### 为什么要执行 `PRAGMA foreign_keys = ON`？

SQLite 的外键检查是连接级设置。每次打开连接后都要开启，否则 schema 里写了 foreign key 也可能不生效。

### 为什么好友关系写双向？

当前业务更接近互为好友。写双向后，查任意一方好友列表都很直接，不需要在查询时做复杂的双向 union。

### 为什么私聊和群聊共用 messages 表？

它们共享消息 ID、发送者、消息体、创建时间等字段。用 `message_type` 区分后，可以减少表数量，并通过 `CHECK` 约束保证私聊和群聊字段组合正确。

### 为什么测试同时用 `:memory:` 和文件数据库？

`:memory:` 速度快，适合验证接口行为；文件数据库能验证真实持久化和重复执行 schema 的幂等性。两者覆盖的风险不同。
