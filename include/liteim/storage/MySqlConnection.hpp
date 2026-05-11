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
};

class MySqlQueryResult {
public:
    void clear();

    const std::vector<std::string>& columns() const noexcept;
    const std::vector<MySqlRow>& rows() const noexcept;

private:
    friend class PreparedStatement;

    std::vector<std::string> columns_;
    std::vector<MySqlRow> rows_;
};

class MySqlConnection {
public:
    MySqlConnection() = default;
    ~MySqlConnection();

    MySqlConnection(const MySqlConnection&) = delete;
    MySqlConnection& operator=(const MySqlConnection&) = delete;

    MySqlConnection(MySqlConnection&& other) noexcept;
    MySqlConnection& operator=(MySqlConnection&& other) noexcept;

    Status connect(const MySqlConfig& config);
    Status ping();
    Status executeSimple(const std::string& sql);
    void close() noexcept;

    bool isConnected() const noexcept;

private:
    friend class PreparedStatement;

    MYSQL* nativeHandle() noexcept;

    MYSQL* handle_{nullptr};
    bool connected_{false};
};

struct PreparedStatementImpl;

class PreparedStatement {
public:
    explicit PreparedStatement(MySqlConnection& connection);
    ~PreparedStatement();

    PreparedStatement(const PreparedStatement&) = delete;
    PreparedStatement& operator=(const PreparedStatement&) = delete;

    PreparedStatement(PreparedStatement&& other) noexcept;
    PreparedStatement& operator=(PreparedStatement&& other) noexcept;

    Status prepare(const std::string& sql);
    Status bindInt64(std::size_t index, std::int64_t value);
    Status bindString(std::size_t index, const std::string& value);
    Status executeUpdate(std::uint64_t& affected_rows);
    Status executeQuery(MySqlQueryResult& result);
    unsigned int lastErrorNumber() const noexcept;
    void close() noexcept;

private:
    Status bindParameters();
    bool isPrepared() const noexcept;

    MySqlConnection* connection_{nullptr};
    std::unique_ptr<PreparedStatementImpl> impl_;
};

} // namespace liteim
