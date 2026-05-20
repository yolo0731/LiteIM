# Step 25：UserDao

## 0. 本 Step 结论

Step 25 在 MySQL 连接池之上实现 `UserDao`，把 `users` 表的创建用户、按 username 查询、按 user_id 查询封装成可测试 DAO。当前第一版收口后，旧的认证专用 `AuthDao` 已清理删除；认证流程直接通过 `UserDao::findUserByUsername()` 和 `UserDao::createUser()` 完成。

## 1. 为什么需要这个 Step

前面 Step 23/24 已经有 `MySqlConnection`、`PreparedStatement`、`MySqlPool` 和 `ConnectionGuard`，但业务层不能到处手写 users 表 SQL。需要一个小而稳定的 DAO：

- 注册时写入 username、password hash、salt、nickname。
- 登录时按 username 查出完整用户记录。
- 好友、群、历史权限校验时按 user_id 查用户是否存在。
- 把 MySQL duplicate key 这类细节转换成项目统一的 `Status` / `ErrorCode`。

## 2. 本 Step 边界

本 Step 只负责 users 表 DAO，不做密码哈希、登录限流、Session 绑定、Redis 在线状态或业务协议 handler。

当前代码边界：

- `UserDao` 是 users 表唯一 DAO。
- `AuthService` 负责认证业务判断。
- `MySqlStorage` 通过组合 `UserDao` 实现 `IStorage` 的用户接口。
- 旧 `AuthDao` 不再参与当前代码、CMake 或测试。

## 3. 文件变化

| 文件 | 当前状态 | 作用 |
| --- | --- | --- |
| `include/liteim/storage/UserDao.hpp` | 保留 | 声明 users 表 DAO 接口 |
| `src/storage/UserDao.cpp` | 保留 | 实现 create / find SQL |
| `src/storage/CMakeLists.txt` | 更新 | 只编译当前 storage DAO |
| `tests/storage/user_dao_test.cpp` | 保留 | Docker MySQL 集成测试 |
| `include/liteim/storage/AuthDao.hpp` | 已删除 | 第一版收口清理旧认证小 DAO |
| `src/storage/AuthDao.cpp` | 已删除 | 第一版收口清理旧认证小 DAO |

## 4. 核心接口与契约

`CreateUserRequest` 是写入 users 表前的业务数据：

```cpp
struct CreateUserRequest {
    std::string username;
    std::string password_hash;
    std::string password_salt;
    std::string nickname;
};
```

`UserRecord` 是数据库查回的完整用户记录：

```cpp
struct UserRecord {
    std::uint64_t user_id{0};
    std::string username;
    std::string password_hash;
    std::string password_salt;
    std::string nickname;
    std::int64_t created_at_ms{0};
};
```

`UserDao` 当前接口：

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

关键契约：

- 参数非法返回 `InvalidArgument`。
- username 唯一冲突返回 `AlreadyExists`。
- 查询不到用户返回 `NotFound`。
- 成功时填充 `UserRecord`，失败时不让调用方误用半成品记录。

## 5. 运行流程

注册路径：

```text
AuthService::handleRegister()
    -> hashPassword(password)
    -> UserDao::createUser()
    -> INSERT users(...)
    -> SELECT LAST_INSERT_ID() 对应用户
    -> RegisterResponse(user_id, username, nickname)
```

登录路径：

```text
AuthService::handleLogin()
    -> UserDao::findUserByUsername(username)
    -> 校验 PBKDF2 hash
    -> OnlineService::bindUser()
    -> LoginResponse(user_id, username, nickname, session_id)
```

用户存在性校验：

```text
ChatService / FriendService / GroupService
    -> IStorage::findUserById()
    -> MySqlStorage::findUserById()
    -> UserDao::findUserById()
```

## 6. 关键实现点

`UserDao` 不持有连接，只持有 `MySqlPool*` 和 acquire timeout。每个方法临时借连接，方法返回时 `ConnectionGuard` 自动归还连接。

`createUser()` 使用 prepared statement 插入用户，避免字符串拼接 SQL。插入成功后再查回完整记录，确保返回的 `user_id` 和 `created_at_ms` 来自数据库事实。

duplicate username 不靠业务层先查再插，而是依赖数据库唯一索引兜底。这样两个并发注册同一个 username 时，MySQL 约束仍能保证只有一条成功。

`findUserByUsername()` 和 `findUserById()` 共用行解析逻辑，避免不同查询返回字段漂移。

## 7. 测试设计

`tests/storage/user_dao_test.cpp` 连接 Docker MySQL，覆盖：

- 头文件自包含。
- 创建用户后返回完整字段。
- 单连接池也能创建用户。
- 重复 username 返回 `AlreadyExists`。
- 按 username 查回创建用户。
- 按 user_id 查回创建用户。
- 查询不存在用户返回 `NotFound`。

测试数据使用 `step25_` 前缀，`SetUp()` / `TearDown()` 都清理，避免污染 seed 用户。

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "UserDao" --output-on-failure
```

如果本地 MySQL 没启动，先执行：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
```

## 9. 面试表达

可以这样讲：

> Step 25 把 users 表访问收敛到 `UserDao`。业务层不直接拼 SQL，而是通过 DAO 创建用户、按 username/id 查询用户。DAO 使用连接池和 prepared statement，重复 username 由 MySQL 唯一约束兜底并转换成 `AlreadyExists`，不存在用户返回 `NotFound`。认证逻辑不放在 DAO，密码哈希、限流、Session 绑定都留在 `AuthService`。

这个表达重点是：DAO 只做数据访问，业务语义留给 service。

## 10. 面试常见追问

### 为什么不先查 username 是否存在，再插入？

先查再插有并发窗口。两个请求可能都查到不存在，然后同时插入。正确兜底是数据库唯一约束，DAO 把 duplicate key 转成 `AlreadyExists`。

### 为什么 UserDao 返回完整 UserRecord？

注册和登录都需要数据库生成的 `user_id`、创建时间以及 hash/salt 等字段。返回完整记录能让 service 只依赖数据库事实，不猜测自增 id 或时间。

### 为什么不把密码校验放进 UserDao？

DAO 的责任是 SQL 数据访问。密码校验属于认证业务，放在 `AuthService` 更清晰，也方便以后调整 PBKDF2 参数、token、限流或审计逻辑。

### 删除旧 AuthDao 会不会影响登录？

不会。当前登录需要的是按 username 查完整用户记录，`UserDao::findUserByUsername()` 已经覆盖这个需求。额外保留只含 `usernameExists()` 的认证 DAO 会让 storage 模块边界变乱，所以第一版收口时删除。
