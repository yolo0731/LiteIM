# Step 1：初始化 CMake 工程

## 1. 概念

Step 1 的目标是把 Step 0 的“空 CMake 骨架”变成一个最小可构建、可运行、可测试的 C++17 工程，并从第一步接入 GoogleTest。

这一步只解决三个问题：

1. CMake 能生成构建系统。
2. `liteim_server` 能被编译并运行。
3. `liteim_tests` 能被 CTest 发现并执行。
4. GoogleTest 能跑通一个最小 smoke case。

这一步不实现网络、协议、Reactor、线程池、MySQL、Redis 或 Qt。  
这些模块会在后续 Step 中按实际需要逐步创建目录和代码。

## 2. 新增文件

```text
LiteIM/
├── CMakeLists.txt
├── server/
│   ├── CMakeLists.txt
│   └── main.cpp
└── tests/
    ├── CMakeLists.txt
    └── test_main.cpp
```

## 3. 代码说明

### 根 CMakeLists.txt

根 `CMakeLists.txt` 做项目级配置：

- 设置项目名、版本和语言。
- 要求 C++17。
- 关闭 compiler extension，避免依赖非标准 C++。
- 开启 CTest。
- 使用 `FetchContent` 拉取 GoogleTest v1.14.0。
- 加载 CMake 自带 `GoogleTest` 模块。
- 加载 `server/` 和 `tests/` 两个子目录。

这一步没有 `src/` 目录，因为还没有公共库代码。等 Step 2 开始写 `base` 模块时，再创建 `include/liteim/base/` 和 `src/base/`。

### server/CMakeLists.txt

`server/CMakeLists.txt` 定义 `liteim_server`：

- 输入文件是 `main.cpp`。
- 编译标准是 C++17。
- 在 GCC / Clang 下开启 `-Wall -Wextra -Wpedantic`。

### server/main.cpp

`server/main.cpp` 目前只打印：

```text
LiteIM server scaffold is running.
```

它只是 smoke target，用来验证工程能编译和运行。真正的监听 socket、epoll 和 Reactor 会在后续 Step 加入。

### tests/CMakeLists.txt

`tests/CMakeLists.txt` 定义 `liteim_tests`，链接 `GTest::gtest_main`，并通过：

```cmake
gtest_discover_tests(liteim_tests)
```

把 GoogleTest 用例自动注册给 CTest。

### tests/test_main.cpp

`test_main.cpp` 不再自己写 `int main()`。

它只写一个 GoogleTest case：

```cpp
TEST(SmokeTest, GoogleTestWorks)
```

`GTest::gtest_main` 会提供测试入口。这个 smoke case 做两件事：

- `static_assert(__cplusplus >= 201703L)` 验证当前确实按 C++17 或更高标准编译。
- `EXPECT_EQ(1 + 1, 2)` 验证 GoogleTest 断言和 CTest 发现链路可用。

## Step 级作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

Step 1 还没有公共 `.hpp`，真实场景是让工程第一次具备“能配置、能编译、能运行 server、能被 CTest 发现测试”的基本闭环。后续每个 Step 都挂在这条构建和测试链路上。

### 2. 上下层调用连接

```text
开发者命令
    -> 根 CMakeLists.txt
    -> FetchContent GoogleTest
    -> add_subdirectory(server)
    -> add_subdirectory(tests)
    -> liteim_server / liteim_tests
    -> CTest / server smoke
```

当前根 CMake 后续已经加入 `src/` 和 `spdlog`，但 Step 1 的核心连接仍然是根工程把 server target、test target、GoogleTest 和 CTest 串起来。

### 3. 整体运行链路

