# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 7: implement the `Epoller` wrapper.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read project memory, current planning files, roadmap, Reactor headers, CMake, tests, and Git status. |
| Record Step 7 design | complete | Implement `Epoller` RAII, add/mod/del, and `poll()` with LT mode only. |
| Implement code | complete | Added `src/net/Epoller.cpp`; added minimal `Channel` state methods needed to test `Epoller`. |
| Add tests | complete | Added behavior tests using `pipe()` to verify add, mod, del, timeout, invalid input, and LT behavior. |
| Update docs and tutorials | complete | Added Step 7 tutorial and updated architecture, interview notes, project layout, tutorial index, README, and stale roadmap wording. |
| Build, test, review, commit | complete | Configure, build, CTest, direct tests, server smoke run, whitespace check, and diff review passed. |

## Step 7 Scope

Implement the first real Reactor component:

- `Epoller()` calls `epoll_create1(EPOLL_CLOEXEC)`.
- `~Epoller()` closes the owned epoll fd.
- `updateChannel(Channel*)` calls `EPOLL_CTL_ADD` for new fds and `EPOLL_CTL_MOD` for existing fds.
- `removeChannel(Channel*)` calls `EPOLL_CTL_DEL` and forgets the fd.
- `poll(timeout_ms)` calls `epoll_wait()` and returns active `Channel*` plus event flags.
- First version uses default LT behavior and must not set `EPOLLET`.

`Epoller` does not own `Channel` objects. It only stores pointers in `epoll_event.data.ptr` and assumes higher layers keep the `Channel` alive while registered.

## Minimal Channel Support

Step 7 may add only simple `Channel` definitions required to exercise `Epoller`:

- constructor/destructor
- `fd()`, `events()`, `revents()`, `setRevents()`
- `isNoneEvent()`, `isWriting()`
- event mask mutators such as `enableReading()`, `enableWriting()`, `disableWriting()`, and `disableAll()`
- callback setters

`handleEvent()` callback dispatch and automatic `EventLoop` updates remain later-step work.

## Persistent Requirements

- Every future Step tutorial and final summary must explain the testing section.
- The testing explanation must briefly cover what the tests verify, why those cases matter, and how to run the tests.
- Do not only list `ctest`; explain the purpose of the tests.
- Every Step tutorial must explain each newly added public function/interface in the relevant `.hpp` / `.cpp` section: purpose, input, output, side effects, and error/boundary behavior.
- Do not use interface overview tables in Step tutorials; explain functions one by one in prose.
- The `面试时怎么讲` section must include the Step's design thinking and common interview follow-up questions.

## Out of Scope

- Do not implement `EventLoop::loop()` yet.
- Do not implement `Channel::handleEvent()` callback dispatch yet.
- Do not implement bind/listen/accept server flow.
- Do not implement `Session`.
- Do not modify protocol behavior except tests integration if needed.
- Do not commit unrelated `.codex` changes.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| `bwrap: setting up uid map: Permission denied` | Normal sandboxed file reads | Used approved escalation for local project reads and builds. |
