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
│       │   ├── Session.hpp
│       │   ├── SocketUtil.hpp
│       │   └── TcpServer.hpp
│       ├── protocol/
│       │   ├── FrameDecoder.hpp
│       │   ├── MessageType.hpp
│       │   └── Packet.hpp
│       ├── service/
│       │   └── MessageRouter.hpp
│       └── storage/
│           ├── ICache.hpp
│           ├── IStorage.hpp
│           ├── NullCache.hpp
│           └── StorageTypes.hpp
├── src/
│   ├── CMakeLists.txt
│   ├── net/
│   │   ├── Acceptor.cpp
│   │   ├── Buffer.cpp
│   │   ├── Channel.cpp
│   │   ├── Epoller.cpp
│   │   ├── EventLoop.cpp
│   │   ├── Session.cpp
│   │   ├── SocketUtil.cpp
│   │   └── TcpServer.cpp
│   ├── protocol/
│   │   ├── FrameDecoder.cpp
│   │   └── Packet.cpp
│   ├── service/
│   │   └── MessageRouter.cpp
│   └── storage/
│       └── NullCache.cpp
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
#include "liteim/net/Session.hpp"
#include "liteim/net/TcpServer.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/storage/IStorage.hpp"
#include "liteim/storage/NullCache.hpp"
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
  ├── liteim_net
  ├── liteim_service
  └── liteim_storage

server/CMakeLists.txt
  └── liteim_server

tests/CMakeLists.txt
  └── liteim_tests
```

`liteim_protocol`、`liteim_net`、`liteim_service` 和 `liteim_storage` 都通过：

```cmake
target_include_directories(... PUBLIC ${PROJECT_SOURCE_DIR}/include)
```

把 `include/` 暴露给依赖它们的 target。

因此：

- `liteim_server` 链接库后能包含 `liteim/...` 头文件。
- `liteim_tests` 链接库后也能包含 `liteim/...` 头文件。

`liteim_service` 链接 `liteim_net` 和 `liteim_protocol`。当前依赖方向是：

```text
service -> net -> protocol
```

网络层不反向依赖业务层，这样 `Session`、`TcpServer` 和 `MessageRouter` 都能分别测试。

`liteim_storage` 当前只提供存储/缓存接口和 `NullCache` no-op 实现，不链接 SQLite。后续 `SQLiteStorage` 会在这个模块里实现。

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

Step 11 已经实现 `Session`：

```text
include/liteim/net/Session.hpp
src/net/Session.cpp
```

`Session` 仍然属于 `net` 模块，因为它负责单个连接 fd 的非阻塞读写、`FrameDecoder` 解包、输出缓冲和关闭清理。它不处理登录、私聊、存储等业务语义，完整服务端组合会在后续 `TcpServer` / `MessageRouter` 中完成。

Step 12 已经实现 `TcpServer`：

```text
include/liteim/net/TcpServer.hpp
src/net/TcpServer.cpp
```

`TcpServer` 仍然属于 `net` 模块，因为它组合 `EventLoop`、`Acceptor` 和 `Session`，负责连接集合、按 session/user 发送和网络层优雅关闭。它不实现登录、私聊、群聊或数据库逻辑，这些仍然属于后续 `MessageRouter`、service 和 storage 模块。

Step 13 已经实现 `MessageRouter`：

```text
include/liteim/service/MessageRouter.hpp
src/service/MessageRouter.cpp
```

`MessageRouter` 属于 `service` 模块，因为它根据 `Packet.header.msg_type` 做业务分发。当前只支持 `HEARTBEAT_REQ -> HEARTBEAT_RESP`，未知类型返回 `ERROR_RESP`。它不直接操作 fd，也不管理连接生命周期，只通过 `Session::sendPacket()` 把响应交回网络层。

Step 14 已经实现 storage 接口层：

```text
include/liteim/storage/StorageTypes.hpp
include/liteim/storage/IStorage.hpp
include/liteim/storage/ICache.hpp
include/liteim/storage/NullCache.hpp
src/storage/NullCache.cpp
```

`storage` 模块当前只定义抽象和 no-op 缓存，不实现 SQLite。后续业务层应该依赖 `IStorage` / `ICache`，真实 SQLite 访问留给 Step 15 的 `SQLiteStorage`。

每一步仍然要遵守：只实现当前 Step，编译通过，测试通过，文档同步更新。
