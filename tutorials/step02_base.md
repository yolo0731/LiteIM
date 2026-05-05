# Step 2: Config / Logger / ErrorCode 基础模块

## 1. 本 Step 解决什么问题

Step 1 只有最小 CMake、server target 和 GoogleTest 链路。Step 2 开始引入第一个真正可复用的 C++ 模块：`liteim_base`。

`base` 模块不负责聊天业务，也不负责网络 I/O。它只解决后续每一层都会遇到的公共问题：

- 服务端监听地址、端口、I/O 线程数、业务线程数从哪里来。
- MySQL / Redis / Qt 客户端默认连接参数放在哪里。
- 日志用什么统一入口输出。
- 函数失败时如何表达错误原因，而不是只返回 `false`。
- 消息时间、日志时间、压测统计时间如何统一表示。

所以本 Step 新增：

```text
Config      -> 统一配置
Logger      -> 统一日志入口，当前基于 spdlog
ErrorCode   -> 统一错误码
Status      -> 统一成功/失败返回值
Timestamp   -> 统一时间戳表示
```

本 Step 仍然不实现 socket、epoll、TLV 协议、MySQL 连接池、Redis 连接池或 Qt 客户端。

## 2. 本 Step 新增文件

```text
include/liteim/base/
├── Config.hpp
├── ErrorCode.hpp
├── Logger.hpp
├── Status.hpp
└── Timestamp.hpp

src/
├── CMakeLists.txt
└── base/
    ├── CMakeLists.txt
    ├── Config.cpp
    ├── ErrorCode.cpp
    ├── Logger.cpp
    ├── Status.cpp
    └── Timestamp.cpp

tests/base/
├── config_test.cpp
├── error_code_test.cpp
├── logger_test.cpp
└── timestamp_test.cpp
```

头文件放到 `include/liteim/base/`，实现放到 `src/base/`。这样后续代码统一使用项目限定 include：

```cpp
#include "liteim/base/Config.hpp"
#include "liteim/base/Logger.hpp"
```

不要把头文件放回旧路线里的 `server/net` 或 `server/protocol`。

## 3. CMake 结构

根 `CMakeLists.txt` 新增了 `spdlog`：

```cmake
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)
```

然后接入 `src/`：

```cmake
add_subdirectory(src)
add_subdirectory(server)
add_subdirectory(tests)
```

`src/base/CMakeLists.txt` 生成 `liteim_base`：

```cmake
add_library(liteim_base
    Config.cpp
    ErrorCode.cpp
    Logger.cpp
    Status.cpp
    Timestamp.cpp
)
```

关键点：

- `liteim_base` 对外暴露 `${PROJECT_SOURCE_DIR}/include`。
- `liteim_base` 链接 `spdlog::spdlog_header_only`。
- `liteim_server` 和 `liteim_tests` 都链接 `liteim_base`。

这说明 `base` 是公共库，不是 server 入口文件里的临时工具函数。

## 4. Config 设计

`Config` 当前保存这些配置：

```cpp
struct Config {
    std::string server_host{"0.0.0.0"};
    std::uint16_t server_port{9000};
    std::uint32_t io_threads{4};
    std::uint32_t business_threads{4};
    std::string log_level{"info"};
    MySqlConfig mysql;
    RedisConfig redis;
    QtClientConfig qt_client;

    static Config defaults();
    Status loadFromFile(const std::filesystem::path& path);
};
```

### `Config::defaults()`

职责：返回一份默认配置。

当前默认值包括：

- `server.host = 0.0.0.0`
- `server.port = 9000`
- `server.io_threads = 4`
- `server.business_threads = 4`
- `mysql.host = 127.0.0.1`
- `mysql.port = 3306`
- `redis.host = 127.0.0.1`
- `redis.port = 6379`
- `qt.server_host = 127.0.0.1`
- `qt.server_port = 9000`

这些只是默认值，不代表本 Step 已经连接 MySQL 或 Redis。

### `Config::loadFromFile(path)`

职责：从简单的 `key=value` 文件覆盖默认配置。

支持的形式：

```text
# comment
server.host = 127.0.0.1
server.port = 10086
log.level = debug
mysql.host = mysql.local
redis.port = 6380
qt.server_port = 10087
```

实现边界：

- 支持 `#` 注释。
- 支持 key/value 两侧空格。
- 缺失的配置项保留默认值。
- 未知 key 返回 `ErrorCode::InvalidArgument`。
- 非法端口返回 `ErrorCode::ParseError`。
- 缺失文件返回 `ErrorCode::NotFound`。

本 Step 不引入 YAML / JSON / TOML，是为了让项目先保持可读、可测、可手写。等配置项明显复杂之后，再考虑更成熟的配置格式。

