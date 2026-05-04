# LiteIM Task Plan

## Goal

Continue LiteIM as a step-by-step teaching project. Current active task is Step 14: define `IStorage` / `ICache` abstractions and `NullCache`.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read planning skill, session catchup, memory index, project memory Step 14, planning files, roadmap, database docs, SQL placeholder, CMake, and Git status. |
| Record Step 14 design | complete | Defined storage/cache interfaces while keeping SQLiteStorage, SQL schema, auth, chat, and real cache behavior out of scope. |
| Implement code | complete | Added storage domain types, `IStorage`, `ICache`, and `NullCache`; exposed the module through CMake. |
| Add tests | complete | Added interface/NullCache tests that prove business-layer test doubles can implement `IStorage`. |
| Update docs and tutorials | complete | Added Step 14 tutorial and synced README, database, architecture, layout, interview notes, tutorial index, and project memory. |
| Build, test, review, commit | complete | Build, direct tests, CTest, server smoke run, whitespace check, final diff review, and Step 14 commit completed. |

## Planning Hook Phase Status

### Phase 1: Check memory and repo state
**Status:** complete

### Phase 2: Record Step 14 design
**Status:** complete

### Phase 3: Implement code
**Status:** complete

### Phase 4: Add tests
**Status:** complete

### Phase 5: Update docs and tutorials
**Status:** complete

### Phase 6: Build, test, review, commit
**Status:** complete

## Step 14 Scope

Define storage and cache abstractions for future business services:

- Add storage domain types shared by `IStorage` and `ICache`.
- Add `include/liteim/storage/IStorage.hpp`.
- Add `include/liteim/storage/ICache.hpp`.
- Add `include/liteim/storage/NullCache.hpp`.
- Add `src/storage/NullCache.cpp`.
- Define `IStorage` methods for users, friendships, groups, private/group messages, history, members, and offline messages.
- Define an `ICache` interface for online-session cache lookups.
- Implement `NullCache` as a no-op cache implementation.
- Add a `liteim_storage` CMake target.

Step 14 creates contracts only. It should make future business services depend on interfaces instead of SQLite.

## Step 14 Design Boundaries

- Do not implement `SQLiteStorage`.
- Do not add real SQL schema or database file access.
- Do not implement registration/login/auth behavior.
- Do not implement private chat, group chat, friend list, or history service behavior.
- Do not introduce Redis or any real distributed cache.
- Do not make `MessageRouter` depend on storage yet.
- Do not change protocol encoding/decoding.
- Keep `NullCache` no-op; do not store online state in it.

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
