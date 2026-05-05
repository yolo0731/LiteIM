# Step 0：重置 LiteIM 文件夹

## 1. 概念

Step 0 不是实现功能，而是把旧路线清理掉。

之前的文件夹里混有旧网络层、旧业务层、SQLite、`InMemoryStorage`、旧教程和 build 产物。继续在这些文件上改，容易把旧的单 Reactor / 同步存储路线带进新项目。

所以 Step 0 的目标是：

- 删除旧实现。
- 保留 Git 仓库、许可证和规划文档。
- 不提前创建未来步骤才会用到的空目录。
- 明确后续从 Step 1 开始逐步手写。

## 2. 为什么不保留 .gitkeep

`.gitkeep` 不是 Git 的必需文件。Git 不跟踪空目录，所以很多项目会放 `.gitkeep` 作为占位文件。

但 LiteIM 是一步一步教学式推进的项目。提前提交一堆空目录会让 Step 边界不清楚：读者会以为这些目录已经属于当前 Step 的成果。

因此本项目采用更严格的方式：哪个 Step 需要哪个目录，就在那个 Step 创建。

## 3. 本 Step 做了什么

删除：

- 旧 `include/`、`src/`、`server/`。
- 旧 `tests/`。
- 旧 `tutorials/`。
- 旧 `docs/`。
- 旧 `client_qt/`。
- 旧 `sql/`。
- 旧 `build/`。
- 未来目录里的 `.gitkeep` 占位文件。

保留：

- `.gitignore`。
- `LICENSE`。
- `CMakeLists.txt` 空骨架。
- `README.md`。
- `task_plan.md`、`findings.md`、`progress.md`。
- `docs/architecture.md`、`docs/project_layout.md`。
- `tutorials/README.md`、`tutorials/step00_reset.md`。

## 4. 测试验证

Step 0 没有业务代码，验证重点是：

- CMake 空骨架能 configure。
- build 命令不会因为旧 target 残留失败。
- CTest 当前没有测试，但命令能正常退出。
- 不存在 `.gitkeep`。
- 旧 SQLite / InMemoryStorage 主线文件不再存在。

运行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
find . -name .gitkeep
rg -n "SQLiteStorage|step15_sqlite|InMemoryStorage|server/net|server/protocol" .
```

## 5. 面试时怎么讲

这个 Step 不作为简历技术点，只是工程管理上的起点。

可以说：我在正式实现前先统一了项目路线，删除了旧的同步存储和旧目录结构，准备按 Step 逐步建立 `include/liteim/<module>` + `src/<module>` 的分层布局，避免还没实现功能就提前堆目录。

## 6. 下一步

Step 1：初始化 CMake 工程，创建最小 `liteim_server` 和 `liteim_tests`。
