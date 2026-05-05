# LiteIM Project Layout

Step 2 后只保留当前步骤真正需要的最小文件，不提前提交未来目录。

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
│       └── base/
│           ├── Config.hpp
│           ├── ErrorCode.hpp
│           ├── Logger.hpp
│           ├── Status.hpp
│           └── Timestamp.hpp
├── server/
│   ├── CMakeLists.txt
│   └── main.cpp
├── src/
│   ├── CMakeLists.txt
│   └── base/
│       ├── CMakeLists.txt
│       ├── Config.cpp
│       ├── ErrorCode.cpp
│       ├── Logger.cpp
│       ├── Status.cpp
│       └── Timestamp.cpp
├── tests/
│   ├── base/
│   │   ├── config_test.cpp
│   │   ├── error_code_test.cpp
│   │   ├── logger_test.cpp
│   │   └── timestamp_test.cpp
│   ├── CMakeLists.txt
│   └── test_main.cpp
└── tutorials/
    ├── README.md
    ├── step00_reset.md
    ├── step01_project_init.md
    └── step02_base.md
```

目标结构会按 Step 逐步创建，而不是在 Step 0、Step 1 或 Step 2 一次性建立。

最终约定：

- 头文件放在 `include/liteim/<module>/`。
- 库实现放在 `src/<module>/`。
- 基础公共模块从 Step 2 开始放在 `include/liteim/base/` 和 `src/base/`。
- 服务端入口放在 `server/`。
- CLI 客户端放在 `client_cli/`。
- Qt 客户端放在 `client_qt/`。
- 压测工具放在 `bench/`。
- 不向 `server/net` 或 `server/protocol` 增加头文件。

这些目录将在真正需要它们的 Step 中创建。当前 Step 2 只新增 `base` 模块目录，因为它只需要验证公共配置、日志、错误码、状态和时间戳能力。
