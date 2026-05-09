# LiteIM Progress

## Current Snapshot

- Completed through: `Step 18: implement TimerManager + timerfd heartbeat timeout`.
- Last feature commit before this cleanup: `4447a37 feat(timer): integrate timerfd heartbeat timeout`.
- Current module baseline: `base`, `protocol`, `net`, `concurrency`, and `timer`.
- Next implementation step: `Step 19: implement signalfd graceful shutdown`.

## 2026-05-09 Documentation And Memory Cleanup

User request:

- Delete unused markdown files, especially the redundant `docs/` markdown files.
- Keep public project description in README.
- Keep project route in `/home/yolo/jianli/PROJECT_MEMORY.md`.
- Keep Codex constraints in `/home/yolo/jianli/AGENTS.md`.
- Keep file/session memory in `task_plan.md`, `findings.md`, and `progress.md`, but make those files lighter.

Changes made:

- Removed repository-local `docs/` markdown files.
- Rewrote `README.md` as the single public overview.
- Rewrote `task_plan.md`, `findings.md`, and `progress.md` into compact current-state files.
- Updated tutorials so they no longer list or link removed docs files.
- Updated `/home/yolo/jianli/PROJECT_MEMORY.md` and `/home/yolo/jianli/AGENTS.md` to reflect the no-docs markdown policy.

Verification:

- `cmake -S . -B build && cmake --build build` passed.
- `./build/server/liteim_server` passed and printed `LiteIM server scaffold is running on 0.0.0.0:9000`.
- `ctest --test-dir build --output-on-failure` passed `164/164`.
- `git diff --check` produced no output.
- `.gitkeep` scan produced no output.
- stale SQLite / `InMemoryStorage` / old `server/net` / old `server/protocol` path scan produced no output.

Commit:

- This cleanup is intended to be committed as one LiteIM Git commit.
