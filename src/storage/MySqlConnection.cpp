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

// 保存真正的参数值的结构体,因为 MYSQL_BIND 只能保存指针,所以需要一个地方来存储实际的值
struct ParameterValue {
    bool bound{false};
    bool is_null{false};
    std::int64_t int64_value{0};
    std::uint64_t uint64_value{0};
    std::string string_value;
    unsigned long length{0};
};

struct ResultColumnBuffer {
    std::vector<char> buffer;
    unsigned long length{0};
    bool is_null{false};
    bool error{false};
};

// 当初始缓冲区不足以容纳整个列数据时,从 MySQL 服务器再次读，获取完整列数据的函数
Status
fetchFullColumn(MYSQL_STMT* statement, unsigned int column, unsigned long length, std::optional<std::string>& value) {
    std::vector<char> buffer(static_cast<std::size_t>(length) + 1U);
    // 给c风格字符串预留一个额外的字节用于存储终止符'\0'
    unsigned long fetched_length = 0;
    bool is_null = false;
    bool error = false;

    MYSQL_BIND bind;  // MYSQL_BIND结构体用于描述参数或结果列的绑定信息,创建一个新的
                      // MYSQL_BIND,这次重新读取列数据时，按下面的方式设置读取规则
    resetBind(bind);  // 将bind结构体的所有成员初始化为0或nullptr
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = buffer.data();
    bind.buffer_length = static_cast<unsigned long>(buffer.size());
    bind.length = &fetched_length;  // 实际的长度将被写入fetched_length变量
    bind.is_null = &is_null;
    bind.error = &error;

    // mysql_stmt_fetch_column函数用于从MySQL服务器获取指定列的数据,并将数据存储在bind结构体描述的缓冲区中
    if (mysql_stmt_fetch_column(statement, &bind, column, 0) != 0)
    {
        return statementError(statement, "mysql_stmt_fetch_column failed");
    }
    if (is_null)
    {
        value = std::nullopt;
        return Status::ok();
    }

    value = std::string(buffer.data(), fetched_length);
    return Status::ok();
}

}  // namespace

