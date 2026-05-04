# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 15: implement `SQLiteStorage`.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read planning skill, session catchup, memory index, project memory Step 15, current storage interfaces, CMake, tests, SQLite availability, and Git status. |
| Record Step 15 design | complete | Implemented `SQLiteStorage` over `IStorage` while keeping auth/chat services and real cache behavior out of scope. |
| Implement code | complete | Added SQLite schema, `SQLiteStorage` header/implementation, RAII statement helpers, and SQLite CMake linkage. |
| Add tests | complete | Added SQLiteStorage tests for users, friends, groups, messages, offline queries, and file persistence. |
| Update docs and tutorials | complete | Added Step 15 tutorial and synced README, database, architecture, layout, interview notes, tutorial index, and project memory. |
| Build, test, review, commit | complete | Build, direct tests, CTest, server smoke run, whitespace check, final diff review, and Step 15 commit preparation completed. |

## Planning Hook Phase Status

### Phase 1: Check memory and repo state
**Status:** complete

### Phase 2: Record Step 15 design
**Status:** complete

### Phase 3: Implement code
**Status:** complete

### Phase 4: Add tests
**Status:** complete

### Phase 5: Update docs and tutorials
**Status:** complete

### Phase 6: Build, test, review, commit
**Status:** complete

## Step 15 Scope

Implement SQLite persistence behind the Step 14 `IStorage` contract:

- Add `include/liteim/storage/SQLiteStorage.hpp`.
- Add `src/storage/SQLiteStorage.cpp`.
- Replace the SQL placeholder in `sql/init.sql` with the Step 15 schema.
- Open a SQLite database from a path, defaulting to `liteim.db`.
- Execute `sql/init.sql` on startup.
- Implement all current `IStorage` methods.
- Keep `NullCache` as the single-process no-op cache implementation.
- Link `liteim_storage` with SQLite3.

`SQLiteStorage` is the storage-layer implementation. It should translate storage interface calls into SQL but must not implement business protocol behavior.

## Step 15 Design Boundaries

- Do not implement registration/login/auth behavior.
- Do not implement private chat, group chat, friend list, or history service behavior.
- Do not introduce Redis or any real distributed cache.
- Do not make `MessageRouter` depend on storage yet.
- Do not change protocol encoding/decoding.
- Do not change `IStorage` unless a clear SQLite mapping bug requires it.
- Keep `NullCache` no-op; do not store online state in it.
- Keep password hashing/salt generation out of Step 15; storage only stores the supplied salt/hash.

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
