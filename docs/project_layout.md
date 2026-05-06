# LiteIM Project Layout

Step 8 后只保留当前步骤真正需要的最小文件，不提前提交未来目录。

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
│   └── project_layout.md
├── include/
│   └── liteim/
│       ├── base/
│       │   ├── Config.hpp
│       │   ├── ErrorCode.hpp
│       │   ├── Logger.hpp
│       │   ├── Status.hpp
│       │   └── Timestamp.hpp
│       ├── net/
│       │   ├── Buffer.hpp
│       │   └── SocketUtil.hpp
│       └── protocol/
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
│   │   ├── Buffer.cpp
│   │   ├── SocketUtil.cpp
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
│   │   ├── buffer_test.cpp
│   │   └── socket_util_test.cpp
│   ├── protocol/
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
    └── step08_socket_util.md
```

目标结构会按 Step 逐步创建，而不是在 Step 0 到 Step 8 一次性建立。

最终约定：

- 头文件放在 `include/liteim/<module>/`。
- 库实现放在 `src/<module>/`。
- 基础公共模块从 Step 2 开始放在 `include/liteim/base/` 和 `src/base/`。
- 协议模块从 Step 3 开始放在 `include/liteim/protocol/` 和 `src/protocol/`，Step 4 在同一模块内加入 `Packet` 编解码，Step 5 加入 `TlvCodec`，Step 6 加入 `FrameDecoder`。
- 网络模块从 Step 7 开始放在 `include/liteim/net/` 和 `src/net/`，Step 7 加入 socket-agnostic `Buffer`，Step 8 加入 Linux socket 工具函数 `SocketUtil`。
- 服务端入口放在 `server/`。
- CLI 客户端放在 `client_cli/`。
- Qt 客户端放在 `client_qt/`。
- 压测工具放在 `bench/`。
- 不向 `server/net` 或 `server/protocol` 增加头文件。

这些目录将在真正需要它们的 Step 中创建。当前 Step 8 只在 `net` 模块中补充 `SocketUtil.hpp`、`SocketUtil.cpp` 和 `socket_util_test.cpp`，因为它只需要验证非阻塞 fd、常用 socket option、无效 fd 错误返回和重复关闭保护。
