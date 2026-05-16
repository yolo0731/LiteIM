# Step 23：MySqlConnection 和 PreparedStatement

## 0. 本 Step 结论

- 目标：Step 23 的目标是封装 MySQL C API，让后续 DAO 不直接操作 MYSQL、MYSQL_STMT 和 MYSQL_BIND。
- 前置依赖：依赖 Step 0-22 已建立的工程、协议或运行时基础。
- 主要交付：`MySqlConnection 和 PreparedStatement` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

Step 23 的目标是封装 MySQL C API，让后续 DAO 不直接操作 `MYSQL*`、`MYSQL_STMT*` 和 `MYSQL_BIND`。

到 Step 22 为止，LiteIM 已经有本地 MySQL schema 和 seed 数据，但 C++ 代码还不能安全地连接数据库。Step 23 解决的问题是：

```text
如何用 C++17 写一个小而清楚的 MySQL prepared statement 封装？
```

答案是引入 `MySqlConnection`、`PreparedStatement` 和 `MySqlQueryResult`。

### 概念

MySQL C API 是 C 风格接口：

- 连接用 `MYSQL*`。
- 预处理语句用 `MYSQL_STMT*`。
- 参数和结果绑定用 `MYSQL_BIND` 数组。
- 错误信息从连接或 statement 里读取。

如果 DAO 每个函数都直接写这些细节，会有几个问题：

- 资源释放容易漏。
- 参数绑定容易写错。
- SQL 注入风险更难审查。
- 错误语义不统一。
- 后续连接池难以复用连接对象。

Step 23 把这些 C API 细节收进两个 RAII 类：

```text
MySqlConnection
    owns MYSQL*
    connect / ping / close / executeSimple

PreparedStatement
    owns MYSQL_STMT*
    prepare / bind / executeUpdate / executeQuery
```

本 Step 只封装单连接和单 statement，不实现连接池、不实现 DAO、不接入业务服务。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `MySqlConnection 和 PreparedStatement` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/storage/MySqlConnection.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/MySqlConnection.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/storage/mysql_connection_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/tutorials/step23_mysql_connection.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/storage/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

### `MySqlRow`

```cpp
struct MySqlRow {
    std::vector<std::optional<std::string>> values;
};
```

查询结果的一行。每个字段都用 `std::optional<std::string>` 表达：

- 有值：字段不是 SQL NULL。
- `std::nullopt`：字段是 SQL NULL。

当前统一把 MySQL 结果转成字符串，再由 DAO 按字段语义解析成 `uint64`、`int64` 或业务字符串。这样 wrapper 保持简单，不在底层绑定业务类型。

### `MySqlQueryResult`

```cpp
class MySqlQueryResult {
public:
    void clear();
    const std::vector<std::string>& columns() const noexcept;
    const std::vector<MySqlRow>& rows() const noexcept;
};
```

`MySqlQueryResult` 是 `executeQuery()` 的输出对象。

- `clear()` 清空列名和行数据，`executeQuery()` 开始时会先调用。
- `columns()` 返回 MySQL result metadata 里的列名。
- `rows()` 返回所有已拉取行。

关键成员变量：

- `columns_`：列名。
- `rows_`：完整结果集。

`PreparedStatement` 是 friend，因为只有 statement 执行查询时能填充结果内部字段。外部只能读，不能绕过接口写。

### `MySqlConnection`

```cpp
class MySqlConnection {
public:
    MySqlConnection() = default;
    ~MySqlConnection();
    MySqlConnection(MySqlConnection&& other) noexcept;
    MySqlConnection& operator=(MySqlConnection&& other) noexcept;

    Status connect(const MySqlConfig& config);
    Status ping();
    Status executeSimple(const std::string& sql);
    void close() noexcept;
    bool isConnected() const noexcept;
};
```

所有权：

- `MySqlConnection` 拥有一个 `MYSQL* handle_`。
- 析构时自动 `close()`。
- 禁止拷贝，允许移动。
- 移动后旧对象不再拥有连接。

`connect(config)`：

- 先关闭旧连接。
- 调用 `mysql_init()`。
- 设置字符集 `utf8mb4`。
- 调用 `mysql_real_connect()` 连接 Step 22 的 MySQL。
- 失败时返回 `Status::error(...)` 并清理 handle。

`ping()`：

- 检查连接是否存在。
- 调用 `mysql_ping()`。
- 后续连接池借出连接前会用它判断连接是否失效。

`executeSimple(sql)`：

