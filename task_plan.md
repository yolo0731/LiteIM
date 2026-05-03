# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 9: implement `Channel` and connect it to `EventLoop`.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read planning skill, session catchup, memory index, project memory, planning files, Reactor source/tests/docs, and Git status. |
| Record Step 9 design | complete | Implement `Channel::update()` so event-mask changes notify `EventLoop`, while keeping Acceptor/Session out of scope. |
| Implement code | complete | Wired `enableReading()`, `enableWriting()`, `disableWriting()`, and `disableAll()` to update or remove epoll interest through `EventLoop`. |
| Add tests | complete | Added Channel-focused tests for automatic registration, disable/remove behavior, re-enable behavior, and callback dispatch. |
| Update docs and tutorials | complete | Added Step 9 tutorial and updated architecture, interview notes, tutorial index, README, and layout docs. |
| Build, test, review, commit | complete | Configure, build, CTest, direct tests, server smoke run, whitespace check, and stale-doc wording check passed; final diff reviewed before commit. |

## Planning Hook Phase Status

### Phase 1: Check memory and repo state
**Status:** complete

### Phase 2: Record Step 9 design
**Status:** complete

### Phase 3: Implement code
**Status:** complete

### Phase 4: Add tests
**Status:** complete

### Phase 5: Update docs and tutorials
**Status:** complete

### Phase 6: Build, test, review, commit
**Status:** complete

## Step 9 Scope

Implement the `Channel` event-proxy layer and connect it to `EventLoop`:

- `Channel` continues to bind one fd to interested events, returned events, and callbacks.
- `handleEvent()` dispatches read, write, close, and error callbacks based on `revents_`.
- `enableReading()` and `enableWriting()` update `events_`, then notify `EventLoop`.
- `disableWriting()` updates `events_`, then notifies `EventLoop`.
- `disableAll()` clears all interest and removes the fd from the loop's epoll interest set.
- `Channel::update()` is the private bridge to `EventLoop::updateChannel()` / `removeChannel()`.

`Channel` does not own the fd and does not own `EventLoop`. Future `Acceptor` and `Session` objects will own their fd lifetimes and unregister their channels before destruction.

## Step 9 Design Boundaries

- Do not implement `Acceptor`.
- Do not implement `Session`.
- Do not implement bind/listen/accept server flow.
- Do not add `EPOLLET`; keep LT mode.
- Do not add a wakeup fd to `EventLoop`.
- Keep low-level `Epoller` tests possible with `Channel(nullptr, fd)` by making null-loop channels mutate local event state only.

## Persistent Requirements

- Every future Step tutorial and final summary must explain the testing section.
- The testing explanation must briefly cover what the tests verify, why those cases matter, and how to run the tests.
- Do not only list `ctest`; explain the purpose of the tests.
- Every Step tutorial must explain each newly added public function/interface in the relevant `.hpp` / `.cpp` section: purpose, input, output, side effects, and error/boundary behavior.
- Do not use interface overview tables in Step tutorials; explain functions one by one in prose.
- The `面试时怎么讲` section must include the Step's design thinking and common interview follow-up questions.

## Out of Scope

- Do not implement bind/listen/accept server flow.
- Do not implement `Session`.
- Do not modify protocol behavior except tests integration if needed.
- Do not commit unrelated `.codex` changes.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| `bwrap: setting up uid map: Permission denied` | Normal sandboxed file reads | Used approved escalation for local project reads and builds. |
| `/bin/bash: -c: line 1: unexpected EOF while looking for matching \`\`` | Stale-doc wording search included a backtick inside the shell string | Re-ran the search with a simpler expression that avoided backticks; no stale Step 9 wording was found in current docs/index files. |
