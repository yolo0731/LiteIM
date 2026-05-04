# LiteIM Task Plan

## Goal

Reset the LiteIM roadmap after Step 15 toward a resume-ready C++/Qt IM project:

- Keep the C++ server focused on Linux networking: nonblocking socket, epoll, Reactor, Session lifecycle, protocol framing, and later eventfd-based one-loop-per-thread.
- Add a WeChat-style Qt Widgets desktop client so the project can be demonstrated as a real chat application.
- Keep MySQL and Redis as simple supporting components later; do not make database/cache expertise the main resume claim.
- Keep PersonaAgent integration simple: a Python BotClient logs in as a bot contact and chats through the same LiteIM protocol.

Current active phase: roadmap reset only. Do not implement the full refactor in this turn.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Check memory and repo state | complete | Read planning skill, session catchup, Git status, existing Qt placeholder, protocol definitions, and project memory. |
| Record simplified refactor direction | complete | Updated planning files so networking/Qt/Agent integration become the main line and MySQL/Redis become optional supporting topics. |
| Update Step roadmap | complete | Rewrote Step 16+ in project memory and tutorials roadmap. |
| Sync docs index | complete | Updated tutorial index and high-level docs that describe the future route. |
| Verify docs-only diff | complete | Git status confirms only markdown/planning files changed; `git diff --check` passed. |
| Review and commit | complete | Reviewed roadmap-only diff and created the roadmap reset commit. |

## Planning Hook Phase Status

### Phase 1: Check memory and repo state
**Status:** complete

### Phase 2: Record simplified refactor direction
**Status:** complete

### Phase 3: Update Step roadmap
**Status:** complete

### Phase 4: Sync docs index
**Status:** complete

### Phase 5: Verify docs-only diff
**Status:** complete

### Phase 6: Review and commit
**Status:** complete

## This Turn Scope

This turn should only reset the plan after Step 15:

- Keep current server code intact.
- Keep Step 15 as the current completed implementation point.
- Update future Step definitions in `/home/yolo/jianli/PROJECT_MEMORY.md`.
- Update `tutorials/00_roadmap.md` and tutorial index to match the new route.
- Record that Qt client and high-performance network refactors are future steps, not current implementation.

## Refactor Roadmap

Future stages should be implemented as separate steps and commits:

1. Step 16-21: service-side MVP: auth, private chat, group chat, history, heartbeat timeout, CLI client.
2. Step 22-25: networking resume highlights: `eventfd`, `queueInLoop()`, `EventLoopThreadPool`, business thread pool, and simple backpressure.
3. Step 26-30: Qt client: optional Qt target, `QTcpSocket`, TLV codec, login/register, WeChat-style main window, chat bubbles, heartbeat, and AI bot contact placeholder.
4. Step 31-32: tests, simple benchmark, README screenshots, and interview docs.
5. Optional later step: basic MySQL/Redis adapters if time allows; keep them supporting and do not make them the main claim.

## Design Boundaries

- Do not implement the full roadmap in one commit.
- Do not overstate MySQL/Redis; keep them as supporting components.
- Do not delete the completed Step 15 SQLite work during the roadmap reset. If storage changes later, do it as a separate step.
- Do not add Redis Pub/Sub, Redis Streams, Redis Cluster, distributed locks, or advanced database tuning in the first pass.
- Do not implement Qt image/file/voice/video transfer, message recall, or Moments-like features.
- Do not copy WeChat branding, icons, or assets; only use a familiar three-column chat layout.
- Do not make Qt build mandatory before CI and local Qt availability are reliable.
- Keep every stage buildable, tested, documented, and separately commit-ready.

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
