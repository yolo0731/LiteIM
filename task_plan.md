# LiteIM Task Plan

## Purpose

`task_plan.md` 只保留当前可执行状态，不再长期堆积每个历史 Step 的完整日志。完整项目路线以 `/home/yolo/jianli/PROJECT_MEMORY.md` 为准，协作约束以 `/home/yolo/jianli/AGENTS.md` 为准。

## Current Position

- Current completed step: `Step 18: implement TimerManager + timerfd heartbeat timeout`.
- Next planned step: `Step 19: implement signalfd graceful shutdown`.
- Current test baseline after Step 18: `ctest --test-dir build --output-on-failure` passed `164/164`.
- Current source route: high-performance LiteIM route, not the old single-Reactor / SQLite / `InMemoryStorage` route.

## Active Plan

### Phase 1 - Remove redundant markdown

**Status:** complete

Delete the repository-local `docs/` markdown files that duplicated README, planning memory, tutorials, and `PROJECT_MEMORY.md`.

### Phase 2 - Compact working memory

**Status:** complete

Rewrite `task_plan.md`, `findings.md`, and `progress.md` into lightweight current-state files. Remove historical重构前噪声 and stale hook context.

### Phase 3 - Sync references

**Status:** complete

Update README, tutorials, `/home/yolo/jianli/PROJECT_MEMORY.md`, and `/home/yolo/jianli/AGENTS.md` so future work does not recreate or depend on repository-local `docs/` markdown.

### Phase 4 - Verify and commit

**Status:** complete

Run link/reference checks plus the normal LiteIM build/test smoke commands, then commit the cleanup in the LiteIM Git repository.

## Next Step Boundary

When the user asks to continue LiteIM implementation, start `Step 19: implement signalfd graceful shutdown`.

Step 19 should only cover graceful signal handling and shutdown wiring. Do not start MySQL, Redis, login state, CLI, Qt, benchmark, or PersonaAgent in the same Step unless explicitly requested.

## Standing Rules

- Follow `concept -> handwritten code -> tests -> commit`.
- Keep C++17.
- Use `liteim::Byte` / `liteim::Bytes` for raw wire data.
- Keep important `Status` checks explicit.
- I/O threads must not run MySQL / Redis blocking work.
- Business threads must not directly mutate `Session`.
- Cross-thread responses must go through the owning `EventLoop`.
- Do not reintroduce SQLite or `InMemoryStorage` as the mainline storage route.
