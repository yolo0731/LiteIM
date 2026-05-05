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
- CLI, Python E2E, benchmark, GoogleTest/gMock, ASan/UBSan, CI.
- Qt Widgets demo client with a familiar IM three-column chat layout.
- PersonaAgent integration through a Python BotClient and a separate six-node LangGraph AgentService.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Step 0 concept | done | Step 0 is a cleanup/reset step, not feature implementation. |
| Step 0 delete old route files | done | Removed old source, tests, docs/tutorials, SQLite/InMemoryStorage route files, and build output. |
| Step 0 keep minimal root | done | Removed premature empty folders and `.gitkeep`; future directories will be created by the Step that needs them. |
| Step 0 docs | done | Rewrote README, findings, progress, task plan, docs, and tutorial index for the minimal Step 0 route. |
| Step 0 verification | done | CMake configure/build and CTest passed; `.gitkeep` and stale-route filename checks returned no matches. |
| Step 0 commit | done | Commit: `29e41e9 chore: keep LiteIM step0 minimal`. |
| Step 1 concept | done | Step 1 creates a minimal buildable server/test project and introduces GoogleTest from the start. |
| Step 1 code | done | Added root CMake wiring, GoogleTest FetchContent, minimal `server`, and gtest-based `tests` target. |
| Step 1 tests | done | CMake configure/build, server smoke run, and CTest passed with `SmokeTest.GoogleTestWorks`. |
| Step 1 commit | done | Commit message: `init: create LiteIM project structure with googletest`. |

## Current Decision

Use `/home/yolo/jianli/PROJECT_MEMORY.md` as the source of truth.

LiteIM phases:

1. Step 0: reset workspace and keep only the minimal current-step files.
2. Step 1-20: high-performance network base and multi-Reactor echo server.
3. Step 21-30: MySQL / Redis storage and cache layer.
4. Step 31-40: async IM business services and BotGateway.
5. Step 41-45: CLI, Python E2E, benchmark, GoogleTest/gMock, ASan/UBSan, CI.
6. Step 46-53: Qt Widgets demo client.
7. Step 54: README, architecture diagrams, Qt screenshots, benchmark report, and interview docs.
8. PersonaAgent Step 1-6: Python BotClient, protocol compatibility, FastAPI `/chat`, login/heartbeat/reconnect, Echo Bot.
9. PersonaAgent Step 7-20: six-node LangGraph + Knowledge/Memory/Authorized Style RAG + Persona + Safety + Tool Calling + Checkpoint + Trace + Evaluation.

## Important Boundaries

- Do not continue the old single-Reactor business baseline.
- Do not make `InMemoryStorage` the main storage path. It may only be a future test double/mock.
- Do not reintroduce SQLite.
- Do not use Boost.Asio.
- Do not run MySQL / Redis calls in I/O threads.
- Do not let business threads directly mutate `Session`; responses must go through `EventLoop::queueInLoop()` or `runInLoop()`.
- Do not embed Python/LangGraph into the C++ server; PersonaAgent connects as a BotClient.
- PersonaAgent uses six core LangGraph nodes: `dialogue_policy`, `retrieve`, `tool_router`, `generate_reply`, `safety_check`, and `send_message`.
- Authorized Style RAG data must have consent manifest, source metadata, allowed usage, PII redaction, revocation support, and SafetyGuard protection.
- LiteIM should only expose BotGateway/protocol integration points; Knowledge/Memory/Style RAG, Tool Calling, Trace, Checkpoint, and Evaluation belong to PersonaAgent.
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

Step 0 intentionally does not keep empty future directories or `.gitkeep` files.

## Step 1 Target

Step 1 should add the first real buildable project structure:

```text
LiteIM/
├── CMakeLists.txt
├── server/main.cpp
├── server/CMakeLists.txt
├── tests/test_main.cpp
└── tests/CMakeLists.txt
```

Step 1 intentionally does not create `include/`, `src/`, `client_qt/`, `bench/`, `scripts`, or `docker`. It does introduce GoogleTest through CMake `FetchContent` because all later steps should use gtest cases instead of a custom test framework.
Those directories will be created in the Step that first needs them.

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
- Tests use GoogleTest from Step 1; later Step plans should name concrete `TEST` / `TEST_F` / `TEST_P` cases when possible.
- README benchmark numbers must only use real local benchmark results.
- Each code Step must build and pass relevant tests before commit.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| Existing worktree contained old SQLite/InMemoryStorage route files | Step 0 cleanup | Deleted old implementation files. |
| Step 0 initially created all future directories with `.gitkeep` | User review | Removed them; future directories will be created only when each Step needs them. |
| Sandbox `bwrap` uid map failure | Running shell commands | Used approved escalated execution for repository inspection and cleanup. |
