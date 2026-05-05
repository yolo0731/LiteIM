# LiteIM Project Layout

Step 0 后的目录结构是新路线的空骨架。

```text
LiteIM/
├── include/liteim/
│   ├── base/
│   ├── net/
│   ├── protocol/
│   ├── concurrency/
│   ├── timer/
│   ├── storage/
│   ├── cache/
│   └── service/
├── src/
│   ├── base/
│   ├── net/
│   ├── protocol/
│   ├── concurrency/
│   ├── timer/
│   ├── storage/
│   ├── cache/
│   └── service/
├── server/
├── client_cli/
├── client_qt/
├── bench/
├── tests/
├── scripts/
├── docker/
├── docs/
├── tutorials/
└── .github/workflows/
```

规则：

- 头文件放在 `include/liteim/<module>/`。
- 库实现放在 `src/<module>/`。
- 服务端入口放在 `server/`。
- CLI 客户端放在 `client_cli/`。
- Qt 客户端放在 `client_qt/`。
- 压测工具放在 `bench/`。
- 不向 `server/net` 或 `server/protocol` 增加头文件。