- 走 `mysql_query()`。
- 用于 `START TRANSACTION`、`COMMIT`、`ROLLBACK` 这类事务控制语句。
- 如果 SQL 返回结果集，会立即 `mysql_store_result()` 并释放，避免连接上残留未消费结果。
- 普通用户输入 SQL 仍然应该走 `PreparedStatement`，不能通过它拼接。

关键成员变量：

- `MYSQL* handle_{nullptr}`：MySQL C API 连接资源。
- `bool connected_{false}`：表示连接是否成功建立。

private helper：

- `nativeHandle()` 只给 `PreparedStatement` 使用，避免 DAO 直接拿 `MYSQL*`。

线程边界：

- 单个 `MySqlConnection` 不加锁。
- 一个连接一次只应在一个业务线程使用。
- Reactor I/O 线程不能直接调用 MySQL。

### `PreparedStatement`

```cpp
class PreparedStatement {
public:
    explicit PreparedStatement(MySqlConnection& connection);
    ~PreparedStatement();
    PreparedStatement(PreparedStatement&& other) noexcept;
    PreparedStatement& operator=(PreparedStatement&& other) noexcept;

    Status prepare(const std::string& sql);
    Status bindInt64(std::size_t index, std::int64_t value);
    Status bindUInt64(std::size_t index, std::uint64_t value);
    Status bindString(std::size_t index, const std::string& value);
    Status executeUpdate(std::uint64_t& affected_rows);
    Status executeQuery(MySqlQueryResult& result);
    unsigned int lastErrorNumber() const noexcept;
    void close() noexcept;
};
```

所有权：

- `PreparedStatement` 拥有一个 `MYSQL_STMT*`，藏在 `PreparedStatementImpl` 中。
- 析构时自动 `close()`。
- 禁止拷贝，允许移动。
- 它不拥有 `MySqlConnection`，只保存 `MySqlConnection* connection_`，所以 statement 生命周期不能超过连接。

`prepare(sql)`：

- 关闭旧 statement。
- 检查连接已建立。
- 调用 `mysql_stmt_init()` 和 `mysql_stmt_prepare()`。
- 根据 `mysql_stmt_param_count()` 初始化参数绑定数组。

`bindInt64(index, value)`：

- `index` 是从 0 开始的参数位置。
- 绑定为 `MYSQL_TYPE_LONGLONG`。
- 下标越界或未 prepare 返回 `InvalidArgument`。

`bindUInt64(index, value)`：

- 同样绑定为 `MYSQL_TYPE_LONGLONG`，但把 `MYSQL_BIND::is_unsigned` 设为 true。
- 用于 `BIGINT UNSIGNED` 主键和外键，例如 `user_id`、`message_id`、`group_id`。
- 这样 DAO 不需要先把 `std::uint64_t` 强行限制到 `INT64_MAX`。

`bindString(index, value)`：

- 把字符串存进 `parameter_values`，再让 `MYSQL_BIND` 指向内部字符串数据。
- 这样 `executeUpdate()` / `executeQuery()` 前参数内存仍然有效。

`executeUpdate(affected_rows)`：

- 先检查所有参数都已绑定。
- 调用 `mysql_stmt_bind_param()`。
- 调用 `mysql_stmt_execute()`。
- 用 `mysql_stmt_affected_rows()` 输出影响行数。
- 适合 `INSERT`、`UPDATE`、`DELETE`。

`executeQuery(result)`：

- 清空输出结果。
- 绑定参数并执行。
- 读取 result metadata 和列名。
- 把每列先按字符串缓冲读取。
- 对 SQL NULL 写入 `std::nullopt`。
- 对超过初始缓冲的列用 `mysql_stmt_fetch_column()` 拉完整字段。

`lastErrorNumber()`：

- 暴露 `mysql_stmt_errno()`。
- 后续 DAO 用它识别 MySQL duplicate key errno `1062`，再转换成 `ErrorCode::AlreadyExists`。

关键成员和 helper：

- `connection_`：非 owning 指针，指向外部连接。
- `impl_`：隐藏 `MYSQL_STMT*`、参数 `MYSQL_BIND`、参数值缓存。
- `bindParameters()`：执行前确认每个参数都已绑定。
- `isPrepared()`：判断 statement 是否可执行。

失败语义：

- 未连接、未 prepare、参数缺失、参数下标越界返回 `InvalidArgument`。
- MySQL C API 错误返回 `IoError` 并携带 MySQL error text。
- 查询语句没有结果集时，`executeQuery()` 返回 `InvalidArgument`。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

