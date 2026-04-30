# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 5: implement Linux socket utility functions.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read `/home/yolo/jianli/PROJECT_MEMORY.md`; repo is clean except untracked `.codex`. |
| Explain SocketUtil concepts | complete | SocketUtil wraps Linux socket/fcntl/setsockopt/getsockopt/close helpers. |
| Implement SocketUtil and tests | complete | Added `server/net/SocketUtil.*` and `tests/test_socket_util.cpp`. |
| Update docs and tutorial | complete | Added Chinese Step 5 tutorial and docs/interview notes. |
| Build, test, commit | complete | Final CMake build, CTest, direct tests, and server run passed. |

## Step 5 Scope

Implement `server/net/SocketUtil` with:

- `createNonBlockingSocket()`
- `setNonBlocking(int fd)`
- `setReuseAddr(int fd)`
- `setReusePort(int fd)`
- `closeFd(int fd)`
- `getSocketError(int fd)`

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
- Do not commit unrelated `.codex` changes.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| `bwrap: setting up uid map: Permission denied` | Normal sandboxed file reads | Used approved escalation for local project reads and builds. |
