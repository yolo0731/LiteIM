# Step 1：从零初始化 LiteIM CMake 工程

本步骤目标：从一个空目录开始，创建一个可以编译、运行、测试、提交的 C++17 项目骨架。

这一阶段不写网络代码，也不写 Qt。先把工程结构搭好，因为后面的协议、epoll、SQLite、Qt 都要挂在这个骨架上。

## 1. 这一步最终要得到什么

完成后，项目目录应该长这样：

```text
LiteIM/
├── .gitignore
├── CMakeLists.txt
├── README.md
├── client_qt/
│   └── CMakeLists.txt
├── docs/
│   ├── architecture.md
│   ├── database.md
│   ├── interview_notes.md
│   └── protocol.md
├── server/
│   ├── CMakeLists.txt
│   └── main.cpp
├── sql/
│   └── init.sql
├── tests/
│   ├── CMakeLists.txt
│   └── test_smoke.cpp
└── tutorials/
```

并且下面这些命令可以成功：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

## 2. 你要先理解的概念

### 2.1 什么是 CMake

CMake 不是编译器。它是“生成构建系统的工具”。

你写：

```bash
cmake -S . -B build
```

意思是：

- `-S .`：源码目录是当前目录。
- `-B build`：把生成的构建文件放到 `build/`。

然后你写：

```bash
cmake --build build
```

意思是：

- 使用 `build/` 里的构建文件真正编译项目。

所以 CMake 的流程是：

```text
CMakeLists.txt
    ↓ cmake -S . -B build
生成 Makefile / Ninja 文件
    ↓ cmake --build build
调用 g++ 编译源码
    ↓
生成可执行文件
```

### 2.2 为什么构建产物放到 build/

不要在源码目录里直接生成 `.o` 文件、Makefile 和可执行文件。

正确做法是 out-of-source build：

```text
源码：LiteIM/
构建产物：LiteIM/build/
```

好处：

- 源码目录干净。
- 删除 `build/` 就能重新构建。
- `.gitignore` 可以直接忽略 `build/`。

### 2.3 为什么先拆 server、tests、client_qt

项目后面会越来越大：

- `server/`：C++ 服务端。
- `tests/`：测试代码。
- `client_qt/`：Qt 桌面客户端。
- `docs/`：架构、协议、数据库、面试笔记。
- `sql/`：数据库初始化脚本。
- `tutorials/`：学习笔记和逐步教学。

提前拆目录不是为了复杂，而是为了避免后面所有文件堆在一起。

## 3. 从空目录开始操作

假设你现在在：

```bash
cd ~/jianli
```

创建项目目录：

```bash
mkdir LiteIM
cd LiteIM
```

创建子目录：

```bash
mkdir server tests client_qt docs sql tutorials
```

这时项目还不能编译，因为还没有 CMake 文件和源码。

## 4. 创建顶层 CMakeLists.txt

新建文件：

```text
LiteIM/CMakeLists.txt
```

内容：

```cmake
cmake_minimum_required(VERSION 3.16)

project(LiteIM
    VERSION 0.1.0
    DESCRIPTION "A lightweight IM system built with C++17, epoll, Qt and SQLite"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LITEIM_BUILD_QT_CLIENT "Build the Qt Widgets client" OFF)

enable_testing()

add_subdirectory(server)
add_subdirectory(tests)
add_subdirectory(client_qt)
```

逐行解释：

```cmake
cmake_minimum_required(VERSION 3.16)
```

要求 CMake 版本至少是 3.16。版本太低可能不支持某些现代写法。

```cmake
project(LiteIM ...)
```

定义项目名、版本、描述和语言。这里语言是 C++。

```cmake
set(CMAKE_CXX_STANDARD 17)
```

指定使用 C++17。项目后面会用到 `std::optional`、`std::string_view` 等现代 C++ 特性，所以不用 C++11。

```cmake
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

表示 C++17 是硬要求，不允许编译器降级。

```cmake
set(CMAKE_CXX_EXTENSIONS OFF)
```

关闭 GNU 扩展，让代码更接近标准 C++。

```cmake
option(LITEIM_BUILD_QT_CLIENT "Build the Qt Widgets client" OFF)
```

定义一个开关。默认不构建 Qt 客户端，因为第一阶段只做服务端。等后面写 Qt 时再打开。

```cmake
enable_testing()
```

启用 CTest。注意这个最好放在顶层，不然 `ctest --test-dir build` 可能找不到测试。

```cmake
add_subdirectory(server)
add_subdirectory(tests)
add_subdirectory(client_qt)
```

告诉 CMake 进入这些子目录继续读取它们自己的 `CMakeLists.txt`。

## 5. 创建 server 模块

新建文件：

```text
server/CMakeLists.txt
```

内容：

```cmake
add_executable(liteim_server
    main.cpp
)

