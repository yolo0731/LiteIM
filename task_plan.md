# LiteIM Task Plan

## Goal

Restart LiteIM from `Step 0` according to `/home/yolo/jianli/PROJECT_MEMORY.md`.

LiteIM is planned as a C++17 high-performance IM system:

- Linux nonblocking socket + epoll LT.
- `eventfd` task wakeup and one-loop-per-thread Reactor.
- multi-Reactor `TcpServer`.
- business `ThreadPool` for blocking MySQL / Redis work.
- `timerfd` heartbeat timeout and `signalfd` graceful shutdown.
- slow-client output-buffer backpressure.
- custom TLV binary protocol and TCP stream decoder.
- MySQL persistence and Redis online/unread/rate-limit state.
- CLI, Python E2E, benchmark, GoogleTest, ASan, CI.
- Qt Widgets demo client with a familiar IM three-column chat layout.
- PersonaAgent integration through a Python BotClient.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Step 0 concept | done | Step 0 is a cleanup/reset step, not feature implementation. |
| Step 0 delete old route files | done | Removed old source, tests, docs/tutorials, SQLite/InMemoryStorage route files, and build output. |
| Step 0 rebuild clean folders | done | Recreated target high-performance layout with `.gitkeep` placeholders. |
| Step 0 docs | done | Rewrote README, findings, progress, task plan, docs, and tutorial index for the new route. |
| Step 0 verification | done | CMake configure/build and CTest passed; stale-route filename check returned no matches. |
| Step 0 commit | pending | Commit message: `chore: reset LiteIM workspace for high performance roadmap`. |
| Step 1 concept | pending | Explain high-performance project structure and why it differs from the old route. |
| Step 1 code | pending | Add real CMake targets, minimal server entry, and minimal test target. |
| Step 1 tests | pending | Verify configure, build, server smoke run, and CTest. |
| Step 1 commit | pending | Commit message: `init: create LiteIM high performance project structure`. |

## Current Decision

Use `/home/yolo/jianli/PROJECT_MEMORY.md` as the source of truth.

LiteIM phases:

1. Step 0: reset workspace and remove stale route files.
2. Step 1-20: high-performance network base and multi-Reactor echo server.
3. Step 21-30: MySQL / Redis storage and cache layer.
4. Step 31-40: async IM business services and BotGateway.
5. Step 41-45: CLI, Python E2E, benchmark, GoogleTest, ASan, CI.
6. Step 46-53: Qt Widgets demo client.
7. Step 54: README, architecture diagrams, Qt screenshots, benchmark report, and interview docs.

PersonaAgent phases:

1. Step 1-6: Python BotClient, protocol compatibility, login/heartbeat, Echo Bot.
2. Step 7-20: FastAPI + LangGraph + RAG + Persona + Safety + Tool Calling + Checkpoint + Trace + Evaluation.

## Important Boundaries

- Do not continue the old single-Reactor business baseline.
- Do not make `InMemoryStorage` the main storage path. It may only be a future test double/mock.
- Do not reintroduce SQLite.
- Do not use Boost.Asio.
- Do not run MySQL / Redis calls in I/O threads.
- Do not let business threads directly mutate `Session`; responses must go through `EventLoop::queueInLoop()` or `runInLoop()`.
- Do not embed Python/LangGraph into the C++ server; PersonaAgent connects as a BotClient.
- Do not use WeChat logo, name, icons, or assets in the Qt client.
- Qt is a demo layer; service logic stays in the server.

## Step 0 Kept Files

- `.gitignore`
- `LICENSE`
- `CMakeLists.txt` as an empty Step 0 scaffold
- `README.md`
- `task_plan.md`
- `findings.md`
- `progress.md`
- `docs/architecture.md`
- `docs/project_layout.md`
- `tutorials/README.md`
- `tutorials/step00_reset.md`
- target empty directories with `.gitkeep`

## Step 1 Target

Step 1 should add the first real buildable project structure:

```text
LiteIM/
├── include/liteim/{base,net,protocol,concurrency,timer,storage,cache,service}/
├── src/{base,net,protocol,concurrency,timer,storage,cache,service}/
├── server/main.cpp
├── tests/test_main.cpp
└── CMakeLists.txt + target CMake files
```

Step 1 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

## Persistent Requirements

- Every Step follows: concept -> code -> tests -> commit.
- Every Step tutorial explains new public functions, important private helpers, tests, edge cases, and interview talking points.
- Tests must be explained, not just listed.
- README benchmark numbers must only use real local benchmark results.
- Each code Step must build and pass relevant tests before commit.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| Existing worktree contained old SQLite/InMemoryStorage route files | Step 0 cleanup | Deleted old implementation files and recreated the new target layout. |
| Sandbox `bwrap` uid map failure | Running shell commands | Used approved escalated execution for repository inspection and cleanup. |
