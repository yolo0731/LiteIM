# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 4: implement the network `Buffer` abstraction.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read `/home/yolo/jianli/PROJECT_MEMORY.md`; repo is `main...origin/main [ahead 1]`; `.codex` is untracked and unrelated. |
| Enable Planning with Files | complete | Created `task_plan.md`, `findings.md`, and `progress.md` in the LiteIM project root. |
| Explain Buffer concepts | complete | Buffer is a generic network read/write buffer, not protocol-aware and not socket-owning. |
| Implement Buffer and tests | complete | `server/net/Buffer.*` and `tests/test_buffer.cpp` compile and pass tests. |
| Update docs and tutorial | complete | Updated Chinese architecture/interview docs and added `tutorials/step04_buffer.md`. |
| Build, test, commit | complete | Build and tests passed; Step 4 commit prepared. |

## Step 4 Scope

Implement `server/net/Buffer` with:

- `append(const char* data, size_t len)`
- `appendString(const std::string& data)`
- `readableBytes()`
- `peek()`
- `retrieve(size_t len)`
- `retrieveAllAsString()`

## Out of Scope

- Do not implement socket APIs.
- Do not implement epoll.
- Do not implement `Session`.
- Do not modify protocol behavior except tests integration if needed.
- Do not commit unrelated `.codex` changes.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| `bwrap: setting up uid map: Permission denied` | Normal sandboxed file reads | Used approved escalation for local project reads and builds. |
