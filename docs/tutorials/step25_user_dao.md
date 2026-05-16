# Step 25：UserDao 和 AuthDao

## 0. 本 Step 结论

- 目标：Step 25 的目标是在 MySQL wrapper 和连接池之上实现 users 表 DAO。
- 前置依赖：依赖 Step 0-24 已建立的工程、协议或运行时基础。
- 主要交付：`UserDao 和 AuthDao` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

Step 25 的目标是在 MySQL wrapper 和连接池之上实现 users 表 DAO。

到 Step 24 为止，LiteIM 已经能从业务线程安全借出 MySQL 连接，但还没有任何业务表访问代码。Step 25 解决的问题是：

```text
注册 / 登录相关服务如何通过清晰的 DAO 访问 users 表？
```

答案是先实现 `UserDao` 和 `AuthDao`，只做数据访问，不做业务判断。

### 概念

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

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `UserDao 和 AuthDao` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/storage/UserDao.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/storage/AuthDao.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/UserDao.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/AuthDao.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/storage/user_dao_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/tutorials/step25_user_dao.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/base/ErrorCode.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/base/ErrorCode.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/storage/MySqlConnection.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/MySqlConnection.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

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

- `user_id` 按 MySQL `BIGINT UNSIGNED` 绑定。
- 不存在用户返回 `NotFound`。
- 输出参数只在 ok 时有效。

### private helper

`.cpp` 内部有几类 helper：

- `requiredValue()`：检查查询列存在且不是 NULL。
- `parseUint64()` / `parseInt64()`：把 MySQL 字符串字段解析成整数。
- `rowToUserRecord()`：把一行转换成 `UserRecord`。
- `bindUserId()`：使用 `PreparedStatement::bindUInt64()` 绑定用户 id。
- `querySingleUser()`：统一处理 0 行、多行和正常单行。

这些 helper 不暴露在头文件里，因为它们只是 users 表 DAO 的内部解析逻辑。

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

## 5. 运行流程

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

### 5. 该项目代码在实际应用中的具体数据例子

注册接口未来收到 `username=charlie` 时，AuthService 先用 `AuthDao::usernameExists()` 判断唯一性，再调用 `UserDao::createUser()` 写入 `users`。登录 `alice` 时，`findUserByUsername("alice", user)` 返回 `user_id=1001`、`nickname=Alice`、`password_hash=dev_hash_alice`；好友列表等公开场景则不应把 hash/salt 继续传出去。

## 6. 关键实现点

### 相关接口补充说明

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

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `UserDao 和 AuthDao` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

测试覆盖：

- `UserDao.hpp` / `AuthDao.hpp` 头文件自包含。
- 创建用户成功。
- 重复用户名返回 `AlreadyExists`。
- 按 username 查询成功。
- 按 user_id 查询成功。
- 查询不存在用户返回 `NotFound`。
- `usernameExists()` 对存在和不存在用户返回正确 bool。

测试使用 `step25_` 前缀用户，并在 SetUp/TearDown 清理，避免污染 seed 用户。

## 8. 验证命令

```bash
cmake --build build
docker compose -f docker/docker-compose.yml ps
ctest --test-dir build -R "UserDao|AuthDao" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

### 一句话

Step 25 在 MySQL 连接池之上实现 users 表 DAO。

### 展开说

可以这样说：

> Step 25 在 MySQL 连接池之上实现 users 表 DAO。`UserDao` 负责创建用户和按 username/id 查询，`AuthDao` 当前只负责 username 是否存在。DAO 只做数据访问，不做密码 hash 或登录判断。用户名唯一冲突通过 `mysql_stmt_errno() == 1062` 转成 `ErrorCode::AlreadyExists`，不存在用户返回 `NotFound`。所有 DAO 方法临时从 `MySqlPool` 借连接，用 prepared statement 执行 SQL，方法返回时 RAII guard 自动归还连接。

### 容易被追问

- 为什么用户名重复要转成 AlreadyExists？
- 为什么 find 不存在返回 NotFound？

## 10. 面试常见追问

### Q1：为什么用户名重复要转成 AlreadyExists？

上层注册服务需要区分“用户名已存在”和普通数据库错误。结构化错误比解析 MySQL 文本错误稳定。

### Q2：为什么 find 不存在返回 NotFound？

不存在是明确业务结果，不能返回空 UserRecord 加 ok。否则上层可能把 user_id=0 当成真实用户继续处理。
