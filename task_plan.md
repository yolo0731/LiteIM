# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 8: implement the `EventLoop` skeleton.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read project memory, memory index, planning files, Reactor headers/impl, CMake, tests, and Git status. |
| Record Step 8 design | complete | Implement `EventLoop` as the Reactor scheduling layer while keeping automatic Channel-to-loop updates for Step 9. |
| Implement code | complete | Added `src/net/EventLoop.cpp`; added the `Channel::handleEvent()` dispatch needed by `EventLoop::loop()`. |
| Add tests | complete | Added EventLoop tests using `pipe()` to verify callback dispatch, quit, update, and remove behavior. |
| Update docs and tutorials | complete | Added Step 8 tutorial and updated architecture, interview notes, tutorial index, README, and layout docs. |
| Build, test, review, commit | complete | Configure, build, CTest, direct tests, server smoke run, and whitespace check passed; final diff review before commit. |

## Planning Hook Phase Status

### Phase 1: Check memory and repo state
**Status:** complete

### Phase 2: Record Step 8 design
**Status:** complete

### Phase 3: Implement code
**Status:** complete

### Phase 4: Add tests
**Status:** complete

### Phase 5: Update docs and tutorials
**Status:** complete

### Phase 6: Build, test, review, commit
**Status:** complete

## Step 8 Scope

Implement the Reactor scheduling layer:

- `EventLoop()` owns an `Epoller`.
- `loop()` repeatedly calls `Epoller::poll()`.
- `loop()` iterates active `Channel` objects and calls `Channel::handleEvent()`.
- `quit()` requests the loop to stop.
- `updateChannel(Channel*)` forwards registration/modification to `Epoller`.
- `removeChannel(Channel*)` forwards deletion to `Epoller`.

`EventLoop` does not own `Channel` objects or socket fds. Future `Acceptor` and `Session` objects will own those lifetimes.

## Minimal Channel Support For Step 8

Step 8 may implement only the `Channel` behavior needed for `EventLoop::loop()` to be testable:

- `Channel::handleEvent()` dispatches callbacks based on `revents_`.
- Read/write/close/error callback storage was already added in Step 7.
- `Channel::enableReading()`, `enableWriting()`, `disableWriting()`, and `disableAll()` still only mutate local event masks in Step 8.

Automatic `Channel::update()` calls into `EventLoop` remain Step 9 work. Step 8 tests should manually call `loop.updateChannel(&channel)`.

## Persistent Requirements

- Every future Step tutorial and final summary must explain the testing section.
- The testing explanation must briefly cover what the tests verify, why those cases matter, and how to run the tests.
- Do not only list `ctest`; explain the purpose of the tests.
- Every Step tutorial must explain each newly added public function/interface in the relevant `.hpp` / `.cpp` section: purpose, input, output, side effects, and error/boundary behavior.
- Do not use interface overview tables in Step tutorials; explain functions one by one in prose.
- The `面试时怎么讲` section must include the Step's design thinking and common interview follow-up questions.

## Out of Scope

- Do not implement automatic `Channel::update()` integration yet.
- Do not implement bind/listen/accept server flow.
- Do not implement `Session`.
- Do not modify protocol behavior except tests integration if needed.
- Do not commit unrelated `.codex` changes.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| `bwrap: setting up uid map: Permission denied` | Normal sandboxed file reads | Used approved escalation for local project reads and builds. |