## 5. ErrorCode 和 Status

`ErrorCode` 是统一错误码：

```cpp
enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    IoError,
    ParseError,
    ConfigError,
    InternalError,
};
```

`toString(ErrorCode code)` 用于把错误码转成可读字符串，方便日志、测试和调试。

`Status` 是简单的函数返回状态：

```cpp
class Status {
public:
    static Status ok();
    static Status error(ErrorCode code, std::string message);

    bool isOk() const noexcept;
    ErrorCode code() const noexcept;
    const std::string& message() const noexcept;
};
```

为什么不只返回 `bool`：

- `false` 只能表示失败，不能说明失败原因。
- `Status` 可以携带 `ErrorCode` 和错误信息。
- 单元测试可以精确断言失败类型，例如 `NotFound` 或 `ParseError`。

为什么现在不直接做复杂的 `Result<T>`：

- Step 2 的接口还很少，`Status` 足够表达无返回值函数的成败。
- 后续 DAO、协议解析、Redis 调用如果需要返回数据，可以再引入 `Result<T>`，不急着过度设计。

## 6. Logger 设计

`Logger` 当前封装 `spdlog`：

```cpp
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off,
};

LogLevel parseLogLevel(std::string_view level);

class Logger {
public:
    static void init(LogLevel level = LogLevel::Info);
    static std::shared_ptr<spdlog::logger> get();
    static void setLevel(LogLevel level);
};
```

### `parseLogLevel()`

职责：把配置文件里的字符串变成枚举。

例如：

```cpp
parseLogLevel("debug") == LogLevel::Debug
parseLogLevel("warning") == LogLevel::Warn
parseLogLevel("verbose") == LogLevel::Info
```

未知日志级别回退到 `Info`，这样配置写错时不会导致服务端直接崩掉。

### `Logger::init()`

职责：初始化名为 `liteim` 的全局 logger，并设置日志格式：

```text
[2026-05-05 16:08:57.973] [info] LiteIM server scaffold is running on 0.0.0.0:9000
```

内部使用 `std::mutex` 保护初始化，避免多个模块同时第一次调用 logger 时重复创建。

### `Logger::get()`

职责：返回当前 logger。如果还没有显式调用 `init()`，它会按 `Info` 级别创建默认 logger。

### `Logger::setLevel()`

职责：运行时调整日志级别。

本 Step 不做自研异步日志。因为当前还没有 I/O 线程和业务线程池，先把统一日志入口接好更重要。等高性能主线走到线程模型和压测阶段，可以再根据实际需要升级成异步日志策略。

## 7. Timestamp 设计

`Timestamp` 封装 `std::chrono::system_clock::time_point`：

```cpp
class Timestamp {
public:
    using Clock = std::chrono::system_clock;

    Timestamp();
    explicit Timestamp(Clock::time_point time_point);

    static Timestamp now();
    std::int64_t millisecondsSinceEpoch() const;
    std::string toIso8601String() const;
};
```

接口含义：

- `Timestamp()`：构造 Unix epoch 时间点。
- `Timestamp(time_point)`：用指定时间点构造，方便测试。
- `now()`：返回当前系统时间。
- `millisecondsSinceEpoch()`：返回从 Unix epoch 到当前时间点的毫秒数。
- `toIso8601String()`：返回 UTC 字符串，例如 `1970-01-01T00:00:00Z`。

后续消息表、历史消息分页、日志字段、压测统计都需要时间字段，所以 Step 2 先放一个确定性的时间工具。

## 8. server/main.cpp 的变化

Step 1 的 server 只是打印普通文本。Step 2 改成：

```cpp
#include "liteim/base/Config.hpp"
#include "liteim/base/Logger.hpp"

int main() {
    const auto config = liteim::Config::defaults();
    liteim::Logger::init(liteim::parseLogLevel(config.log_level));
    liteim::Logger::get()->info("LiteIM server scaffold is running on {}:{}",
                                config.server_host,
                                config.server_port);
    return 0;
}
```

这段代码仍然没有启动真实 TCP 服务。它只是证明：

- server target 能使用 `liteim_base`。
- 默认配置可读取。
- 日志系统可初始化。
- 格式化日志输出正常。

真实监听 socket 会在后续网络 Step 中实现。

## 9. 测试说明

本 Step 新增 4 个测试文件，加上 Step 1 的 smoke test，一共 15 个 CTest 用例。

### `tests/base/config_test.cpp`

测试：

```cpp
TEST(ConfigTest, DefaultsContainExpectedValues)
TEST(ConfigTest, LoadFromFileOverridesConfiguredValues)
TEST(ConfigTest, MissingValuesKeepDefaults)
TEST(ConfigTest, MissingFileReturnsNotFound)
TEST(ConfigTest, UnknownKeyFails)
TEST(ConfigTest, InvalidPortFails)
```