它们位于 MySQL DAO 之下、MySQL C API 之上。后续 DAO 不直接 include `<mysql.h>`，而是：

```text
DAO
    -> MySqlPool::acquire()
    -> MySqlConnection
    -> PreparedStatement
    -> MySQL C API
```

本 Step 之后，用户 DAO、消息 DAO、好友群组 DAO 都会复用这套封装。

### 2. 上下层调用连接

```text
AuthService / ChatService
    -> business ThreadPool
    -> UserDao / MessageDao / FriendDao / GroupDao
    -> MySqlPool
    -> MySqlConnection
    -> PreparedStatement
    -> mysqlclient
    -> Docker MySQL
```

`MySqlConnection` 不知道 `Session`，不回调网络层，也不做业务判断。

### 3. 整体运行链路

以查询用户为例：

1. DAO 从连接池拿到 `MySqlConnection`。
2. 构造 `PreparedStatement statement(connection)`。
3. `prepare("SELECT ... WHERE username = ?")`。
4. `bindString(0, username)`。
5. `executeQuery(result)`。
6. DAO 检查 `result.rows()`。
7. DAO 把字符串字段解析成 `UserRecord`。
8. `ConnectionGuard` 析构后归还连接。

### 4. 自身内部运行流程

连接流程：

```text
connect(config)
    -> close old handle
    -> mysql_init
    -> set utf8mb4
    -> mysql_real_connect
    -> connected_ = true
```

statement 更新流程：

```text
prepare SQL
    -> bind every parameter
    -> bindParameters()
    -> mysql_stmt_execute
    -> mysql_stmt_affected_rows
    -> mysql_stmt_free_result
```

statement 查询流程：

```text
prepare SQL
    -> bind every parameter
    -> execute
    -> fetch metadata
    -> bind result columns
    -> fetch rows
    -> convert each column to optional<string>
    -> free result
```

### 5. 该项目代码在实际应用中的具体数据例子

测试连接到 Docker MySQL `127.0.0.1:33060/liteim` 后，可以用 prepared statement 查询 `SELECT user_id FROM users WHERE username=?`，绑定 `alice` 得到 `1001`。插入消息时用 `bindUInt64()` 绑定 `conversation_id=10011002`、`sender_id=1001`、`receiver_id=1002`，用 `bindString()` 绑定 `hello bob`，避免把用户输入拼进 SQL 字符串。

## 6. 关键实现点

- 保持模块职责单一。
- 失败时返回清晰错误，不吞掉异常状态。
- 不跨越本 Step 边界提前实现后续业务。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `MySqlConnection 和 PreparedStatement` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

测试分两类：

- 头文件 / 不可用 MySQL 行为：验证 header 自包含、连接失败返回错误而不是崩溃。
- Docker MySQL 集成测试：验证真实连接、`ping()`、prepared select、insert/query、错误 SQL 和特殊字符绑定。

特殊字符绑定测试的目的不是证明 MySQL 永远安全，而是证明当前路径不拼接用户输入 SQL，参数通过 `MYSQL_BIND` 传入。

## 8. 验证命令

```bash
cmake --build build
docker compose -f docker/docker-compose.yml ps
ctest --test-dir build -R "MySqlConnection" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
docker compose -f docker/docker-compose.yml up -d
```

## 9. 面试表达

### 一句话

Step 23 把 MySQL C API 包成 RAII 对象。

### 展开说

可以这样说：

> Step 23 把 MySQL C API 包成 RAII 对象。`MySqlConnection` 拥有 `MYSQL*`，负责连接、ping、关闭和简单事务控制语句；`PreparedStatement` 拥有 `MYSQL_STMT*`，负责 prepare、signed/unsigned 64-bit 参数绑定、字符串绑定、更新和查询。查询结果统一输出成 `MySqlQueryResult`，字段用 `optional<string>` 表达 SQL NULL。DAO 以后只依赖这些 C++ 封装，不直接操作 MySQL C 指针，也不拼接用户输入 SQL。

### 容易被追问

- 为什么封装 PreparedStatement？
- 为什么 MySqlConnection 不跨线程共享？

## 10. 面试常见追问

### Q1：为什么封装 PreparedStatement？

DAO 层需要统一参数绑定、执行和结果读取，避免散落 `MYSQL_STMT*` 生命周期，也避免拼接 SQL 带来的注入风险。

### Q2：为什么 MySqlConnection 不跨线程共享？

连接对象内部状态和 prepared statement 生命周期都不适合并发乱用。后续由 MySqlPool 控制“一个借出的连接同一时间只给一个业务任务使用”。