struct PreparedStatementImpl {
    MYSQL_STMT* statement{nullptr};                // MySQL预处理语句的原生句柄，代表一条预处理 SQL 语句
    std::vector<MYSQL_BIND> parameters;            // 存储参数绑定信息的数组，大小等于预处理语句的 ? 参数数量
    std::vector<ParameterValue> parameter_values;  // 自己定义的真实参数值存储区
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
    : handle_(std::exchange(other.handle_, nullptr)), connected_(std::exchange(other.connected_, false)) {}

MySqlConnection& MySqlConnection::operator=(MySqlConnection&& other) noexcept {
    if (this != &other)
    {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
        connected_ = std::exchange(other.connected_, false);
    }
    return *this;
}

Status MySqlConnection::connect(const MySqlConfig& config) {
    close();

    handle_ = mysql_init(nullptr);  // 创建/初始化一个 MYSQL 原生连接句柄，nullptr表示由目前没有 MYSQL 对象
    if (handle_ == nullptr)
    {  // 创建失败
        return Status::error(ErrorCode::InternalError, "mysql_init failed");
    }

    // mysql_options(...)给这个连接句柄 handle_ 设置一个选项，用UTF-8编码方式连接
    if (mysql_options(handle_, MYSQL_SET_CHARSET_NAME, kUtf8Mb4) != 0)
    {
        const auto status = mysqlError(handle_, "mysql_options MYSQL_SET_CHARSET_NAME failed");
        close();
        return status;
    }
    // 用config 里的配置去连接 MySQL
    MYSQL* connected = mysql_real_connect(handle_,
                                          config.host.c_str(),
                                          config.user.c_str(),
                                          config.password.c_str(),
                                          config.database.c_str(),
                                          config.port,
                                          nullptr,
                                          0);
    if (connected == nullptr)
    {
        const auto status = mysqlError(handle_, "mysql_real_connect failed");
        close();
        return status;
    }

    connected_ = true;
    return Status::ok();
}

Status MySqlConnection::ping() {
    if (!isConnected())
    {
        return Status::error(ErrorCode::InvalidArgument, "MySQL connection is not connected");
    }
    if (mysql_ping(handle_) != 0)
    {
        return mysqlError(handle_, "mysql_ping failed");
    }
    return Status::ok();
}

Status MySqlConnection::executeSimple(const std::string& sql) {
    if (!isConnected())
    {
        return Status::error(ErrorCode::InvalidArgument, "MySQL connection is not connected");
    }
    if (mysql_query(handle_, sql.c_str()) != 0)  // 执行SQL语句,成功返回0,失败返回非0
    {
        return mysqlError(handle_, "mysql_query failed");
    }

    MYSQL_RES* result = mysql_store_result(handle_);
    // 获取查询结果,正常应该返回nullptr
    if (result != nullptr)
    {
        mysql_free_result(result);
        return Status::ok();
    }
    if (mysql_field_count(handle_) != 0)  // 检查是否有结果集
    {
        return mysqlError(handle_, "mysql_store_result failed");
    }
    return Status::ok();
}

void MySqlConnection::close() noexcept {
    if (handle_ != nullptr)
    {
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
    : connection_(&connection), impl_(std::make_unique<PreparedStatementImpl>()) {}

PreparedStatement::~PreparedStatement() {
    close();
}

PreparedStatement::PreparedStatement(PreparedStatement&& other) noexcept = default;

PreparedStatement& PreparedStatement::operator=(PreparedStatement&& other) noexcept = default;

// 把一条 SQL 字符串预编译成 MySQL Prepared Statement，并初始化参数绑定空间。
Status PreparedStatement::prepare(const std::string& sql) {
    close();
    impl_ = std::make_unique<PreparedStatementImpl>();

    if (connection_ == nullptr || !connection_->isConnected())
    {
        return Status::error(ErrorCode::InvalidArgument, "MySQL connection is not connected");
    }
    // mysql_stmt_init函数创建一个新的 MYSQL_STMT 结构体实例，并返回一个指向该实例的指针,这个实例代表一条预处理 SQL 语句
    impl_->statement = mysql_stmt_init(connection_->nativeHandle());
    if (impl_->statement == nullptr)
    {
        return mysqlError(connection_->nativeHandle(), "mysql_stmt_init failed");
    }
    // 把 SQL 发给 MySQL 进行预编译,c_str() 获取 C 风格字符串,返回值为0表示成功
    if (mysql_stmt_prepare(impl_->statement, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
    {
        const auto status = statementError(impl_->statement, "mysql_stmt_prepare failed");
        close();
        return status;
    }
    //识别里面有几个 ? 参数
    const auto parameter_count = mysql_stmt_param_count(impl_->statement);
    impl_->parameters.resize(parameter_count);  //预留参数绑定空间
    impl_->parameter_values.resize(parameter_count);
    for (auto& bind : impl_->parameters)
    {
        resetBind(bind);  // 将每个 MYSQL_BIND 结构体的所有成员初始化为0或nullptr
    }

    return Status::ok();
}

//  给已经 prepare() 好的 SQL 语句里的第 index 个 ? 参数绑定一个 C++ 值
Status PreparedStatement::bindInt64(std::size_t index, std::int64_t value) {
    if (!isPrepared())
    {
        return Status::error(ErrorCode::InvalidArgument, "prepared statement is not prepared");
    }
    if (index >= impl_->parameters.size())
    {
        return Status::error(ErrorCode::InvalidArgument, "parameter index out of range");
    }

    auto& parameter_value = impl_->parameter_values[index];
    // 获取第 index 个参数的 ParameterValue 存储结构体(真实参数值存储区)
    parameter_value.bound = true;
    parameter_value.is_null = false;
    parameter_value.int64_value = value;
    parameter_value.length = sizeof(parameter_value.int64_value);

    // 填写 MySQL C API 要求的 MYSQL_BIND 结构
    auto& bind = impl_->parameters[index];
    resetBind(bind);
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &parameter_value.int64_value;
    bind.is_null = &parameter_value.is_null;
    bind.length = &parameter_value.length;
    bind.is_unsigned = false;
    return Status::ok();
}

Status PreparedStatement::bindUInt64(std::size_t index, std::uint64_t value) {
    if (!isPrepared())
    {
        return Status::error(ErrorCode::InvalidArgument, "prepared statement is not prepared");
    }
    if (index >= impl_->parameters.size())
    {
        return Status::error(ErrorCode::InvalidArgument, "parameter index out of range");
    }

    auto& parameter_value = impl_->parameter_values[index];
    parameter_value.bound = true;
    parameter_value.is_null = false;
    parameter_value.uint64_value = value;
    parameter_value.length = sizeof(parameter_value.uint64_value);

    auto& bind = impl_->parameters[index];
    resetBind(bind);
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &parameter_value.uint64_value;
    bind.is_null = &parameter_value.is_null;
    bind.length = &parameter_value.length;
    bind.is_unsigned = true;
    return Status::ok();
}

Status PreparedStatement::bindString(std::size_t index, const std::string& value) {
    if (!isPrepared())
    {
        return Status::error(ErrorCode::InvalidArgument, "prepared statement is not prepared");
    }
    if (index >= impl_->parameters.size())
    {
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
//用于执行不会返回结果集的 SQL 语句，受影响的行数通过 affected_rows 参数返回
Status PreparedStatement::executeUpdate(std::uint64_t& affected_rows) {
    affected_rows = 0;
    const auto bind_status = bindParameters();  //把之前 bindInt64() / bindString() 绑定好的参数真正交给 MySQL
    if (!bind_status.isOk())
    {
        return bind_status;
    }

    // 真正执行预处理 SQL
    if (mysql_stmt_execute(impl_->statement) != 0)
    {
        return statementError(impl_->statement, "mysql_stmt_execute failed");
    }
    // 获取受影响的行数,如果返回-1表示获取失败
    const auto affected = mysql_stmt_affected_rows(impl_->statement);
    if (affected == static_cast<my_ulonglong>(-1))
    {
        return statementError(impl_->statement, "mysql_stmt_affected_rows failed");
    }
    affected_rows = static_cast<std::uint64_t>(affected);
    mysql_stmt_free_result(impl_->statement);
    return Status::ok();
}
// 执行已经 prepare() 好的 SQL 语句，用于执行会返回结果集 的 SQL,查询结果 rows/columns 存进 MySqlQueryResult
Status PreparedStatement::executeQuery(MySqlQueryResult& result) {
    result.clear();
    if (!isPrepared())
    {
        return Status::error(ErrorCode::InvalidArgument, "prepared statement is not prepared");
    }

    bool update_max_length = true;
    if (mysql_stmt_attr_set(impl_->statement, STMT_ATTR_UPDATE_MAX_LENGTH, &update_max_length) !=
        0)  //提供每一列的最大长度
    {
        return statementError(impl_->statement, "mysql_stmt_attr_set failed");
    }

    const auto bind_status = bindParameters();
    if (!bind_status.isOk())
    {
        return bind_status;
    }

    if (mysql_stmt_execute(impl_->statement) != 0)
    {
        return statementError(impl_->statement, "mysql_stmt_execute failed");
    }
    // 拿结果集的“列信息”，有几列、每列名字是什么
    MYSQL_RES* metadata = mysql_stmt_result_metadata(impl_->statement);
    if (metadata == nullptr)
    {
        mysql_stmt_free_result(impl_->statement);
        return Status::error(ErrorCode::InvalidArgument, "prepared statement did not return a result set");
    }
    const auto metadata_guard = std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>(metadata, mysql_free_result);

    if (mysql_stmt_store_result(impl_->statement) != 0)
    {
        return statementError(impl_->statement, "mysql_stmt_store_result failed");
    }

    const unsigned int column_count = mysql_num_fields(metadata);
    MYSQL_FIELD* fields = mysql_fetch_fields(metadata);
    result.columns_.reserve(column_count);

    std::vector<MYSQL_BIND> result_binds(column_count);
    std::vector<ResultColumnBuffer> buffers(column_count);
    for (unsigned int column = 0; column < column_count; ++column)
    {
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

    if (!result_binds.empty() && mysql_stmt_bind_result(impl_->statement, result_binds.data()) != 0)
    {
        mysql_stmt_free_result(impl_->statement);
        return statementError(impl_->statement, "mysql_stmt_bind_result failed");
    }

    while (true)
    {
        for (auto& buffer : buffers)
        {
            buffer.length = 0;
            buffer.is_null = false;
            buffer.error = false;
        }

        const int fetch_status = mysql_stmt_fetch(impl_->statement);
        if (fetch_status == MYSQL_NO_DATA)
        {
            break;
        }
        if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED)
        {
            mysql_stmt_free_result(impl_->statement);
            return statementError(impl_->statement, "mysql_stmt_fetch failed");
        }

        MySqlRow row;
        row.values.reserve(column_count);
        for (unsigned int column = 0; column < column_count; ++column)
        {
            const auto& buffer = buffers[column];
            if (buffer.is_null)
            {
                row.values.push_back(std::nullopt);
                continue;
            }

            if (buffer.error || buffer.length >= result_binds[column].buffer_length)
            {
                std::optional<std::string> value;
                const auto status = fetchFullColumn(impl_->statement, column, buffer.length, value);
                if (!status.isOk())
                {
                    mysql_stmt_free_result(impl_->statement);
                    return status;
                }
                row.values.push_back(std::move(value));
            } else
            {
                row.values.emplace_back(std::string(buffer.buffer.data(), buffer.length));
            }
        }
        result.rows_.push_back(std::move(row));
    }

    mysql_stmt_free_result(impl_->statement);
    return Status::ok();
}

unsigned int PreparedStatement::lastErrorNumber() const noexcept {
    if (!impl_ || impl_->statement == nullptr)
    {
        return 0;
    }
    return mysql_stmt_errno(impl_->statement);
}

void PreparedStatement::close() noexcept {
    if (impl_ && impl_->statement != nullptr)
    {
        mysql_stmt_close(impl_->statement);
        impl_->statement = nullptr;
    }
    if (impl_)
    {
        impl_->parameters.clear();
        impl_->parameter_values.clear();
    }
}

// 把已经设置好的参数值绑定到 MySQL 预处理语句上
Status PreparedStatement::bindParameters() {
    if (!isPrepared())
    {
        return Status::error(ErrorCode::InvalidArgument, "prepared statement is not prepared");
    }
    for (std::size_t index = 0; index < impl_->parameter_values.size(); ++index)
    {
        if (!impl_->parameter_values[index].bound)  // 检查每个参数是否都已经绑定了值
        {
            return Status::error(ErrorCode::InvalidArgument,
                                 "missing bind value for parameter index " + std::to_string(index));
        }
    }
    if (!impl_->parameters.empty() && mysql_stmt_bind_param(impl_->statement, impl_->parameters.data()) != 0)
    // mysql_stmt_bind_param真正把参数绑定信息(一条sql语句)交给 MySQL C API
    { return statementError(impl_->statement, "mysql_stmt_bind_param failed"); }
    return Status::ok();
}

bool PreparedStatement::isPrepared() const noexcept {
    return impl_ && impl_->statement != nullptr;
}

}  // namespace liteim
