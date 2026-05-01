# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 6: define Reactor core header interfaces.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read `/home/yolo/jianli/PROJECT_MEMORY.md`; repo has user-modified `tutorials/step05_socket_util.md` and untracked `.codex`. |
| Explain Reactor interface concepts | complete | Step 6 defines interfaces only: `Epoller`, `Channel`, and `EventLoop`. |
| Implement headers and compile tests | complete | Added three header declarations and an interface-only test that avoids constructing undefined classes. |
| Update docs and tutorial | complete | Added Chinese Step 6 docs, tutorial, and README progress. |
| Build, test, commit | complete | CMake build, CTest, direct test executable, server smoke run, diff check, and Step 6-only commit preparation completed. |

## Step 6 Scope

Define Reactor core interfaces:

- `server/net/Epoller.hpp`
- `server/net/Channel.hpp`
- `server/net/EventLoop.hpp`

This Step only declares responsibilities and dependencies. Implementations are left for later Steps:

- Step 7 implements `Epoller`.
- Step 8 implements `EventLoop`.
- Step 9 implements `Channel` and connects callbacks to `EventLoop`.

## Persistent Requirements

- Every future Step tutorial and final summary must explain the testing section.
- The testing explanation must briefly cover what the tests verify, why those cases matter, and how to run the tests.
- Do not only list `ctest`; explain the purpose of the tests.
- Every Step tutorial must explain each newly added public function/interface in the relevant `.hpp` / `.cpp` section: purpose, input, output, side effects, and error/boundary behavior.
- Do not use interface overview tables in Step tutorials; explain functions one by one in prose.
- The `面试时怎么讲` section must include the Step's design thinking and common interview follow-up questions.

## Out of Scope

- Do not implement bind/listen/accept server flow.
- Do not implement epoll.
- Do not implement `Session`.
- Do not modify protocol behavior except tests integration if needed.
- Do not instantiate undefined Reactor classes in tests; use compile-time interface checks.
- Do not commit unrelated `.codex` changes.
- Do not stage the existing user modification in `tutorials/step05_socket_util.md`.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| `bwrap: setting up uid map: Permission denied` | Normal sandboxed file reads | Used approved escalation for local project reads and builds. |
