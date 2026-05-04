# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 13: implement `MessageRouter` heartbeat routing.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read planning skill, session catchup, project memory, planning files, roadmap, TcpServer/Packet/MessageType source, CMake, tests, and Git status. |
| Record Step 13 design | complete | Implement `MessageRouter` as a service-layer dispatcher while keeping login, chat, storage, and heartbeat timeout cleanup out of scope. |
| Implement code | complete | Added service module and route `HEARTBEAT_REQ` / unknown message responses through `Session::sendPacket()`. |
| Add tests | complete | Added MessageRouter tests for heartbeat response, unknown type error response, seq_id preservation, and empty-body boundaries. |
| Update docs and tutorials | complete | Added Step 13 tutorial and synced README, architecture, layout, interview notes, tutorial index, and project memory. |
| Build, test, review, commit | complete | Build, direct tests, CTest, server Ctrl+C smoke run, whitespace check, final diff review, and Step 13 commit completed. |

## Planning Hook Phase Status

### Phase 1: Check memory and repo state
**Status:** complete

### Phase 2: Record Step 13 design
**Status:** complete

### Phase 3: Implement code
**Status:** complete

### Phase 4: Add tests
**Status:** complete

### Phase 5: Update docs and tutorials
**Status:** complete

### Phase 6: Build, test, review, commit
**Status:** complete

## Step 13 Scope

Implement `MessageRouter`, the first service-layer dispatcher:

- Add `include/liteim/service/MessageRouter.hpp`.
- Add `src/service/MessageRouter.cpp`.
- Route packets by `packet.header.msg_type`.
- Support `HEARTBEAT_REQ` by replying with `HEARTBEAT_RESP`.
- Reply to unknown message types with `ERROR_RESP`.
- Preserve the request `seq_id` in router-generated responses.
- Keep response body small and deterministic so tests can assert it.
- Integrate `MessageRouter` with `TcpServer::setMessageCallback()` in `server/main.cpp`.

`MessageRouter` is service-layer orchestration. It should depend on `Session::sendPacket()` but must not operate on raw fds, own sessions, or manage epoll state.

## Step 13 Design Boundaries

- Do not implement registration/login/auth.
- Do not implement private chat, group chat, history, friend list, or bot behavior.
- Do not define `IStorage` / `ICache`; those belong to Step 14.
- Do not add SQLite/database access.
- Do not implement heartbeat timeout cleanup; timerfd/TimerHeap belongs to Step 20.
- Do not add user identity binding to `MessageRouter`; login binding comes later.
- Do not change protocol framing or `FrameDecoder`.
- Do not make `MessageRouter` call `sendToSession()` or touch raw fd values.

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
| `warning: Not a git repository. Use --no-index to compare two paths outside a working tree` | Ran `git diff -- /home/yolo/jianli/PROJECT_MEMORY.md` from `/home/yolo/jianli` | Treat `/home/yolo/jianli/PROJECT_MEMORY.md` as workspace-level metadata outside the LiteIM Git repo; verify repo-local diff from `/home/yolo/jianli/LiteIM` and inspect the external file directly if needed. |
