# Step 23：MySqlConnection 和 PreparedStatement

## 1. 概念

Step 22 已经用 Docker Compose 启动了本地 MySQL，并初始化了 LiteIM 的表和 seed 数据。Step 23 开始把 C++ 代码接到真实 MySQL，但只做最底层封装：

- `MySqlConnection`：RAII 持有一个 MySQL C API 连接。
- `PreparedStatement`：封装 `MYSQL_STMT`，负责预编译 SQL、绑定参数、执行更新和查询。
- `MySqlQueryResult`：用输出参数承载查询结果。

本 Step 不做连接池、不做 DAO、不接入业务服务。网络 I/O 线程仍然不能访问 MySQL；后续真正查询数据库时，MySQL 操作必须放到 business `ThreadPool`。

## 2. hpp 接口说明

### `MySqlQueryResult`

`MySqlQueryResult` 保存一次查询的列名和行数据：

- `columns()`：返回查询结果列名。
- `rows()`：返回所有行。
- `clear()`：清空结果，供 `executeQuery()` 重复写入。

每行是 `MySqlRow`，字段类型是 `std::optional<std::string>`。这样 SQL `NULL` 可以表达为 `std::nullopt`，非空字段统一先转成字符串。DAO 层后续再把字符串解析成 `UserRecord`、`MessageRecord` 等强类型结构。

### `MySqlConnection`

`MySqlConnection` 拥有一个 `MYSQL*`：

- `connect(const MySqlConfig&)`：用 `Config` 里的 host、port、user、password、database 连接 MySQL，并设置字符集为 `utf8mb4`。
- `ping()`：调用 `mysql_ping()` 检查连接是否仍然可用。
- `close()`：关闭连接，可重复调用。
- `isConnected()`：用于测试和上层判断连接状态。

对象不可拷贝、可移动。它不加锁，也不承诺跨线程共享；后续 Step 24 的连接池会负责把连接借给业务线程使用。

### `PreparedStatement`

`PreparedStatement` 绑定到一个已连接的 `MySqlConnection`：

- `prepare(sql)`：调用 `mysql_stmt_prepare()` 预编译 SQL。
- `bindInt64(index, value)`：绑定 `BIGINT` / 整数参数，index 从 0 开始。
- `bindString(index, value)`：绑定字符串参数，特殊字符不会拼进 SQL。
- `executeUpdate(affected_rows)`：执行 `INSERT` / `UPDATE` / `DELETE` / DDL，并输出 affected rows。
- `executeQuery(result)`：执行 `SELECT`，把列名和行数据写入 `MySqlQueryResult`。
- `close()`：关闭 statement，可重复调用。

失败语义仍然是 `Status`：MySQL API 调用失败时返回 `ErrorCode::IoError` 和底层错误信息；未连接、未 prepare、参数下标越界、缺少参数绑定等调用错误返回 `InvalidArgument`。

## 3. 运行流程

一次查询的典型流程：

```text
Config::defaults()
  -> MySqlConnection::connect()
  -> PreparedStatement::prepare("SELECT username FROM users WHERE user_id = ?")
  -> bindInt64(0, 1001)
  -> executeQuery(result)
  -> result.rows()[0].values[0] == "alice"
```

一次写入流程：

```text
PreparedStatement::prepare("INSERT INTO table (id, text_value) VALUES (?, ?)")
  -> bindInt64(0, 7)
  -> bindString(1, "quote ' OR 1=1 --")
  -> executeUpdate(affected_rows)
```

关键点是：用户输入只进入 `bind*()`，不会通过字符串拼接进入 SQL。即使字符串里有引号、反斜杠、换行、中文或类似注入片段，也只是普通字段值。

## 4. 测试

新增 `tests/storage/mysql_connection_test.cpp`：

- `MySqlConnectionTest.HeaderIsSelfContained`：确认新头文件可以直接使用。
- `MySqlIntegrationTest.ConnectsAndPingsLocalMySql`：连接 Docker MySQL 并 `ping()` 成功。
- `MySqlIntegrationTest.PreparedStatementExecutesSimpleSelect`：通过 prepared statement 查询 seed 用户 `alice`。
- `MySqlIntegrationTest.ExecuteUpdateAndQueryRoundTripSpecialCharacters`：创建临时表，插入并查询含引号、反斜杠、换行和中文的字符串，验证参数绑定不是 SQL 拼接。
- `MySqlIntegrationTest.InvalidSqlReturnsErrorStatus`：错误 SQL 返回失败 `Status` 和错误信息。

这些测试默认连接 `127.0.0.1:33060`，用户 `liteim`，密码 `6`。如果本地 Docker MySQL 没启动，测试会 skip；做 Step 23 验证时应该先启动 Docker 依赖。

## 5. 验证命令

```bash
docker compose -f docker/docker-compose.yml up -d --wait
cmake --build build
ctest --test-dir build -R "MySqlConnectionTest|MySqlIntegrationTest|StorageInterfaceTest" --output-on-failure
ctest --test-dir build --output-on-failure
```

本 Step 不改变 `TcpServer` / `Session` / `ThreadPool` 的运行时行为。后续 Step 24 才实现 `MySqlPool` 和 `ConnectionGuard`。
