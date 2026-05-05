# LiteIM Project Layout

Step 1 后只保留当前步骤真正需要的最小文件，不提前提交未来目录。

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
├── server/
│   ├── CMakeLists.txt
│   └── main.cpp
├── tests/
│   ├── CMakeLists.txt
│   └── test_main.cpp
└── tutorials/
    ├── README.md
    ├── step00_reset.md
    └── step01_project_init.md
```

目标结构会按 Step 逐步创建，而不是在 Step 0 或 Step 1 一次性建立。

最终约定：

- 头文件放在 `include/liteim/<module>/`。
- 库实现放在 `src/<module>/`。
- 服务端入口放在 `server/`。
- CLI 客户端放在 `client_cli/`。
- Qt 客户端放在 `client_qt/`。
- 压测工具放在 `bench/`。
- 不向 `server/net` 或 `server/protocol` 增加头文件。

这些目录将在真正需要它们的 Step 中创建。当前 Step 1 只创建 `server/` 和 `tests/`，因为它只需要验证最小 server target 和 test target。
