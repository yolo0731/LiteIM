# Step 25：UserDao 和 AuthDao

## 1. 概念

Step 23 有单连接和 prepared statement，Step 24 有连接池。Step 25 开始把这些底层能力收敛成 DAO。

DAO 的职责很窄：

- 只访问 MySQL 表。
- 只把 SQL 行转换成 C++ DTO。
- 只处理数据库层错误语义。
- 不做注册、登录、密码校验、会话绑定或网络响应。

本 Step 只做 `users` 表：

- `UserDao::createUser()` 插入用户并返回完整 `UserRecord`。
- `UserDao::findUserByUsername()` 按用户名查询。
- `UserDao::findUserById()` 按用户 id 查询。
- `AuthDao::usernameExists()` 给后续 AuthService 做用户名存在性检查。

重复用户名不是普通 `IoError`。DAO 会把 MySQL duplicate key 转成 `ErrorCode::AlreadyExists`，让上层业务能明确区分“用户名已存在”和“数据库不可用”。

## 2. hpp 接口说明

### `ErrorCode::AlreadyExists`

Step 25 新增 `AlreadyExists` 错误码，用于表达唯一约束冲突这类“资源已经存在”的数据访问结果。当前使用场景是 `users.username` 唯一索引冲突。

### `PreparedStatement::lastErrorNumber()`

`PreparedStatement` 新增：

```cpp
unsigned int lastErrorNumber() const noexcept;
```

它返回 `mysql_stmt_errno()`。DAO 用它识别 MySQL duplicate key 错误码 `1062`，避免只靠错误字符串判断。

### `UserDao`

`UserDao` 依赖 `MySqlPool`：

```cpp
explicit UserDao(MySqlPool& pool,
                 std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));
```

public 方法：

- `createUser(const CreateUserRequest&, UserRecord&)`：插入 `users` 行，写入 `created_at_ms` 和 `updated_at_ms`，成功后按 username 查回完整记录。
- `findUserByUsername(const std::string&, UserRecord&)`：查询唯一 username；不存在返回 `NotFound`。
- `findUserById(std::uint64_t, UserRecord&)`：查询 user_id；不存在返回 `NotFound`。

失败语义：

- 连接池借连接失败时直接返回连接池错误。
- MySQL prepare/bind/execute/query 失败时返回底层 `Status`。
- 用户名重复返回 `AlreadyExists`。
- 查询不到用户返回 `NotFound`。
- 查询行格式异常返回 `InternalError`。

### `AuthDao`

`AuthDao` 当前只负责注册/登录前的轻量查询：

```cpp
Status usernameExists(const std::string& username, bool& exists);
```

它不做密码验证，也不做登录失败限制。登录失败限制属于 Redis cache 后续步骤。

## 3. 作用场景和运行流程

后续注册流程会是：

```text
business ThreadPool
  -> AuthDao::usernameExists(username)
  -> password hash/salt 生成
  -> UserDao::createUser(request, created_user)
  -> EventLoop::queueInLoop() 返回注册结果
```

后续登录流程会是：

```text
business ThreadPool
  -> UserDao::findUserByUsername(username, user)
  -> service 层校验 password_hash/password_salt
  -> Redis 记录在线状态
  -> EventLoop::queueInLoop() 绑定 Session / 返回登录结果
```

DAO 内部的一次查询流程：

```text
MySqlPool::acquire(timeout, guard)
  -> PreparedStatement::prepare()
  -> bindString() / bindInt64()
  -> executeQuery()
  -> MySqlQueryResult row
  -> UserRecord
  -> ConnectionGuard 析构归还连接
```

线程边界：

- DAO 方法会阻塞等待连接池和 MySQL 响应，后续只能在 business `ThreadPool` 中调用。
- DAO 不持有 `Session`，不直接操作 `EventLoop`，不跨线程触碰网络对象。
- `ConnectionGuard` 生命周期限制在 DAO 方法内部，不把 MySQL 连接泄露给业务层长期持有。

## 4. 测试

新增 `tests/storage/user_dao_test.cpp`：

- `UserDaoTest.HeadersAreSelfContained`：`UserDao` / `AuthDao` 头文件可独立使用。
- `UserDaoIntegrationTest.CreateUserPersistsAndReturnsCreatedRecord`：创建用户成功并返回字段。
- `UserDaoIntegrationTest.CreateUserWorksWithSingleConnectionPool`：确认 `createUser()` 插入后先归还连接，再查询完整用户，避免 1 连接池自我等待。
- `UserDaoIntegrationTest.DuplicateUsernameReturnsAlreadyExists`：重复 username 返回 `AlreadyExists`。
- `UserDaoIntegrationTest.FindUserByUsernameReturnsCreatedUser`：按 username 查回用户。
- `UserDaoIntegrationTest.FindUserByIdReturnsCreatedUser`：按 user_id 查回用户。
- `UserDaoIntegrationTest.FindMissingUserReturnsNotFound`：不存在用户返回 `NotFound`。
- `UserDaoIntegrationTest.UsernameExistsReportsExistingAndMissingUsers`：存在性查询能区分 true / false。

测试使用 `step25_` 用户名前缀，并在 SetUp/TearDown 清理自己的测试用户，不修改 seed 用户 `alice`、`bob`、`mira_bot`。

## 5. 验证命令

```bash
docker compose -f docker/docker-compose.yml up -d --wait
cmake --build build
ctest --test-dir build -R "UserDaoTest|UserDaoIntegrationTest|ErrorCodeTest" --output-on-failure
ctest --test-dir build --output-on-failure
```

本 Step 不实现 `MessageDao`、`OfflineMessageDao`、Redis client、AuthService 或 ChatService。它只把 `users` 表的数据访问封装成 DAO。