1. `cmake -S . -B build` 读取 [根 CMakeLists.txt](../CMakeLists.txt#L1)，设置 C++17 并打开 `enable_testing()`。
2. CMake 通过 FetchContent 准备 GoogleTest target。
3. 根工程进入 `server/`，生成 [liteim_server](../server/CMakeLists.txt#L1)。
4. 根工程进入 `tests/`，生成 [liteim_tests](../tests/CMakeLists.txt#L1)。
5. `gtest_discover_tests()` 把 GoogleTest 用例注册给 CTest。
6. `cmake --build build` 编译 server 和 tests。
7. `ctest --test-dir build` 执行发现到的测试；server smoke 单独运行可执行文件验证入口存在。

### 4. 自身内部运行流程

整体可以看成 4 步：configure、build、test discovery、server smoke。

核心结构职责：

- [CMakeLists.txt](../CMakeLists.txt#L15) 负责启用测试、声明依赖、进入子目录。
- [server/CMakeLists.txt](../server/CMakeLists.txt#L1) 负责生成服务端入口。
- [tests/CMakeLists.txt](../tests/CMakeLists.txt#L49) 负责把 GoogleTest 用例注册给 CTest。
- `server/main.cpp` 在 Step 1 只是最小入口，后续才接入真实网络 server。

核心函数或命令流程：

```text
configure
    -> evaluate root CMake
    -> fetch googletest
    -> generate build files

build
    -> compile server/main.cpp
    -> compile tests/test_main.cpp
    -> link liteim_server / liteim_tests

test discovery
    -> run liteim_tests --gtest_list_tests
    -> register discovered cases into CTest

server smoke
    -> run liteim_server
    -> process exits normally
```

### 5. 该项目代码在实际应用中的具体数据例子

后续增加 `SessionTest.ReceivesCompletePacket` 这种用例时，测试入口已经准备好：`liteim_tests` 链接 GoogleTest，CTest 负责发现和执行。比如 `Packet.seq_id=7` 的私聊请求在 Step 14 之后会进入 `Session` 测试；Step 1 只需要保证 `tests/CMakeLists.txt` 能把测试源编进同一个目标，`ctest --test-dir build --output-on-failure` 能统一报告结果。

## 4. 测试验证

运行：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

验证点：

1. `cmake -S . -B build` 成功，说明 CMake 配置无误。
2. `cmake --build build` 成功，说明 `liteim_server` 和 `liteim_tests` 都能编译。
3. `./build/server/liteim_server` 输出启动信息，说明 server target 可运行。
4. `ctest --test-dir build --output-on-failure` 发现并执行 `SmokeTest.GoogleTestWorks`，说明 GoogleTest 已接入 CTest。

## 5. 为什么从 Step 1 就用 GoogleTest

不自研测试框架，原因是：

1. GoogleTest 是 C++ 后端常见工业标准，面试官和 CI 都熟悉。
2. 自研小框架很难自然支持 fixture、参数化测试、death test 和 gMock。
3. GoogleTest 能和 CTest 原生集成，后续 GitHub Actions 只需要跑 `ctest --output-on-failure`。
4. 早接入可以避免后期几十个测试文件从自研框架迁移到 GoogleTest 的成本。
5. 后续 Step 文档可以直接写 `TEST(BufferTest, AppendIncreasesReadableBytes)` 这类用例名，教程和代码能一一对应。

## 6. 面试时怎么讲

这一 Step 不是高性能技术点，而是工程起点。

可以这样讲：

> 我没有一开始就堆完整目录，而是先建立最小 CMake 工程，保证服务端 target 和测试 target 能独立构建和运行。后续每个模块会在需要时再创建目录和 CMake target，这样项目演进路径更清晰，也方便每一步配套测试和文档。

## 7. 面试常见追问

**为什么不一开始创建完整目录？**

因为这是教学式项目。提前创建空目录没有实际行为，反而会让 Step 边界不清楚。

**为什么 Step 1 就要有 tests？**

因为后续每个模块都要靠测试验收。先把 CTest 和 GoogleTest 接好，后面新增协议、Buffer、Reactor 时可以直接加入 `TEST` 用例。

**为什么不自己写一个简单测试框架？**

自研框架虽然一开始简单，但后面很难支持 fixture、参数化测试、death test、gMock 和 CI 生态。这个项目目标是实习简历项目，用 GoogleTest 更贴近工程实践。
