# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 11: implement `Session` lifecycle.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read planning skill, session catchup, memory index, project memory, planning files, protocol/Buffer/Reactor source, and Git status. |
| Record Step 11 design | complete | Implement `Session` as one connected-client owner while keeping TcpServer and business routing out of scope. |
| Implement code | complete | Added `Session.hpp/.cpp`, read loop, frame decoding, message callback, output buffer, write loop, sendPacket, and close cleanup. |
| Add tests | complete | Added Session tests using connected stream sockets for read/decode, sticky/large frames, send/write, close, and invalid-frame behavior. |
| Update docs and tutorials | complete | Added Step 11 tutorial and updated architecture, interview notes, tutorial index, README, and layout docs. |
| Build, test, review, commit | complete | Configure, build, CTest, direct tests, server smoke run, whitespace check, and final diff review passed before commit. |

## Planning Hook Phase Status

### Phase 1: Check memory and repo state
**Status:** complete

### Phase 2: Record Step 11 design
**Status:** complete

### Phase 3: Implement code
**Status:** complete

### Phase 4: Add tests
**Status:** complete

### Phase 5: Update docs and tutorials
**Status:** complete

### Phase 6: Build, test, review, commit
**Status:** complete

## Step 11 Scope

Implement `Session`, the one-connection component:

- Own one connected client fd.
- Hold one `Channel` for that fd.
- Hold one `FrameDecoder` for TCP stream framing.
- Hold one output `Buffer`.
- `start()` registers read interest after callbacks are configured.
- `handleRead()` loops `read()` until `EAGAIN` / `EWOULDBLOCK`.
- Read bytes are fed into `FrameDecoder`.
- Complete decoded `Packet`s are passed to `MessageCallback`.
- `sendPacket()` encodes a `Packet`, appends it to the output buffer, and enables write interest.
- `handleWrite()` writes buffered bytes until the buffer is empty or the fd would block.
- `close()` removes the channel from `EventLoop`, closes the fd, and notifies `CloseCallback`.

`Session` owns the connected fd after construction. Future `TcpServer` will create and store `Session` objects when `Acceptor` reports new connections.

## Step 11 Design Boundaries

- Do not implement `TcpServer`.
- Do not manage online sessions.
- Do not implement `MessageRouter`.
- Do not implement login, heartbeat response, private chat, or storage behavior.
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

- Do not implement `TcpServer`.
- Do not modify protocol behavior except tests integration if needed.
- Do not commit unrelated `.codex` changes.
- Do not commit unrelated pre-existing edits such as the current `tutorials/step10_acceptor.md` wording tweak unless explicitly requested.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| `bwrap: setting up uid map: Permission denied` | Normal sandboxed file reads | Used approved escalation for local project reads and builds. |
| `/bin/bash: -c: line 1: unexpected EOF while looking for matching \`\`` | Stale-doc wording search included a backtick inside the shell string | Re-ran the search with a simpler expression that avoided backticks; no stale Step 9 wording was found in current docs/index files. |
