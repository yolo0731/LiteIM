# Step 25：UserDao 和 AuthDao

Step 25 的目标是在 MySQL wrapper 和连接池之上实现 users 表 DAO。

到 Step 24 为止，LiteIM 已经能从业务线程安全借出 MySQL 连接，但还没有任何业务表访问代码。Step 25 解决的问题是：

```text
注册 / 登录相关服务如何通过清晰的 DAO 访问 users 表？
```

答案是先实现 `UserDao` 和 `AuthDao`，只做数据访问，不做业务判断。

## 1. 概念

DAO 是 Data Access Object。它的职责是把数据库表操作封装成 C++ 方法：

```text
UserDao::createUser()
UserDao::findUserByUsername()
UserDao::findUserById()
AuthDao::usernameExists()
```

它不负责：

- 判断用户名格式是否合法。
- 计算密码 hash。
- 校验密码。
- 生成登录 token。
- 写 Redis 在线状态。
- 操作 `Session`。

这些会在后续 AuthService 中完成。

Step 25 还新增了两个重要错误语义：

- 用户名重复：`ErrorCode::AlreadyExists`。
- 用户不存在：`ErrorCode::NotFound`。

## 2. 本 Step 新增 / 修改文件

新增：

```text
include/liteim/storage/UserDao.hpp
include/liteim/storage/AuthDao.hpp
src/storage/UserDao.cpp
src/storage/AuthDao.cpp
tests/storage/user_dao_test.cpp
tutorials/step25_user_dao.md
```

同时更新：

```text
include/liteim/base/ErrorCode.hpp
src/base/ErrorCode.cpp
include/liteim/storage/MySqlConnection.hpp
src/storage/MySqlConnection.cpp
src/storage/CMakeLists.txt
tests/CMakeLists.txt
README.md
task_plan.md
findings.md
progress.md
```

`PreparedStatement::lastErrorNumber()` 是本 Step 为 DAO 错误转换补的能力。

## 3. UserDao.hpp 接口说明

```cpp
class UserDao {
public:
    explicit UserDao(MySqlPool& pool,
                     std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status createUser(const CreateUserRequest& request, UserRecord& created_user);
    Status findUserByUsername(const std::string& username, UserRecord& user);
    Status findUserById(std::uint64_t user_id, UserRecord& user);
};
```

### 构造函数

`UserDao(MySqlPool& pool, acquire_timeout)` 保存连接池指针和 acquire 超时时间。

所有权边界：

- `UserDao` 不拥有 `MySqlPool`。
- 调用者必须保证 pool 生命周期长于 DAO。
- DAO 方法内部临时借连接，方法返回后自动归还。

线程边界：

- DAO 方法会阻塞等待 MySQL 连接和 SQL 执行。
- 只能在 business `ThreadPool` 使用。
- 不能在 Reactor I/O loop 直接调用。

关键成员变量：

- `MySqlPool* pool_`：非 owning 指针。
- `std::chrono::milliseconds acquire_timeout_`：每次 DAO 方法借连接的最大等待时间。

### `createUser()`

```cpp
Status createUser(const CreateUserRequest& request, UserRecord& created_user);
```

流程：

1. 取当前毫秒时间。
2. 从 pool 借连接。
3. prepared insert 到 `users`：
   - `username`
   - `password_hash`
   - `password_salt`
   - `nickname`
   - `created_at_ms`
   - `updated_at_ms`
4. 检查 affected rows 必须是 1。
5. 再调用 `findUserByUsername()` 查回完整 `UserRecord`。

失败语义：

- pool acquire 失败：返回连接池错误。
- prepared statement 失败：返回 MySQL 错误。
- username 唯一约束冲突：通过 `lastErrorNumber() == 1062` 转成 `AlreadyExists`。
- affected rows 不是 1：返回 `InternalError`。

为什么插入后再查：

MySQL 自增 id 和 created 字段最终以数据库为准，查回可以得到完整记录，后续 service 不需要猜测。

### `findUserByUsername()`

```cpp
Status findUserByUsername(const std::string& username, UserRecord& user);
```

使用 prepared select：

```sql
SELECT user_id, username, password_hash, password_salt, nickname, created_at_ms
FROM users
WHERE username = ?
LIMIT 1
```

失败语义：

- 没查到：`NotFound`。
- 查到多行：`InternalError`，因为 username 应该唯一。
- 行格式不符合预期：`InternalError`。

### `findUserById()`

```cpp
Status findUserById(std::uint64_t user_id, UserRecord& user);
```

使用 `user_id` 查询 users 表。

边界：

- `user_id` 要能安全绑定到 MySQL signed `int64`。
- 不存在用户返回 `NotFound`。
- 输出参数只在 ok 时有效。

### private helper

`.cpp` 内部有几类 helper：

- `requiredValue()`：检查查询列存在且不是 NULL。
- `parseUint64()` / `parseInt64()`：把 MySQL 字符串字段解析成整数。
- `rowToUserRecord()`：把一行转换成 `UserRecord`。
- `bindUserId()`：检查 `uint64` 是否超过 signed bind 范围。
- `querySingleUser()`：统一处理 0 行、多行和正常单行。

这些 helper 不暴露在头文件里，因为它们只是 users 表 DAO 的内部解析逻辑。

## 4. AuthDao.hpp 接口说明

```cpp
class AuthDao {
public:
    explicit AuthDao(MySqlPool& pool,
                     std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status usernameExists(const std::string& username, bool& exists);
};
```

`AuthDao` 当前只提供注册 / 登录前需要的轻量查询。

### `usernameExists()`