覆盖点：

- 默认配置是否符合预期。
- 配置文件能否覆盖 server / MySQL / Redis / Qt 字段。
- 只写一个配置项时，其他字段是否保留默认值。
- 文件不存在时是否返回 `ErrorCode::NotFound`。
- 未知 key 是否返回 `ErrorCode::InvalidArgument`。
- 非法端口是否返回 `ErrorCode::ParseError`。

### `tests/base/error_code_test.cpp`

测试：

```cpp
TEST(ErrorCodeTest, ToStringReturnsReadableNames)
TEST(StatusTest, OkStatusHasOkCode)
TEST(StatusTest, ErrorStatusCarriesCodeAndMessage)
```

覆盖点：

- 每个错误码能转成可读字符串。
- `Status::ok()` 的 code 是 `Ok`，message 为空。
- `Status::error()` 能保留错误码和错误信息。

### `tests/base/logger_test.cpp`

测试：

```cpp
TEST(LoggerTest, ParseLogLevelReturnsExpectedLevel)
TEST(LoggerTest, UnknownLogLevelFallsBackToInfo)
TEST(LoggerTest, InitCreatesReusableLogger)
```

覆盖点：

- 常见日志级别字符串能正确解析。
- 未知日志级别回退到 `Info`。
- `Logger::init()` 后能拿到名为 `liteim` 的 logger。

### `tests/base/timestamp_test.cpp`

测试：

```cpp
TEST(TimestampTest, NowReturnsPositiveEpochMilliseconds)
TEST(TimestampTest, Iso8601StringUsesUtcFormat)
```

覆盖点：

- 当前时间戳大于 0。
- Unix epoch 时间点格式化为 `1970-01-01T00:00:00Z`。

## 10. 如何验证

在 `LiteIM/` 目录下运行：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

server 预期输出类似：

```text
[2026-05-05 16:08:57.973] [info] LiteIM server scaffold is running on 0.0.0.0:9000
```

CTest 预期：

```text
100% tests passed, 0 tests failed out of 15
```

测试通过说明：

- `liteim_base` 可以被 server 和 tests 正常链接。
- GoogleTest / CTest 链路继续有效。
- 配置、日志、错误码、状态和时间戳的基础行为可验证。

## 11. 面试时怎么讲

可以这样讲：

> 我没有一开始就写聊天业务，而是先抽出一个 `base` 模块，统一处理配置、日志、错误码、状态返回和时间戳。这样后续协议层、网络层、MySQL / Redis 层、Qt 客户端和测试都能复用同一套基础设施。配置当前用简单 `key=value` 文件，缺失项保留默认值，非法端口、未知 key、缺失文件都通过 `Status + ErrorCode` 返回明确错误；日志使用 `spdlog` 封装成项目统一入口，避免后续模块散落 `cout`。

重点表达：

- `base` 模块是底层公共依赖，不反向依赖网络或业务层。
- `Status` 比裸 `bool` 更适合排查错误。
- 配置解析先保持简单，不提前引入复杂配置库。
- `spdlog` 是成熟日志库，当前先统一入口，异步日志不在 Step 2 过度实现。
- 每个基础能力都有 GoogleTest 覆盖，后续重构更稳。

## 12. 面试常见追问

### 为什么不直接用异常处理配置错误？

可以用异常，但这里选择 `Status` 是为了让错误路径更显式。配置文件缺失、未知 key、端口非法都属于可预期错误，返回 `Status` 更容易在调用处写清楚处理逻辑，也更方便单元测试断言错误码。

### 为什么不直接使用 JSON / YAML / TOML？

第一版配置项不复杂，`key=value` 足够。这个项目重点是 C++ 网络服务器，不是配置系统。先实现可测的轻量解析器，后续如果配置层复杂，再替换为成熟库。

### 为什么 `Logger` 要封装 `spdlog`？

封装后业务代码依赖的是项目自己的 `Logger` 入口，而不是到处直接创建 `spdlog` logger。以后如果要换日志格式、调整输出位置、升级异步日志，只需要改 `base` 层。

### 为什么 `Timestamp` 用 UTC？

服务端内部日志、消息存储、压测统计应该避免依赖本地时区。UTC 更适合作为跨客户端、跨环境的一致时间表达。

### 当前 `Logger` 是异步的吗？

不是。Step 2 只建立统一日志入口。当前项目还没有 I/O 线程和业务线程池，没必要过早实现异步日志。后续如果压测发现同步日志影响性能，可以在保持 `Logger` 对外接口不变的情况下升级实现。

## 13. 本 Step 提交

提交信息：

```text
feat(base): add config logger and error code
```
