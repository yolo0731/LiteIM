# LiteIM Project Layout

Step 4 后只保留当前步骤真正需要的最小文件，不提前提交未来目录。

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
│       └── protocol/
│           ├── MessageType.hpp
│           ├── Packet.hpp
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
│   └── protocol/
│       ├── CMakeLists.txt
│       ├── MessageType.cpp
│       ├── Packet.cpp
│       └── Tlv.cpp
├── tests/
│   ├── base/
│   │   ├── config_test.cpp
│   │   ├── error_code_test.cpp
│   │   ├── logger_test.cpp
│   │   └── timestamp_test.cpp
│   ├── protocol/
│   │   ├── message_type_test.cpp
│   │   ├── packet_test.cpp
│   │   └── tlv_type_test.cpp
│   ├── CMakeLists.txt
│   └── test_main.cpp
└── tutorials/
    ├── README.md
    ├── step00_reset.md
    ├── step01_project_init.md
    ├── step02_base.md
    ├── step03_protocol_types.md
    └── step04_packet.md
```

目标结构会按 Step 逐步创建，而不是在 Step 0、Step 1、Step 2、Step 3 或 Step 4 一次性建立。

最终约定：

- 头文件放在 `include/liteim/<module>/`。
- 库实现放在 `src/<module>/`。
- 基础公共模块从 Step 2 开始放在 `include/liteim/base/` 和 `src/base/`。
- 协议模块从 Step 3 开始放在 `include/liteim/protocol/` 和 `src/protocol/`，Step 4 在同一模块内加入 `Packet` 编解码。
- 服务端入口放在 `server/`。
- CLI 客户端放在 `client_cli/`。
- Qt 客户端放在 `client_qt/`。
- 压测工具放在 `bench/`。
- 不向 `server/net` 或 `server/protocol` 增加头文件。

这些目录将在真正需要它们的 Step 中创建。当前 Step 4 只在已有 `protocol` 模块中新增 `Packet.hpp`、`Packet.cpp` 和 `packet_test.cpp`，因为它只需要验证 fixed header 编解码和校验。
