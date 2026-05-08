# LiteIM Project Layout

Step 16 后只保留当前步骤真正需要的最小文件，不提前提交未来目录。

当前结构：

```text
LiteIM/
├── .gitignore
├── CMakeLists.txt
├── LICENSE
├── README.md
├── task_plan.md
├── findings.md
├── progress.md
├── docs/
│   ├── architecture.md
│   ├── project_layout.md
│   └── roadmap.md
├── include/
│   └── liteim/
│       ├── base/
│       │   ├── Config.hpp
│       │   ├── ErrorCode.hpp
│       │   ├── Logger.hpp
│       │   ├── Status.hpp
│       │   ├── Types.hpp
│       │   └── Timestamp.hpp
│       ├── net/
│       │   ├── Buffer.hpp
│       │   ├── Acceptor.hpp
│       │   ├── Channel.hpp
│       │   ├── Epoller.hpp
│       │   ├── EventLoop.hpp
│       │   ├── EventLoopThread.hpp
│       │   ├── EventLoopThreadPool.hpp
│       │   ├── Session.hpp
│       │   ├── SocketUtil.hpp
│       │   ├── TcpServer.hpp
│       │   └── UniqueFd.hpp
│       └── protocol/
│           ├── ByteOrder.hpp
│           ├── FrameDecoder.hpp
│           ├── MessageType.hpp
│           ├── Packet.hpp
│           ├── TlvCodec.hpp
│           └── Tlv.hpp
├── server/
│   ├── CMakeLists.txt
│   └── main.cpp
├── src/
│   ├── CMakeLists.txt
│   ├── base/
│   │   ├── CMakeLists.txt
│   │   ├── Config.cpp
│   │   ├── ErrorCode.cpp
│   │   ├── Logger.cpp
│   │   ├── Status.cpp
│   │   └── Timestamp.cpp
│   ├── net/
│   │   ├── Acceptor.cpp
│   │   ├── Buffer.cpp
│   │   ├── Channel.cpp
│   │   ├── EventLoop.cpp
│   │   ├── EventLoopThread.cpp
│   │   ├── EventLoopThreadPool.cpp
│   │   ├── Epoller.cpp
│   │   ├── Session.cpp
│   │   ├── SocketUtil.cpp
│   │   ├── TcpServer.cpp
│   │   ├── UniqueFd.cpp
│   │   └── CMakeLists.txt
│   └── protocol/
│       ├── CMakeLists.txt
│       ├── FrameDecoder.cpp
│       ├── MessageType.cpp
│       ├── Packet.cpp
│       ├── TlvCodec.cpp
│       └── Tlv.cpp
├── tests/
│   ├── base/
│   │   ├── config_test.cpp
│   │   ├── error_code_test.cpp
│   │   ├── logger_test.cpp
│   │   └── timestamp_test.cpp
│   ├── net/
│   │   ├── acceptor_header_test.cpp
│   │   ├── acceptor_test.cpp
│   │   ├── buffer_test.cpp
│   │   ├── channel_header_test.cpp
│   │   ├── channel_test.cpp
│   │   ├── epoller_header_test.cpp
│   │   ├── epoller_test.cpp
│   │   ├── event_loop_header_test.cpp
│   │   ├── event_loop_test.cpp
│   │   ├── event_loop_thread_header_test.cpp
│   │   ├── event_loop_thread_pool_header_test.cpp
│   │   ├── event_loop_thread_test.cpp
│   │   ├── event_loop_thread_pool_test.cpp
│   │   ├── session_header_test.cpp
│   │   ├── session_test.cpp
│   │   ├── socket_util_test.cpp
│   │   ├── tcp_server_header_test.cpp
│   │   ├── tcp_server_test.cpp
│   │   └── unique_fd_test.cpp
│   ├── protocol/
│   │   ├── byte_order_test.cpp
│   │   ├── frame_decoder_test.cpp
│   │   ├── message_type_test.cpp
│   │   ├── packet_test.cpp
│   │   ├── tlv_codec_test.cpp
│   │   └── tlv_type_test.cpp
│   ├── CMakeLists.txt
│   └── test_main.cpp
└── tutorials/
    ├── README.md
    ├── step00_reset.md
    ├── step01_project_init.md
    ├── step02_base.md
    ├── step03_protocol_types.md
    ├── step04_packet.md
    ├── step05_tlv_codec.md
    ├── step06_frame_decoder.md
    ├── step07_buffer.md
    ├── step08_socket_util.md
    ├── step09_reactor_interfaces.md
    ├── step10_epoller.md
    ├── step11_channel.md
    ├── step12_event_loop.md
    ├── step13_acceptor.md
    ├── step14_session.md
    ├── step15_event_loop_thread_pool.md
    └── step16_tcp_server.md
```

目标结构会按 Step 逐步创建，而不是在 Step 0 一次性建立。

最终约定：

- 头文件放在 `include/liteim/<module>/`。
- 库实现放在 `src/<module>/`。
- 基础公共模块从 Step 2 开始放在 `include/liteim/base/` 和 `src/base/`；`Types.hpp` 放只含轻量别名的公共类型，例如 `Byte` / `Bytes`。
- 协议模块从 Step 3 开始放在 `include/liteim/protocol/` 和 `src/protocol/`，Step 4 在同一模块内加入 `Packet` 编解码，Step 5 加入 `TlvCodec`，Step 6 加入 `FrameDecoder`；Step 16 前清理新增 `ByteOrder.hpp`，让 Packet/TLV 共享大端读写 helper。
- 网络模块从 Step 7 开始放在 `include/liteim/net/` 和 `src/net/`，Step 7 加入 socket-agnostic `Buffer`，Step 8 加入 Linux socket 工具函数 `SocketUtil`，Step 9 加入 Reactor 核心接口 `Channel` / `Epoller` / `EventLoop`，Step 10 实现 `Epoller` 和最小 `Channel` 状态函数，Step 11 实现 `Channel` 回调分发和 `EventLoop` 更新桥接，Step 12 实现 `EventLoop` 阻塞循环和 `eventfd` 任务投递，Step 13 实现非阻塞监听器 `Acceptor`，Step 13 review hardening 补充 `UniqueFd`、`Channel::tie()` 和 Acceptor loop-thread close 清理，Step 14 实现单连接 `Session`，Step 15 实现 `EventLoopThread` / `EventLoopThreadPool`；Step 16 前清理让 `Epoller` 校验 `Channel` owner loop，并让 Acceptor callback 直接接收 `UniqueFd`；Step 16 加入多 Reactor `TcpServer`。
- 服务端入口放在 `server/`。
- CLI 客户端放在 `client_cli/`。
- Qt 客户端放在 `client_qt/`。
- 压测工具放在 `bench/`。
- 不向 `server/net` 或 `server/protocol` 增加头文件。

代码整洁约定：

- 原始二进制统一用 `liteim::Byte` / `liteim::Bytes`。
- 文本数据用 `std::string`，公共接口不主动引入 `std::string_view`。
- 保留必要的线程边界函数，例如 `close()` 投递到 `closeInLoop()`；删除没有边界意义的一次性私有包装函数。

这些目录将在真正需要它们的 Step 中创建。当前 Step 16 已在 `net` 模块中补充 `TcpServer.hpp`、`TcpServer.cpp`、`tcp_server_header_test.cpp`、`tcp_server_test.cpp` 和 `step16_tcp_server.md`。本 Step 只实现多 Reactor echo server，不创建业务线程池、MySQL、Redis、CLI、Qt、benchmark 或 Docker 目录。
