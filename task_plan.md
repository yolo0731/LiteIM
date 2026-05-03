# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 10: implement the nonblocking `Acceptor`.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read planning skill, session catchup, memory index, project memory, planning files, SocketUtil, Channel, and Git status. |
| Record Step 10 design | complete | Implement `Acceptor` as the listen-socket owner and new-connection notifier, while keeping Session/TcpServer out of scope. |
| Implement code | complete | Added `Acceptor.hpp/.cpp`, create/bind/listen nonblocking socket, register listen fd with EventLoop, and loop accept until EAGAIN. |
| Add tests | complete | Added Acceptor tests using localhost connections to verify callback delivery, multiple pending accepts, nonblocking accepted fd, and constructor validation. |
| Update docs and tutorials | complete | Added Step 10 tutorial and updated architecture, interview notes, tutorial index, README, and layout docs. |
| Build, test, review, commit | complete | Configure, build, CTest, direct tests, server smoke run, whitespace check, stale-doc wording check, and final diff review passed before commit. |

## Planning Hook Phase Status

### Phase 1: Check memory and repo state
**Status:** complete

### Phase 2: Record Step 10 design
**Status:** complete

### Phase 3: Implement code
**Status:** complete

### Phase 4: Add tests
**Status:** complete

### Phase 5: Update docs and tutorials
**Status:** complete

### Phase 6: Build, test, review, commit
**Status:** complete

## Step 10 Scope

Implement `Acceptor`, the listening-socket component:

- Create a nonblocking IPv4 TCP listen socket.
- Set `SO_REUSEADDR` and `SO_REUSEPORT`.
- Bind to the configured IP and port.
- Call `listen()`.
- Register the listen fd in `EventLoop` through a `Channel`.
- When the listen fd is readable, loop `accept4()` until `EAGAIN` / `EWOULDBLOCK`.
- Ensure accepted client fds are nonblocking and close-on-exec.
- Notify upper layers through a new-connection callback.
- Close accepted fds immediately if no callback is installed.

`Acceptor` owns the listen fd and its listen `Channel`. It does not own accepted client fds after passing them to the callback. Future `TcpServer` will create `Session` objects from those fds.

## Step 10 Design Boundaries

- Do not implement `Session`.
- Do not implement `TcpServer`.
- Do not parse protocol packets.
- Do not manage online sessions.
- Do not add `EPOLLET`; keep LT mode.
- Do not add a wakeup fd to `EventLoop`.
- Do not add business routing or login behavior.

## Persistent Requirements

- Every future Step tutorial and final summary must explain the testing section.
- The testing explanation must briefly cover what the tests verify, why those cases matter, and how to run the tests.
- Do not only list `ctest`; explain the purpose of the tests.
- Every Step tutorial must explain each newly added public function/interface in the relevant `.hpp` / `.cpp` section: purpose, input, output, side effects, and error/boundary behavior.
- Do not use interface overview tables in Step tutorials; explain functions one by one in prose.
- The `面试时怎么讲` section must include the Step's design thinking and common interview follow-up questions.

## Out of Scope

- Do not implement `Session`.
- Do not implement `TcpServer`.
- Do not modify protocol behavior except tests integration if needed.
- Do not commit unrelated `.codex` changes.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| `bwrap: setting up uid map: Permission denied` | Normal sandboxed file reads | Used approved escalation for local project reads and builds. |
| `/bin/bash: -c: line 1: unexpected EOF while looking for matching \`\`` | Stale-doc wording search included a backtick inside the shell string | Re-ran the search with a simpler expression that avoided backticks; no stale Step 9 wording was found in current docs/index files. |
