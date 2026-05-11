#pragma once

#include "liteim/base/Config.hpp"
#include "liteim/base/Status.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct MYSQL;

namespace liteim {

struct MySqlRow {
    std::vector<std::optional<std::string>> values;
    // 用于存储每列的值，std::nullopt表示NULL值，用optional是为了区分NULL和空字符串
};

class MySqlQueryResult { // 查询结果类
public:
    void clear();

    const std::vector<std::string>& columns() const noexcept; // 获取列名列表
    const std::vector<MySqlRow>& rows() const noexcept;       // 获取查询结果的行数据

private:
    friend class PreparedStatement;

    std::vector<std::string> columns_; // 存储列名列表
    std::vector<MySqlRow> rows_;       // 存储查询结果每一行的数据
};

class MySqlConnection { // MySQL连接类
public:
    MySqlConnection() = default;
    ~MySqlConnection();

    MySqlConnection(const MySqlConnection&) = delete;
    MySqlConnection& operator=(const MySqlConnection&) = delete;

    MySqlConnection(MySqlConnection&& other) noexcept;            // 移动构造函数
    MySqlConnection& operator=(MySqlConnection&& other) noexcept; // 移动赋值运算符

    Status connect(const MySqlConfig& config);    // 连接到MySQL数据库
    Status ping();                                // 检测连接是否可用
    Status executeSimple(const std::string& sql); // 执行简单的SQL语句（不带参数的查询或更新）
    void close() noexcept;                        // 关闭连接

    bool isConnected() const noexcept;

private:
    friend class PreparedStatement;

    MYSQL* nativeHandle() noexcept; // 获取MySQL连接的句柄供PreparedStatement使用

    MYSQL* handle_{nullptr}; // MySQL连接的原生句柄
    bool connected_{false};
};

struct PreparedStatementImpl;

class PreparedStatement { // 预处理语句类
public:
    explicit PreparedStatement(MySqlConnection& connection);
    ~PreparedStatement();

    PreparedStatement(const PreparedStatement&) = delete;
    PreparedStatement& operator=(const PreparedStatement&) = delete;

    PreparedStatement(PreparedStatement&& other) noexcept;
    PreparedStatement& operator=(PreparedStatement&& other) noexcept;

    Status prepare(const std::string& sql);                  // 准备SQL语句
    Status bindInt64(std::size_t index, std::int64_t value); // 绑定整数参数
    Status bindUInt64(std::size_t index, std::uint64_t value);
    Status bindString(std::size_t index, const std::string& value);
    Status executeUpdate(std::uint64_t& affected_rows); // 执行更新语句，返回受影响的行数
    Status executeQuery(MySqlQueryResult& result);      // 执行查询语句，返回查询结果
    unsigned int lastErrorNumber() const noexcept;      // 获取最后一次错误的错误码
    void close() noexcept;

private:
    Status bindParameters(); // 绑定所有参数到MySQL预处理语句
    bool isPrepared() const noexcept;

    MySqlConnection* connection_{nullptr}; // 指向关联的MySQL连接
    std::unique_ptr<PreparedStatementImpl> impl_;
    // 让 PreparedStatement 有地方长期保存预处理语句的内部状态并不暴露接口
};

} // namespace liteim
