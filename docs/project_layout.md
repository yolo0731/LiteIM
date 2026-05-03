# LiteIM 项目目录结构说明

本文档记录 LiteIM 当前采用的 C++ 项目目录布局，以及为什么要把 `.hpp` 和 `.cpp` 分开。

## 当前目录布局

```text
LiteIM/
├── include/
│   └── liteim/
│       ├── net/
│       │   ├── Acceptor.hpp
│       │   ├── Buffer.hpp
│       │   ├── Channel.hpp
│       │   ├── Epoller.hpp
│       │   ├── EventLoop.hpp
│       │   └── SocketUtil.hpp
│       └── protocol/
│           ├── FrameDecoder.hpp
│           ├── MessageType.hpp
│           └── Packet.hpp
├── src/
│   ├── CMakeLists.txt
│   ├── net/
│   │   ├── Acceptor.cpp
│   │   ├── Buffer.cpp
│   │   ├── Channel.cpp
│   │   ├── Epoller.cpp
│   │   ├── EventLoop.cpp
│   │   └── SocketUtil.cpp
│   └── protocol/
│       ├── FrameDecoder.cpp
│       └── Packet.cpp
├── server/
│   ├── CMakeLists.txt
│   └── main.cpp
├── tests/
├── docs/
├── tutorials/
├── client_qt/
└── sql/
```

## 为什么拆成 include 和 src

更成熟的 C++ 项目通常会把头文件和实现文件分开：

- `include/` 表示“其他 target 可以包含的接口”。
- `src/` 表示“本项目库的实现细节”。
- `server/` 只保留可执行程序入口，不再混放库代码。

这样做的好处：

- include 路径更稳定，测试、server、未来 Qt 客户端都可以使用统一写法。
- 模块边界更清楚，协议层、网络层和可执行入口不会混在一起。
- 后续如果把 `liteim_net`、`liteim_protocol` 拆成单独库，结构更自然。
- 面试时可以讲清楚“接口”和“实现”的边界。

## include 路径约定

项目代码统一使用带项目名前缀的 include：

```cpp
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/net/Buffer.hpp"
#include "liteim/net/SocketUtil.hpp"
#include "liteim/net/Acceptor.hpp"
#include "liteim/net/Epoller.hpp"
#include "liteim/net/Channel.hpp"
#include "liteim/net/EventLoop.hpp"
```

不要再使用旧写法：

```cpp
#include "protocol/Packet.hpp"
#include "net/Buffer.hpp"
```

原因是旧写法依赖某个源码目录刚好被加入 include path，后续模块多了以后容易冲突；新写法以 `include/` 为统一根目录，更接近真实工程。

## CMake 目标划分

当前 CMake 结构：

```text
src/CMakeLists.txt
  ├── liteim_protocol
  └── liteim_net

server/CMakeLists.txt
  └── liteim_server

tests/CMakeLists.txt
  └── liteim_tests
```

`liteim_protocol` 和 `liteim_net` 都通过：

```cmake
target_include_directories(... PUBLIC ${PROJECT_SOURCE_DIR}/include)
```

把 `include/` 暴露给依赖它们的 target。

因此：

- `liteim_server` 链接库后能包含 `liteim/...` 头文件。
- `liteim_tests` 链接库后也能包含 `liteim/...` 头文件。

## 后续新增文件放哪里

网络层头文件：

```text
include/liteim/net/
```

网络层实现：

```text
src/net/
```

协议层头文件：

```text
include/liteim/protocol/
```

协议层实现：

```text
src/protocol/
```

服务端入口和进程级组合：

```text
server/
```

业务层、存储层、定时器层后续也按同样规则扩展：

```text
include/liteim/service/
src/service/

include/liteim/storage/
src/storage/

include/liteim/timer/
src/timer/
```

## 和后续 Step 的关系

这次目录重构本身不改变功能。后续 Step 会继续在这个布局下补网络行为。

Step 7 已经实现 `Epoller`，放置为：

```text
include/liteim/net/Epoller.hpp
src/net/Epoller.cpp
```

Step 8 已经实现 `EventLoop`：

```text
include/liteim/net/EventLoop.hpp
src/net/EventLoop.cpp
```

Step 9 实现 `Channel`：

```text
include/liteim/net/Channel.hpp
src/net/Channel.cpp
```

注意：Step 7 已经在 `src/net/Channel.cpp` 中补了少量 `Channel` 状态方法，用于支撑 `Epoller` 测试；Step 8 又补了 `Channel::handleEvent()` 的基础回调分发。Step 9 已经补齐 `Channel::enableReading()`、`enableWriting()`、`disableWriting()`、`disableAll()` 到 `EventLoop` 的自动更新逻辑。

Step 10 已经实现 `Acceptor`：

```text
include/liteim/net/Acceptor.hpp
src/net/Acceptor.cpp
```

`Acceptor` 仍然属于 `net` 模块，因为它只负责监听 socket、accept 新连接和通知上层 callback，不包含登录、聊天、存储等业务逻辑。

每一步仍然要遵守：只实现当前 Step，编译通过，测试通过，文档同步更新。
