# LiteIM Progress

## 2026-05-05 Step 0 Reset

本次进度是 `Step 0: reset workspace`，目标是把 LiteIM 文件夹清理成可以从零教学推进的最小起点。

## 已完成

- 删除旧源码目录：`include/`、`src/`、`server/`。
- 删除旧测试目录：`tests/`。
- 删除旧教程目录：`tutorials/`。
- 删除旧文档目录：`docs/`。
- 删除旧 Qt 临时目录：`client_qt/`。
- 删除旧 SQL 目录：`sql/`。
- 删除旧构建产物：`build/`。
- 删除空的 `.codex` 临时文件。
- 删除未来 Step 才会使用的空目录和 `.gitkeep`。
- 将根 `CMakeLists.txt` 改成 Step 0 空 CMake 骨架。
- 重写 README、task_plan、findings、progress。
- 新增 `docs/architecture.md`、`docs/project_layout.md`、`tutorials/README.md`、`tutorials/step00_reset.md`。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md`，加入 Step 0 说明，并把 Qt 描述统一为常见 IM 三栏布局。

## 当前状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| Step 0 清理 | done | 旧路线代码、测试、教程和 build 产物已删除。 |
| Step 0 最小起点 | done | 不提前保留未来目录，也不保留 `.gitkeep`。 |
| Step 0 文档 | done | README、计划文件、docs 和 tutorial 已更新。 |
| Step 0 验证 | done | CMake、CTest、`.gitkeep` 检查和旧文件名检查已通过。 |
| Step 0 commit | pending | 提交信息：`chore: keep LiteIM step0 minimal`。 |
| Step 1 | pending | 下一步创建真正可构建的 server/test target。 |

## 下一步

验证 Step 0：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
find . -name .gitkeep
rg -n "SQLiteStorage|step15_sqlite|InMemoryStorage|server/net|server/protocol" .
```

然后进入 Step 1：`init: create LiteIM high performance project structure`。

## Step 0 最小起点验证结果

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `ctest --test-dir build --output-on-failure`：通过，当前没有测试用例，符合 Step 0 预期。
- `find . -name .gitkeep`：无输出，说明没有 `.gitkeep` 残留。
- 旧路线文件名检查：无 `SQLite`、`InMemory`、`step15`、`server/net`、`server/protocol` 文件路径残留。