target_compile_options(liteim_server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
```

解释：

```cmake
add_executable(liteim_server main.cpp)
```

生成一个可执行文件，名字叫 `liteim_server`，源码是 `main.cpp`。

```cmake
target_compile_options(...)
```

给这个目标添加编译警告：

- `-Wall`：开启常见警告。
- `-Wextra`：开启更多警告。
- `-Wpedantic`：提醒不标准的写法。

为什么要开警告？因为 C++ 很容易写出能编译但有隐患的代码，早期开警告能减少低级错误。

新建文件：

```text
server/main.cpp
```

内容：

```cpp
#include <iostream>

int main() {
    std::cout << "LiteIM server starting..." << std::endl;
    return 0;
}
```

解释：

```cpp
#include <iostream>
```

引入标准输入输出库。

```cpp
int main()
```

C++ 程序入口。

```cpp
std::cout << "LiteIM server starting..." << std::endl;
```

打印启动信息。现在服务端还没有网络功能，只用这行证明可执行文件能跑。

```cpp
return 0;
```

返回 0 表示程序正常退出。

## 6. 创建 tests 模块

新建文件：

```text
tests/CMakeLists.txt
```

内容：

```cmake
add_executable(liteim_tests
    test_smoke.cpp
)

target_compile_options(liteim_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(NAME liteim_smoke_tests COMMAND liteim_tests)
```

解释：

```cmake
add_executable(liteim_tests test_smoke.cpp)
```

生成测试可执行文件 `liteim_tests`。

```cmake
add_test(NAME liteim_smoke_tests COMMAND liteim_tests)
```

把 `liteim_tests` 注册给 CTest。这样后面可以用：

```bash
ctest --test-dir build --output-on-failure
```

统一运行测试。

新建文件：

```text
tests/test_smoke.cpp
```

内容：

```cpp
#include <iostream>

int main() {
    std::cout << "LiteIM tests placeholder" << std::endl;
    return 0;
}
```

这是一个占位测试。它暂时不验证业务逻辑，只验证测试目标能不能编译和运行。

后面 Step 2 和 Step 3 会把它替换成真正的协议测试。

## 7. 创建 Qt 客户端占位

新建文件：

```text
client_qt/CMakeLists.txt
```

内容：

```cmake
if(LITEIM_BUILD_QT_CLIENT)
    message(STATUS "Qt client build is not implemented yet. It will be added after the server MVP.")
else()
    message(STATUS "Qt client is disabled. Configure with -DLITEIM_BUILD_QT_CLIENT=ON after Qt files are implemented.")
endif()
```

为什么现在不写 Qt？

因为你当前最重要的是服务端：

```text
协议 → epoll → Session → 登录 → 私聊
```

Qt 只是展示层。如果一开始写 UI，很容易花大量时间调界面，但服务端还没有核心能力。

这个占位文件的作用是：

- 保留 `client_qt/` 目录。
- 顶层 CMake 可以正常 `add_subdirectory(client_qt)`。
- 默认不会构建 Qt，避免电脑没有 Qt 时编译失败。

## 8. 创建文档占位

创建：

```text
docs/architecture.md
docs/protocol.md
docs/database.md
docs/interview_notes.md
```

它们的作用：

- `architecture.md`：记录系统架构。
- `protocol.md`：记录 TLV 协议。
- `database.md`：记录 SQLite 表设计。
- `interview_notes.md`：记录面试讲法。

你做简历项目时，文档不是装饰。文档能帮助你把“我写了代码”升级成“我理解设计”。

## 9. 创建 SQL 占位

创建：

```text
sql/init.sql
```

内容先写：

```sql
-- SQLite schema will be added when the storage layer is implemented.
```

后面 Step 13 接 SQLite 时，会在这里写 `users`、`groups`、`group_members`、`messages` 表。

## 10. 创建 .gitignore

新建：

```text
.gitignore
```

内容：

```gitignore
build/
cmake-build-*/
*.db
*.db-journal
*.log
```

解释：

- `build/`：CMake 构建产物，不提交。
- `cmake-build-*/`：CLion 等 IDE 生成的构建目录，不提交。
- `*.db`：本地 SQLite 数据库，不提交。
- `*.db-journal`：SQLite 日志文件，不提交。
- `*.log`：运行日志，不提交。

原则：

```text
源码、文档、配置模板可以提交。
编译产物、本地数据库、运行日志不要提交。
```

## 11. 第一次配置项目

在 `LiteIM/` 根目录执行：

```bash
cmake -S . -B build
```

如果成功，你会看到类似：

```text
-- Configuring done
-- Generating done
-- Build files have been written to: .../LiteIM/build
```

这一步只生成构建文件，还没有真正编译。

常见错误：

### 错误 1：找不到 CMakeLists.txt

说明你不在项目根目录。

检查：

```bash
pwd
ls
```

你应该看到：

```text
CMakeLists.txt  server  tests  client_qt
```

### 错误 2：CMake 版本太低

检查：

```bash
cmake --version
```

如果低于 3.16，需要升级 CMake 或降低 `cmake_minimum_required`，但建议升级。

## 12. 编译项目

执行：

```bash
cmake --build build
```

成功后会生成：

```text
build/server/liteim_server
build/tests/liteim_tests
```

这里你要理解一个点：

```text
server/main.cpp
    ↓ 编译
build/server/liteim_server
```

源码在 `server/`，可执行文件在 `build/server/`。

## 13. 运行服务端

执行：

```bash
./build/server/liteim_server
```

预期输出：

```text
LiteIM server starting...
```

这说明：

- CMake 配置成功。
- C++ 编译成功。
- 可执行文件生成成功。
- 程序入口 `main()` 正常执行。

## 14. 运行测试

执行：

```bash
ctest --test-dir build --output-on-failure
```

预期输出：

```text
100% tests passed, 0 tests failed out of 1
```

`--output-on-failure` 的作用：

- 测试失败时打印失败输出。
- 成功时保持输出简洁。

这是一个很好的习惯，后面 CI 和本地调试都常用。

### 14.1 这一步的测试是在测什么

Step 1 的测试不是业务测试，而是工程链路测试。

它主要验证：

- 顶层 `enable_testing()` 是否生效。
- `tests/CMakeLists.txt` 是否正确生成 `liteim_tests`。
- `add_test()` 是否把测试目标注册到了 CTest。
- 测试二进制是否可以正常启动并返回 0。

换句话说，Step 1 的测试证明：

```text
以后新增协议测试、网络测试、定时器测试时，项目已经有统一的测试入口。
```

如果这一步 `ctest` 都跑不起来，后面写再多单元测试也没法稳定验证。

### 14.2 如何测试

最小测试命令是：

```bash
ctest --test-dir build --output-on-failure
```

如果失败，优先检查：

- 顶层 `CMakeLists.txt` 是否写了 `enable_testing()`。
- `tests/CMakeLists.txt` 是否写了 `add_test()`。
- 是否已经重新执行过 `cmake -S . -B build`。

## 15. 初始化 Git 并提交

如果还没有 Git 仓库：

```bash
git init
git branch -m main
```

查看状态：

```bash
git status --short
```

暂存并提交：

```bash
git add .
git commit -m "init: create LiteIM project structure"
```

这个提交信息的含义：

- `init`：初始化类提交。
- `create LiteIM project structure`：具体说明创建了工程结构。

后面每一步都建议单独提交。比如：

```text
feat(protocol): add packet header encoding and validation
feat(protocol): implement TCP frame decoder
feat(net): add buffer abstraction
```

这样面试官看仓库时，能看到你是一步一步做出来的。

## 16. 本步骤每个文件的用途

### `CMakeLists.txt`

项目总入口。负责设置 C++ 标准、启用测试、引入子目录。

### `server/CMakeLists.txt`

服务端构建规则。负责生成 `liteim_server`。

### `server/main.cpp`

服务端程序入口。现在只是打印启动信息，后面会创建 `TcpServer` 并启动事件循环。

未来它大概会变成：

```cpp
int main() {
    EventLoop loop;
    TcpServer server(&loop, "0.0.0.0", 9000);
    server.start();
    loop.loop();
}
```

现在不要急着写这些，因为 `EventLoop` 和 `TcpServer` 还不存在。

### `tests/CMakeLists.txt`

测试构建规则。负责生成 `liteim_tests` 并注册 CTest。

### `tests/test_smoke.cpp`

占位测试。后面会逐步替换成：

- `test_protocol.cpp`
- `test_frame_decoder.cpp`
- `test_timer_heap.cpp`
- `test_message_router.cpp`

### `client_qt/CMakeLists.txt`

Qt 客户端占位。现在默认不构建，避免干扰服务端开发。

### `docs/`

项目文档目录。后面写 README 时，可以直接引用这里的内容。

### `sql/init.sql`

数据库初始化脚本。Step 13 接入 SQLite 时会真正写表结构。

### `.gitignore`

告诉 Git 哪些文件不要提交。

## 17. 本步骤的验收标准

你自己做完 Step 1 后，必须满足：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git status --short
```

其中：

- 服务端必须输出 `LiteIM server starting...`
- CTest 必须通过
- `git status --short` 应该是空的，表示没有未提交改动

## 18. 面试时怎么讲 Step 1

这一步本身不是核心亮点，但可以体现工程习惯。

你可以这样说：

> 我先用 CMake 搭建了一个 C++17 的多模块工程，把服务端、测试、Qt 客户端、文档和 SQL 脚本分开管理。服务端第一阶段只依赖 Linux socket、epoll、SQLite 和 nlohmann_json，不使用 Boost.Asio。为了保证每一步都可验证，我从一开始就创建了测试目标并接入 CTest，后续协议、FrameDecoder、TimerHeap 都会写单元测试。

不要把 Step 1 讲成技术亮点。真正的技术亮点从 Step 2 和 Step 3 开始：

- TLV 协议
- TCP 粘包/半包处理
- epoll Reactor
- Session 生命周期

### 18.1 讲解思路

面试里讲 Step 1 时，不要只说“我创建了几个目录”。更好的讲法是突出工程组织能力：

1. 我把服务端、测试、Qt 客户端、文档、SQL 脚本分开，是为了后续模块职责清楚。
2. 我从第一步就接入 CTest，是为了每个功能都能被验证，而不是最后才补测试。
3. 我默认关闭 Qt 客户端构建，是为了先聚焦服务端 MVP，避免早期被 GUI 依赖拖住。
4. 我用 `.gitignore` 排除构建产物和本地数据库，是为了仓库只保存源码、文档和必要配置。
5. 我每个 Step 单独提交，是为了让项目历史能体现开发过程，而不是一次性堆代码。

### 18.2 面试中容易被问到的问题

**Q1：为什么不用一个文件夹把所有代码放一起？**

因为这个项目后面会有网络层、协议层、业务层、存储层、Qt 客户端和测试。如果一开始不拆目录，后面文件混在一起，模块边界会很乱。

**Q2：为什么现在不写 Qt？**

因为 Qt 是展示层，真正体现 C++ 服务端能力的是协议、epoll、Session、心跳和存储。先做服务端，后做 GUI，风险更低。

**Q3：为什么 CTest 要从第一步接入？**

因为后续 `Packet`、`FrameDecoder`、`TimerHeap` 都适合单元测试。提前把测试入口跑通，后续每一步都能直接加测试。

**Q4：为什么 `build/` 不提交？**

`build/` 是构建产物，不是源码。不同机器、不同编译器生成内容可能不同，提交它会污染仓库。

**Q5：Step 1 有什么面试价值？**

它不是核心技术亮点，但能体现工程习惯：模块划分、构建系统、测试入口、版本管理和小步提交。

## 19. 这一阶段不要做什么

不要做：

- 不要写 Qt 页面。
- 不要写 epoll。
- 不要写 SQLite。
- 不要引入第三方库。
- 不要一次性生成几十个空类。

原因：

如果工程还没验证，就堆大量代码，后面出错时你不知道问题来自构建、目录、依赖还是代码逻辑。

正确方式是：

```text
小步实现 → 编译 → 测试 → 提交
```

## 20. 下一步预告：Step 2

Step 2 会开始写协议基础：

```text
include/liteim/protocol/MessageType.hpp
include/liteim/protocol/Packet.hpp
src/protocol/Packet.cpp
tests/test_protocol.cpp
```

核心问题：

TCP 是字节流，不知道一条消息从哪里开始、到哪里结束。

所以我们要定义：

```cpp
struct PacketHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t msg_type;
    uint32_t seq_id;
    uint32_t body_len;
};
```

后面 `FrameDecoder` 会先读 Header，再根据 `body_len` 读取完整 JSON Body。
