# LiteIM Findings

## 权威来源

- `/home/yolo/jianli/PROJECT_MEMORY.md` 是 LiteIM 和 PersonaAgent 的唯一总方案来源。
- LiteIM 现在从 `Step 0` 重新开始。
- 当前路线是 `LiteIM High Performance + Qt Client + PersonaAgent 20-Step Edition`。
- 如果 `README.md`、`task_plan.md`、`progress.md`、教程或源码与 `PROJECT_MEMORY.md` 冲突，统一改回 `PROJECT_MEMORY.md` 的路线。

## Step 0 清理结论

本次 Step 0 的目的不是实现功能，而是清掉旧路线，留下干净起点。

已经删除的旧内容类型：

- 旧 `include/liteim/net`、`protocol`、`service`、`storage` 实现。
- 旧 `src/` 实现。
- 旧 `server/` 入口。
- 旧 `tests/` 单元测试。
- 旧 `tutorials/step01-step15` 教程。
- 旧 `docs/` 文档。
- 旧 `sql/` SQLite / 初始化脚本目录。
- 旧 `client_qt/` 临时结构。
- 旧 `build/` 构建产物。
- 空的 `.codex` 临时文件。

保留和重建的内容：

- `.gitignore`。
- `LICENSE`。
- 空 CMake 骨架。
- README / task_plan / findings / progress。
- `docs/architecture.md` 和 `docs/project_layout.md`。
- `tutorials/README.md` 和 `tutorials/step00_reset.md`。
- 新路线目标目录，并用 `.gitkeep` 保留。

## 核心架构结论

- 先搭高性能网络底座，再做业务、MySQL、Redis、Qt 和 Agent 接入。
- 不再走旧的单 Reactor 业务 baseline。
- 最终 LiteIM 不使用 SQLite。
- `InMemoryStorage` 不能作为主线存储实现；后续最多作为测试 double / mock。
- 服务端使用 C++17、CMake、Linux nonblocking socket、epoll LT、eventfd、timerfd、signalfd 和自定义 TLV 协议。
- 使用 one-loop-per-thread：每个 I/O 线程拥有一个 `EventLoop`。
- 主 Reactor 负责 `accept`，子 Reactor 负责连接读写事件。
- MySQL / Redis 阻塞调用必须进入业务 `ThreadPool`，不能在 I/O 线程执行。
- 业务线程不能直接修改 `Session`；响应必须通过 `EventLoop::queueInLoop()` 或 `EventLoop::runInLoop()` 投递回连接所属 I/O 线程。
- `Session` / `TcpConnection` 生命周期使用 `shared_ptr` / `weak_ptr` 管理，避免跨线程长期持有裸指针。
- 慢客户端保护必须显式实现，通过输出缓冲区高水位触发关闭或限流。

## Step 1 约束

Step 1 只做第一层工程初始化：

- 添加真正的 CMake target。
- 添加 `server/main.cpp`。
- 添加最小 `tests/test_main.cpp`。
- 保持 Qt、MySQL、Redis、协议、Reactor 都不提前实现。

Step 1 不允许恢复旧 Step 1-15 文件。旧代码里的知识可以参考，但文件本身不作为新路线起点。

## 测试要求

- Step 0 验证 CMake 空骨架可 configure/build。
- Step 1 开始，每个行为变化都要配测试。
- 协议、Buffer、FrameDecoder 等底层模块优先写确定性的 GoogleTest 单元测试。
- 网络行为先写 smoke test，等 CLI / Python 客户端出现后再补 E2E。
- MySQL / Redis 区分纯单元测试和依赖 Docker Compose 的集成测试。
- README 和报告里的 QPS、p99、内存占用只能来自真实压测结果，不能写虚构数字。
