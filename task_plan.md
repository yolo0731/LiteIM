# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is project layout refactor before Step 7.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read project memory, AGENTS, current CMake, include usage, and Git status. |
| Decide mature layout | complete | Use `include/liteim/...` for headers and `src/...` for library implementation; keep `server/main.cpp` as app entry. |
| Move files and update build/includes | complete | Moved protocol/net headers to `include/liteim`, sources to `src`, and updated CMake/include directives. |
| Update docs and project guidance | complete | Updated README, docs, tutorials path references, AGENTS.md, and `/home/yolo/jianli/PROJECT_MEMORY.md`. |
| Build, test, review | complete | CMake configure/build, CTest, direct tests, server smoke run, and whitespace check passed. |

## Layout Refactor Scope

Move current C++ library code to a more mature layout:

- Public/internal headers: `include/liteim/<module>/*.hpp`
- Library implementation: `src/<module>/*.cpp`
- Server executable entry: `server/main.cpp`
- Tests: `tests/*.cpp`
- Documentation: `docs/*.md`, `tutorials/*.md`

This refactor must not change runtime behavior or implement Step 7 logic.

## New Include Convention

Use fully qualified project includes:

- `#include "liteim/protocol/Packet.hpp"`
- `#include "liteim/protocol/FrameDecoder.hpp"`
- `#include "liteim/net/Buffer.hpp"`
- `#include "liteim/net/SocketUtil.hpp"`
- `#include "liteim/net/Epoller.hpp"`
- `#include "liteim/net/Channel.hpp"`
- `#include "liteim/net/EventLoop.hpp"`

## Persistent Requirements

- Every future Step tutorial and final summary must explain the testing section.
- The testing explanation must briefly cover what the tests verify, why those cases matter, and how to run the tests.
- Do not only list `ctest`; explain the purpose of the tests.
- Every Step tutorial must explain each newly added public function/interface in the relevant `.hpp` / `.cpp` section: purpose, input, output, side effects, and error/boundary behavior.
- Do not use interface overview tables in Step tutorials; explain functions one by one in prose.
- The `面试时怎么讲` section must include the Step's design thinking and common interview follow-up questions.

## Out of Scope

- Do not implement Step 7 `Epoller` logic yet.
- Do not implement bind/listen/accept server flow.
- Do not implement `Session`.
- Do not modify protocol behavior except tests integration if needed.
- Do not instantiate undefined Reactor classes in tests; use compile-time interface checks.
- Do not commit unrelated `.codex` changes.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| `bwrap: setting up uid map: Permission denied` | Normal sandboxed file reads | Used approved escalation for local project reads and builds. |
