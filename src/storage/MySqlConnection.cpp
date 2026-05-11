#include "liteim/storage/MySqlConnection.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include <mysql.h>

namespace liteim {
namespace {

constexpr const char* kUtf8Mb4 = "utf8mb4";

Status mysqlError(MYSQL* handle, const std::string& action) {
    return Status::error(ErrorCode::IoError, action + ": " + mysql_error(handle));
}

Status statementError(MYSQL_STMT* statement, const std::string& action) {
    return Status::error(ErrorCode::IoError, action + ": " + mysql_stmt_error(statement));
}

void resetBind(MYSQL_BIND& bind) {
    std::memset(&bind, 0, sizeof(bind));
}

struct ParameterValue {
    bool bound{false};
    bool is_null{false};
    std::int64_t int64_value{0};
    std::string string_value;
    unsigned long length{0};
};

struct ResultColumnBuffer {
    std::vector<char> buffer;
    unsigned long length{0};
    bool is_null{false};
    bool error{false};
};

Status fetchFullColumn(MYSQL_STMT* statement,
                       unsigned int column,
                       unsigned long length,
                       std::optional<std::string>& value) {
    std::vector<char> buffer(static_cast<std::size_t>(length) + 1U);
    unsigned long fetched_length = 0;
    bool is_null = false;
    bool error = false;

    MYSQL_BIND bind;
    resetBind(bind);
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = buffer.data();
    bind.buffer_length = static_cast<unsigned long>(buffer.size());
    bind.length = &fetched_length;
    bind.is_null = &is_null;
    bind.error = &error;

    if (mysql_stmt_fetch_column(statement, &bind, column, 0) != 0) {
        return statementError(statement, "mysql_stmt_fetch_column failed");
    }
    if (is_null) {
        value = std::nullopt;
        return Status::ok();
    }

    value = std::string(buffer.data(), fetched_length);
    return Status::ok();
}

} // namespace

struct PreparedStatementImpl {
    MYSQL_STMT* statement{nullptr};
    std::vector<MYSQL_BIND> parameters;
    std::vector<ParameterValue> parameter_values;
};

void MySqlQueryResult::clear() {
    columns_.clear();
    rows_.clear();
}

const std::vector<std::string>& MySqlQueryResult::columns() const noexcept {
    return columns_;
}

const std::vector<MySqlRow>& MySqlQueryResult::rows() const noexcept {
    return rows_;
}

MySqlConnection::~MySqlConnection() {
    close();
}

MySqlConnection::MySqlConnection(MySqlConnection&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      connected_(std::exchange(other.connected_, false)) {
}

MySqlConnection& MySqlConnection::operator=(MySqlConnection&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
        connected_ = std::exchange(other.connected_, false);
    }
    return *this;
}

Status MySqlConnection::connect(const MySqlConfig& config) {
    close();

    handle_ = mysql_init(nullptr);
    if (handle_ == nullptr) {
        return Status::error(ErrorCode::InternalError, "mysql_init failed");
    }

    if (mysql_options(handle_, MYSQL_SET_CHARSET_NAME, kUtf8Mb4) != 0) {
        const auto status = mysqlError(handle_, "mysql_options MYSQL_SET_CHARSET_NAME failed");
        close();
        return status;
    }

    MYSQL* connected = mysql_real_connect(handle_,
                                          config.host.c_str(),
                                          config.user.c_str(),
                                          config.password.c_str(),
                                          config.database.c_str(),
                                          config.port,
                                          nullptr,
                                          0);
    if (connected == nullptr) {
        const auto status = mysqlError(handle_, "mysql_real_connect failed");
        close();
        return status;
    }

    connected_ = true;
    return Status::ok();
}

Status MySqlConnection::ping() {
    if (!isConnected()) {
        return Status::error(ErrorCode::InvalidArgument, "MySQL connection is not connected");
    }
    if (mysql_ping(handle_) != 0) {
        return mysqlError(handle_, "mysql_ping failed");
    }
    return Status::ok();
}

void MySqlConnection::close() noexcept {
    if (handle_ != nullptr) {
        mysql_close(handle_);
        handle_ = nullptr;
    }
    connected_ = false;
}

bool MySqlConnection::isConnected() const noexcept {
    return connected_ && handle_ != nullptr;
}

MYSQL* MySqlConnection::nativeHandle() noexcept {
    return handle_;
}

PreparedStatement::PreparedStatement(MySqlConnection& connection)
    : connection_(&connection), impl_(std::make_unique<PreparedStatementImpl>()) {
}

PreparedStatement::~PreparedStatement() {
    close();
}

PreparedStatement::PreparedStatement(PreparedStatement&& other) noexcept = default;

PreparedStatement& PreparedStatement::operator=(PreparedStatement&& other) noexcept = default;

Status PreparedStatement::prepare(const std::string& sql) {
    close();
    impl_ = std::make_unique<PreparedStatementImpl>();

    if (connection_ == nullptr || !connection_->isConnected()) {
        return Status::error(ErrorCode::InvalidArgument, "MySQL connection is not connected");
    }

    impl_->statement = mysql_stmt_init(connection_->nativeHandle());
    if (impl_->statement == nullptr) {
        return mysqlError(connection_->nativeHandle(), "mysql_stmt_init failed");
    }

    if (mysql_stmt_prepare(impl_->statement, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
        const auto status = statementError(impl_->statement, "mysql_stmt_prepare failed");
        close();
        return status;
    }

    const auto parameter_count = mysql_stmt_param_count(impl_->statement);
    impl_->parameters.resize(parameter_count);
    impl_->parameter_values.resize(parameter_count);
    for (auto& bind : impl_->parameters) {
        resetBind(bind);
    }

    return Status::ok();
}

Status PreparedStatement::bindInt64(std::size_t index, std::int64_t value) {
    if (!isPrepared()) {
        return Status::error(ErrorCode::InvalidArgument, "prepared statement is not prepared");
    }
    if (index >= impl_->parameters.size()) {
        return Status::error(ErrorCode::InvalidArgument, "parameter index out of range");
    }

    auto& parameter_value = impl_->parameter_values[index];
    parameter_value.bound = true;
    parameter_value.is_null = false;
    parameter_value.int64_value = value;
    parameter_value.length = sizeof(parameter_value.int64_value);

    auto& bind = impl_->parameters[index];
    resetBind(bind);
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &parameter_value.int64_value;
    bind.is_null = &parameter_value.is_null;
    bind.length = &parameter_value.length;
    bind.is_unsigned = false;
    return Status::ok();
}

Status PreparedStatement::bindString(std::size_t index, const std::string& value) {
    if (!isPrepared()) {
        return Status::error(ErrorCode::InvalidArgument, "prepared statement is not prepared");
    }
    if (index >= impl_->parameters.size()) {
        return Status::error(ErrorCode::InvalidArgument, "parameter index out of range");
    }

    auto& parameter_value = impl_->parameter_values[index];
    parameter_value.bound = true;
    parameter_value.is_null = false;
    parameter_value.string_value = value;
    parameter_value.length = static_cast<unsigned long>(parameter_value.string_value.size());

    auto& bind = impl_->parameters[index];
    resetBind(bind);
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = parameter_value.string_value.data();
    bind.buffer_length = parameter_value.length;
    bind.is_null = &parameter_value.is_null;
    bind.length = &parameter_value.length;
    return Status::ok();
}

Status PreparedStatement::executeUpdate(std::uint64_t& affected_rows) {
    affected_rows = 0;
    const auto bind_status = bindParameters();
    if (!bind_status.isOk()) {
        return bind_status;
    }

    if (mysql_stmt_execute(impl_->statement) != 0) {
        return statementError(impl_->statement, "mysql_stmt_execute failed");
    }

    const auto affected = mysql_stmt_affected_rows(impl_->statement);
    if (affected == static_cast<my_ulonglong>(-1)) {
        return statementError(impl_->statement, "mysql_stmt_affected_rows failed");
    }
    affected_rows = static_cast<std::uint64_t>(affected);
    mysql_stmt_free_result(impl_->statement);
    return Status::ok();
}

Status PreparedStatement::executeQuery(MySqlQueryResult& result) {
    result.clear();
    if (!isPrepared()) {
        return Status::error(ErrorCode::InvalidArgument, "prepared statement is not prepared");
    }

    bool update_max_length = true;
    if (mysql_stmt_attr_set(impl_->statement, STMT_ATTR_UPDATE_MAX_LENGTH, &update_max_length) != 0) {
        return statementError(impl_->statement, "mysql_stmt_attr_set failed");
    }

    const auto bind_status = bindParameters();
    if (!bind_status.isOk()) {
        return bind_status;
    }

    if (mysql_stmt_execute(impl_->statement) != 0) {
        return statementError(impl_->statement, "mysql_stmt_execute failed");
    }

    MYSQL_RES* metadata = mysql_stmt_result_metadata(impl_->statement);
    if (metadata == nullptr) {
        mysql_stmt_free_result(impl_->statement);
        return Status::error(ErrorCode::InvalidArgument, "prepared statement did not return a result set");
    }
    const auto metadata_guard = std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>(metadata, mysql_free_result);

    if (mysql_stmt_store_result(impl_->statement) != 0) {
        return statementError(impl_->statement, "mysql_stmt_store_result failed");
    }

    const unsigned int column_count = mysql_num_fields(metadata);
    MYSQL_FIELD* fields = mysql_fetch_fields(metadata);
    result.columns_.reserve(column_count);

    std::vector<MYSQL_BIND> result_binds(column_count);
    std::vector<ResultColumnBuffer> buffers(column_count);
    for (unsigned int column = 0; column < column_count; ++column) {
        result.columns_.push_back(fields[column].name != nullptr ? fields[column].name : "");

        const auto max_length = std::max<unsigned long>(fields[column].max_length, 1UL);
        buffers[column].buffer.resize(static_cast<std::size_t>(max_length) + 1U);

        auto& bind = result_binds[column];
        resetBind(bind);
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = buffers[column].buffer.data();
        bind.buffer_length = static_cast<unsigned long>(buffers[column].buffer.size());
        bind.length = &buffers[column].length;
        bind.is_null = &buffers[column].is_null;
        bind.error = &buffers[column].error;
    }

    if (!result_binds.empty() && mysql_stmt_bind_result(impl_->statement, result_binds.data()) != 0) {
        mysql_stmt_free_result(impl_->statement);
        return statementError(impl_->statement, "mysql_stmt_bind_result failed");
    }

    while (true) {
        for (auto& buffer : buffers) {
            buffer.length = 0;
            buffer.is_null = false;
            buffer.error = false;
        }

        const int fetch_status = mysql_stmt_fetch(impl_->statement);
        if (fetch_status == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) {
            mysql_stmt_free_result(impl_->statement);
            return statementError(impl_->statement, "mysql_stmt_fetch failed");
        }

        MySqlRow row;
        row.values.reserve(column_count);
        for (unsigned int column = 0; column < column_count; ++column) {
            const auto& buffer = buffers[column];
            if (buffer.is_null) {
                row.values.push_back(std::nullopt);
                continue;
            }

            if (buffer.error || buffer.length >= result_binds[column].buffer_length) {
                std::optional<std::string> value;
                const auto status = fetchFullColumn(impl_->statement, column, buffer.length, value);
                if (!status.isOk()) {
                    mysql_stmt_free_result(impl_->statement);
                    return status;
                }
                row.values.push_back(std::move(value));
            } else {
                row.values.emplace_back(std::string(buffer.buffer.data(), buffer.length));
            }
        }
        result.rows_.push_back(std::move(row));
    }

    mysql_stmt_free_result(impl_->statement);
    return Status::ok();
}

void PreparedStatement::close() noexcept {
    if (impl_ && impl_->statement != nullptr) {
        mysql_stmt_close(impl_->statement);
        impl_->statement = nullptr;
    }
    if (impl_) {
        impl_->parameters.clear();
        impl_->parameter_values.clear();
    }
}

Status PreparedStatement::bindParameters() {
    if (!isPrepared()) {
        return Status::error(ErrorCode::InvalidArgument, "prepared statement is not prepared");
    }
    for (std::size_t index = 0; index < impl_->parameter_values.size(); ++index) {
        if (!impl_->parameter_values[index].bound) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "missing bind value for parameter index " + std::to_string(index));
        }
    }
    if (!impl_->parameters.empty() && mysql_stmt_bind_param(impl_->statement, impl_->parameters.data()) != 0) {
        return statementError(impl_->statement, "mysql_stmt_bind_param failed");
    }
    return Status::ok();
}

bool PreparedStatement::isPrepared() const noexcept {
    return impl_ && impl_->statement != nullptr;
}

} // namespace liteim
