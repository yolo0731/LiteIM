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
- `tutorials/step00_reset.md`。

## Step 级作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

Step 0 没有新增 `.hpp`，它的真实场景是把旧单 Reactor、旧同步存储、SQLite / `InMemoryStorage` 主线和历史构建产物清掉，给 `/home/yolo/jianli/PROJECT_MEMORY.md` 里的高性能路线留下一个不会误导后续 Step 的干净起点。

### 2. 上下层调用连接

```text
PROJECT_MEMORY.md / task_plan.md
    -> Step 0 reset
    -> 最小 LiteIM 根目录
    -> Step 1 CMake / server / tests
    -> Step 2+ base / protocol / net 模块逐步重建
```

这不是运行时调用链，而是工程层调用链：上层是长期路线和进度记忆，下层是后续 Step 可以继续接上的最小工程边界。

### 3. 整体运行链路

1. 先读取总路线，确认旧文件是否属于旧单 Reactor / 同步存储路线。
2. 扫描旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、旧教程和 `build/` 产物。
3. 删除会让后续 Step 误以为旧路线仍是主线的文件和目录。
4. 保留 `.gitignore`、`LICENSE`、README、planning files 和最小 `CMakeLists.txt`。
5. 运行 CMake configure / build / CTest，证明最小工程仍然可被工具链识别。
6. 用 stale scan 再扫一次旧路线文件名，确认没有残留入口。

### 4. 自身内部运行流程

整体可以看成 4 步：识别旧路线、删除旧产物、保留最小根文件、验证扫描结果。

核心对象不是 C++ 类，而是目录和文件职责：

- Git 元数据和根文件保存项目身份。
- `task_plan.md`、`findings.md`、`progress.md` 保存过程进度。
- `README.md` 保存对外说明。
- `tutorials/step00_reset.md` 解释为什么要重置。

核心函数可以理解成这些工程动作：

- 识别旧文件：查找旧路径、旧存储名、旧教程名和构建产物。
- 删除旧路线：移除会与未来 `include/liteim/...` / `src/...` 布局冲突的内容。
- 保留最小根：只留下下一步 CMake 初始化必需的文件。
- 验证扫描：用 CMake 和 `find` / `rg` 证明旧入口消失。

伪流程：

```text
reset_step0()
    read PROJECT_MEMORY route
    for each path in LiteIM:
        if path belongs to old route or build output:
            delete path
        else if path is root identity or planning memory:
            keep path
    run configure/build/test smoke
    run stale-route scan
```

### 5. 小例子和边界

小例子：如果旧仓库里有 `server/net/EventLoop.hpp`，Step 0 不在旧路径上修补它，而是删除旧目录；后续 Step 9 会在 `include/liteim/net/EventLoop.hpp` 重新定义当前路线的 Reactor 接口。

边界：本 Step 不处理 fd、线程、连接生命周期或业务对象所有权；它只处理文件归属。空目录和 `.gitkeep` 不作为成果保存，后续目录必须由真正需要它的 Step 创建。

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
