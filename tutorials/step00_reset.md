# Step 0：重置 LiteIM 文件夹

## 1. 概念

Step 0 不是实现功能，而是把旧路线清理掉。

之前的文件夹里混有旧网络层、旧业务层、SQLite、`InMemoryStorage`、旧教程和 build 产物。继续在这些文件上改，容易把旧的单 Reactor / 同步存储路线带进新项目。

所以 Step 0 的目标是：

- 删除旧实现。
- 保留 Git 仓库、许可证和规划文档。
- 重建新路线目录骨架。
- 明确后续从 Step 1 开始逐步手写。

## 2. 本 Step 做了什么

删除：

- 旧 `include/`、`src/`、`server/`。
- 旧 `tests/`。
- 旧 `tutorials/`。
- 旧 `docs/`。
- 旧 `client_qt/`。
- 旧 `sql/`。
- 旧 `build/`。

重建：

- `include/liteim/{base,net,protocol,concurrency,timer,storage,cache,service}/`。
- `src/{base,net,protocol,concurrency,timer,storage,cache,service}/`。
- `server/`、`client_cli/`、`client_qt/`、`bench/`、`tests/`。
- `scripts/`、`docker/`、`docs/`、`tutorials/`、`.github/workflows/`。

更新：

- `README.md`。
- `task_plan.md`。
- `findings.md`。
- `progress.md`。
- `docs/architecture.md`。
- `docs/project_layout.md`。
- `tutorials/README.md`。

## 3. 测试验证

Step 0 没有业务代码，验证重点是：

- CMake 空骨架能 configure。
- build 命令不会因为旧 target 残留失败。
- CTest 当前没有测试，但命令能正常退出。
- 旧 SQLite / InMemoryStorage 主线文件不再存在。

运行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "SQLiteStorage|step15_sqlite|InMemoryStorage|server/net|server/protocol" .
```

## 4. 面试时怎么讲

这个 Step 不作为简历技术点，只是工程管理上的起点。

可以说：我在正式实现前先统一了项目路线，删除了旧的同步存储和旧目录结构，重新设计为 `include/liteim/<module>` + `src/<module>` 的分层布局，为后续 Reactor、协议、业务线程池、MySQL/Redis 和 Qt 客户端留出清晰边界。

## 5. 下一步

Step 1：初始化 CMake 工程，添加最小 `liteim_server` 和 `liteim_tests`。
