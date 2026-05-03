# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 12: implement `TcpServer`.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read planning skill, session catchup, project memory, planning files, roadmap, Acceptor/Session/EventLoop source, tests, and Git status. |
| Record Step 12 design | complete | Implement `TcpServer` as the server coordinator while keeping MessageRouter, login, chat, and storage out of scope. |
| Implement code | complete | Added `TcpServer`, integrated `Acceptor` and `Session`, added session/user send helpers, and wired `signalfd` shutdown. |
| Add tests | complete | Added TcpServer tests for accept/session tracking, message callback, sendToSession/sendToUser, and signal shutdown; added Acceptor close test. |
| Update docs and tutorials | complete | Added Step 12 tutorial and updated architecture, interview notes, tutorial index, README, layout docs, and project memory. |
| Build, test, review, commit | complete | Build, CTest, direct tests, server Ctrl+C smoke run, whitespace check, and final diff review passed. |

## Planning Hook Phase Status

### Phase 1: Check memory and repo state
**Status:** complete

### Phase 2: Record Step 12 design
**Status:** complete

### Phase 3: Implement code
**Status:** complete

### Phase 4: Add tests
**Status:** complete

### Phase 5: Update docs and tutorials
**Status:** complete

### Phase 6: Build, test, review, commit
**Status:** complete

## Step 12 Scope

Implement `TcpServer`, the server coordinator:

- Hold an `EventLoop*` supplied by the server entry point.
- Hold one `Acceptor` for the listen socket.
- Maintain active `Session` objects by connected fd.
- Create and start a `Session` when `Acceptor` reports a new connection.
- Remove closed sessions from the active map without destroying a `Session` while its callback stack is still unwinding.
- Provide `sendToSession()`.
- Provide a foundation for `sendToUser()` with explicit user-to-session binding.
- Register `SIGINT` / `SIGTERM` with `signalfd`.
- Add the signal fd to the same `EventLoop` through `Channel`.
- On signal or `stop()`, close sessions, close the listen socket, disable signal handling, and call `EventLoop::quit()`.

`TcpServer` coordinates networking objects only. It does not parse business message types or implement login/chat/storage semantics.

## Step 12 Design Boundaries

- Do not implement `MessageRouter`.
- Do not implement login, heartbeat response, private chat, or storage behavior.
- Do not add `EPOLLET`; keep LT mode.
- Do not add SQLite/database flush logic.
- Do not implement timer/heartbeat timeout logic.
- Do not make `Session` responsible for user identity.
- Keep signal handling limited to `SIGINT` / `SIGTERM` and orderly network shutdown.

## Persistent Requirements

- Every future Step tutorial and final summary must explain the testing section.
- The testing explanation must briefly cover what the tests verify, why those cases matter, and how to run the tests.
- Do not only list `ctest`; explain the purpose of the tests.
- Every Step tutorial must explain each newly added public function/interface in the relevant `.hpp` / `.cpp` section: purpose, input, output, side effects, and error/boundary behavior.
- Do not use interface overview tables in Step tutorials; explain functions one by one in prose.
- The `面试时怎么讲` section must include the Step's design thinking and common interview follow-up questions.

## Out of Scope

- Do not implement `TcpServer`.
- Do not modify protocol behavior except tests integration if needed.
- Do not commit unrelated `.codex` changes.
- Do not commit unrelated pre-existing edits such as the current `tutorials/step10_acceptor.md` wording tweak unless explicitly requested.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| `bwrap: setting up uid map: Permission denied` | Normal sandboxed file reads | Used approved escalation for local project reads and builds. |
| `/bin/bash: -c: line 1: unexpected EOF while looking for matching \`\`` | Stale-doc wording search included a backtick inside the shell string | Re-ran the search with a simpler expression that avoided backticks; no stale Step 9 wording was found in current docs/index files. |