```cpp
Status usernameExists(const std::string& username, bool& exists);
```

流程：

1. 先把 `exists` 置为 false。
2. 从 pool 借连接。
3. prepared select `SELECT user_id FROM users WHERE username = ? LIMIT 1`。
4. 如果结果行非空，`exists = true`。

失败语义：

- 查询失败返回 `Status`。
- 用户不存在不是错误，而是 `exists=false`。

为什么单独有 `AuthDao`：

`UserDao` 面向完整用户记录，`AuthDao` 面向认证流程的小查询。第一版只放 `usernameExists()`，后续 AuthService 需要更多认证专用查询时再扩展。

## 5. 相关接口补充说明

### `ErrorCode::AlreadyExists`

Step 25 新增 `AlreadyExists`，用于表达唯一约束冲突。用户名重复不是普通 `IoError`，上层 AuthService 需要把它转换成“用户名已存在”的业务响应。

### `PreparedStatement::lastErrorNumber()`

`lastErrorNumber()` 返回 `mysql_stmt_errno()`。

`UserDao::createUser()` 在 insert 失败后检查：

```text
1062 -> AlreadyExists
其他 -> 保留原 MySQL 错误
```

这样 DAO 不需要解析错误字符串。

## UserDao / AuthDao 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

`UserDao` / `AuthDao` 位于后续 AuthService 下面。

典型场景：

- 注册：检查 username 是否已存在，插入新用户。
- 登录：按 username 查用户，AuthService 校验密码 hash。
- 好友/群组/消息 DAO：通过用户 id 建外键关系。

### 2. 上下层调用连接

```text
Login/Register Packet
    -> Session 解包
    -> ThreadPool 业务任务
    -> AuthService
    -> AuthDao / UserDao
    -> MySqlPool
    -> MySqlConnection / PreparedStatement
    -> users table
```

DAO 不知道 Packet 格式，也不发送响应；响应由 AuthService 回到网络线程后发送。

### 3. 整体运行链路

注册链路：

1. AuthService 收到注册请求。
2. 调用 `AuthDao::usernameExists()`。
3. 如果存在，直接返回业务错误。
4. 如果不存在，AuthService 生成 password hash 和 salt。
5. 调用 `UserDao::createUser()`。
6. DAO insert users 表。
7. DAO 查回完整 `UserRecord`。
8. AuthService 组装注册成功响应。

登录链路：

1. AuthService 收到登录请求。
2. 调用 `UserDao::findUserByUsername()`。
3. 不存在返回登录失败。
4. 存在则 AuthService 校验密码。
5. 后续 Step 再写 Redis 在线状态。

### 4. 自身内部运行流程

`createUser()` 内部流程：

```text
now_ms
    -> pool.acquire()
    -> prepare INSERT users
    -> bind username/hash/salt/nickname/created/updated
    -> executeUpdate()
    -> duplicate key 1062 -> AlreadyExists
    -> findUserByUsername()
```

`findUserByUsername()` / `findUserById()` 内部流程：

```text
pool.acquire()
    -> prepare SELECT
    -> bind parameter
    -> executeQuery()
    -> querySingleUser()
    -> rowToUserRecord()
```

`usernameExists()` 内部流程：

```text
exists = false
    -> pool.acquire()
    -> prepare SELECT user_id
    -> bind username
    -> executeQuery()
    -> exists = rows not empty
```

### 5. 小例子和边界

小例子：

```cpp
CreateUserRequest request;
request.username = "alice";
request.password_hash = "...";
request.password_salt = "...";
request.nickname = "Alice";

UserRecord created;
const auto status = user_dao.createUser(request, created);
if (!status.isOk()) {
    return status;
}
```

边界：

- DAO 不计算密码 hash。
- DAO 不校验密码。
- DAO 不写 Redis。
- `NotFound` 表示用户不存在，不是空对象。
- `AlreadyExists` 表示唯一约束冲突。
- 所有 MySQL 调用都应在 business 线程执行。

## 后续实现 / 关键设计说明

Step 25 只覆盖 users 表。它不实现：

- MessageDao。
- FriendDao。
- GroupDao。
- AuthService。
- Redis 登录失败限制。
- 登录成功后的在线状态。

这样可以先把用户表访问、唯一约束错误和基础查询语义测试稳定，再进入消息和关系表。

## 测试设计

测试覆盖：

- `UserDao.hpp` / `AuthDao.hpp` 头文件自包含。
- 创建用户成功。
- 重复用户名返回 `AlreadyExists`。
- 按 username 查询成功。
- 按 user_id 查询成功。
- 查询不存在用户返回 `NotFound`。
- `usernameExists()` 对存在和不存在用户返回正确 bool。

测试使用 `step25_` 前缀用户，并在 SetUp/TearDown 清理，避免污染 seed 用户。

## 验证命令

```bash
cmake --build build
docker compose -f docker/docker-compose.yml ps
ctest --test-dir build -R "UserDao|AuthDao" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 面试时怎么讲

可以这样说：

> Step 25 在 MySQL 连接池之上实现 users 表 DAO。`UserDao` 负责创建用户和按 username/id 查询，`AuthDao` 当前只负责 username 是否存在。DAO 只做数据访问，不做密码 hash 或登录判断。用户名唯一冲突通过 `mysql_stmt_errno() == 1062` 转成 `ErrorCode::AlreadyExists`，不存在用户返回 `NotFound`。所有 DAO 方法临时从 `MySqlPool` 借连接，用 prepared statement 执行 SQL，方法返回时 RAII guard 自动归还连接。

## 提交信息

```text
feat(storage): implement user dao
```
