# LiteIM Task Plan

## Goal

Restart LiteIM from `Step 0` according to `/home/yolo/jianli/PROJECT_MEMORY.md`.

LiteIM is planned as a C++17 high-performance IM system:

- Linux nonblocking socket + epoll LT.
- `eventfd` task wakeup and one-loop-per-thread Reactor.
- multi-Reactor `TcpServer`.
- business `ThreadPool` for blocking MySQL / Redis work.
- `timerfd` heartbeat timeout and `signalfd` graceful shutdown.
- slow-client output-buffer backpressure.
- custom TLV binary protocol and TCP stream decoder.
- MySQL persistence and Redis online/unread/rate-limit state.
- CLI, Python E2E, benchmark, GoogleTest/gMock, ASan/UBSan, CI.
- Qt Widgets demo client with a familiar IM three-column chat layout.
- PersonaAgent integration through a Python BotClient and a separate six-node LangGraph AgentService.

## Documentation Boundary

The workspace documentation roles are now separated:

- `/home/yolo/jianli/PROJECT_MEMORY.md`: overall design, long-term route, architecture constraints, Step goals, boundaries, tests, and planned commit messages.
- `/home/yolo/jianli/AGENTS.md` and `/home/yolo/jianli/CLAUDE.md`: agent work constraints and reading order; no completed-Step status, commit hashes, or active next task.
- `LiteIM/README.md`: public project overview; no process progress, test counts, commit hashes, or default next Step.
- `LiteIM/task_plan.md`, `LiteIM/findings.md`, and `LiteIM/progress.md`: active progress, process discoveries, verification results, and actual completion records.

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
| Step 39 HistoryService recent history pagination | done | Added `HistoryService` for `HistoryRequest` / `HistoryResponse`, querying private/group recent messages through `IStorage::getHistory()`, default limit 20, max limit 50, optional `before_message_id` cursor carried by request `TlvType::MessageId`, repeated TLV response fields, server runtime registration on the business pool, private participant validation, and group member validation. Added targeted tests/docs, full build passed, full CTest 321/321 passed with local Docker MySQL/Redis, diff check passed, tutorial scans passed, and bounded server smoke reached listening state then exited through signalfd SIGTERM. No SQL schema change, JSON body mode, reliable ACK, read receipts, client UI, HeartbeatService, or BotGateway. |
| Step 38 GroupService group chat | done | Adopted scheme A: extended `GroupDao` / `IStorage` / `MySqlStorage` with `findGroupById()` and `getGroupsForUser(user_id)`, added `GroupService` for `CreateGroupRequest` / `JoinGroupRequest` / `ListGroupsRequest` / `GroupMessageRequest`, wired server runtime registration, saves group messages through `IStorage`, pushes to in-process online group members, writes offline rows and unread counts for offline members, and logs Redis unread failures after MySQL save instead of failing the sender. Added targeted tests/docs, full build passed, full CTest 312/312 passed, diff check passed, tutorial scans passed, and bounded server smoke reached listening state then exited through signalfd SIGTERM. No complex group permissions, announcements, mute, read receipts, ACK/retry, broadcast optimization, cross-node routing, BotGateway, schema change, or new TLV field introduced. |
| Pre-Step38 Critical Hardening | done | Fixed two runtime correctness issues before group chat: session close cleanup now flows from `TcpServer` to `OnlineService::unbindSession(session_id)` through the business pool, and ChatService no longer reports Redis unread increment failure as sender failure after MySQL message/offline save succeeds. Added targeted tests, synced README/tutorial/planning docs, full build passed, full CTest 303/303 passed, diff check passed, and bounded server smoke reached listening state then exited on timeout SIGTERM as expected. |
| Step 37 OfflineMessageService | done | Adopted scheme 1: login still returns only `LoginResponse`; clients pull offline messages by sending `OfflineMessagesRequest` after login. Added `OfflineMessageService` with RED/GREEN tests, optional `Limit`, max-100 cap, repeated-message TLV response fields, unread clearing per returned conversation, delivered marking for returned message ids, MySQL/Redis integration coverage, server runtime registration, README/tutorial/planning docs, full build, full CTest 297/297 with local Docker MySQL/Redis, diff check, Markdown scans, and server smoke. No reliable ACK retry, no auth/router follow-up response redesign, no group chat, no history paging, and no BotGateway. |
| Step 36 ChatService private chat | done | Added `ChatService` with RED/GREEN tests, `PrivateMessageRequest` business-thread handler, sender identity from `OnlineService`, receiver existence check, server-generated private conversation id, MySQL message/offline save through `IStorage`, in-process online `PrivateMessagePush`, offline Redis unread +1, sender `PrivateMessageResponse`, server runtime registration, README/tutorial/planning docs, full build, full CTest 291/291, diff check, Markdown scans, and server smoke. No group chat, offline pull, history query, cross-node routing, reliable ACK, friend-policy enforcement, or BotGateway. |
| Markdown audit and stale-doc cleanup | done | Checked current-facing Markdown files and tutorial template rules. No current-facing Markdown file needed deletion. Fixed duplicated `本 Step 不实现` wording in Step 34/35 tutorials, renamed the old `Current Decision` process block to `Historical Route Snapshot`, and kept useful process/debug history instead of deleting it. |
| Step tutorial Markdown compact lecture rewrite | done | Markdown-only pass rewrote `tutorials/step00_reset.md` through `tutorials/step35_friend_service.md` to the fixed 0-10 lecture template: conclusion, why, boundary, file table, core contract, runtime flow, key implementation points, test-risk coverage, verification commands, interview expression, and final interview follow-up. Synced tutorial constraints in `/home/yolo/jianli/PROJECT_MEMORY.md`, `AGENTS.md`, and `CLAUDE.md`. No C++/SQL/CMake/test behavior changes intended. Markdown structure scans and `git diff --check` passed. |
| Step 35 FriendService | done | Added `FriendService` with RED/GREEN tests, `TlvType::OnlineStatus`, business-thread handlers for `AddFriendRequest` / `ListFriendsRequest`, MySQL-backed friendship writes, duplicate-friend `AlreadyExists`, Redis-backed online-state response fields, server runtime registration, README/tutorial/planning docs, full build, full CTest 285/285, diff check, and server smoke. No friend approval, blacklist, remark name, private chat, group chat, offline messages, history, heartbeat service, or BotGateway. |
| Step 34 AuthService register and login | done | Added `AuthService` with RED/GREEN tests, PBKDF2-HMAC-SHA256 password hashing, register/login handlers over `IStorage` / `ICache` / `OnlineService`, and server runtime wiring for MySQL / Redis / AuthService. Register/login run in the business thread pool through `MessageRouter`; successful login clears failure counts, writes Redis online state, and binds `SessionManager`. Full build, full CTest 279/279, diff check, and server smoke passed. |
| Step 33 MessageRouter async dispatch framework | done | Added `MessageRouter` with RED/GREEN tests and runtime wiring. It parses request TLVs, dispatches handlers inline or through the business `ThreadPool`, returns `ErrorResponse` for invalid/unhandled paths, preserves request `seq_id`, and is wired from `server/main.cpp` through `TcpServer::setMessageCallback()`. `server/main.cpp` starts `SignalWatcher` before the business pool so worker threads inherit the blocked SIGINT/SIGTERM mask. Full build, full CTest 272/272, server smoke, and diff check passed. |
| Step 32 SessionManager and OnlineService | done | Added `liteim_service` with `SessionManager` and `OnlineService`, using a kick-old-keep-new repeated-login policy. Memory binding uses `user_id -> weak_ptr<Session>` and `session_id -> user_id`; OnlineService writes/refreshes/clears Redis-backed online state through `ICache` while stale old-session unbinds do not delete the new Redis online key. No AuthService, MessageRouter, ChatService, or TcpServer runtime protocol wiring introduced. |
| Step 31 route documentation alignment | done | Markdown-only route adjustment: formalized the former storage/cache adapter cleanup as Step 31, shifted the business-service phase to Step 32-41, shifted later phase ranges to Step 42-46 / Step 47-54 / Step 55, added `tutorials/step31_storage_cache_adapters.md`, and updated affected README/tutorial/planning references. No C++ behavior changed. |
| Markdown contract alignment / doc drift fix | done | Markdown-only pass aligning tutorial structure with the current contract: tutorials now end at interview follow-up, omit tutorial commit-message sections, use concrete LiteIM data examples in runtime-flow section 5, and current docs reflect MySQL history index fields, HeartbeatService/session activity split, `saveMessageWithOfflineRecipients()` output semantics, and 254-test verification. Not Step 31. |
| Step 31 storage/cache adapters | done | Formal storage/cache adapter Step. Added `MySqlStorage : IStorage`, `RedisCache : ICache`, `UserProfileRecord`, unsigned MySQL `bindUInt64()`, one MySQL transaction for message + offline rows, unread delta signed-range validation, tests/docs sync, and kept business runtime wiring for Step 32. |
| Step 30 UnreadCounter and LoginRateLimiter | done | Added Redis-backed `UnreadCounter` and `LoginRateLimiter` over `RedisPool`, with invalid-input unit tests, Docker-backed Redis integration tests, README/tutorial/planning docs, and full CTest verification. No AuthService, ChatService, TcpServer runtime wiring, SessionManager, OnlineService, Redis Cluster, Pub/Sub, Streams, or distributed locks introduced. |
| Step 21-29 tutorial format alignment | done | Markdown-only pass rewriting `tutorials/step21_storage_cache_interfaces.md` through `tutorials/step29_online_status_cache.md` to follow the fixed teaching structure: concept, detailed interface/contract explanation, architecture position plus internal runtime flow, tests, verification, and interview follow-up. Did not modify `step20_backpressure.md` or C++/SQL behavior. |
| Step 29 OnlineStatusCache | done | Added `OnlineStatusCache` over `RedisPool` for `online:user:<user_id>` TTL-backed online sessions, including set online, refresh TTL, set offline, online check, session lookup, value parsing, invalid-argument handling, missing-user `NotFound`, README/tutorial/planning docs, and Docker-backed Redis tests. No unread counter, login limiter, service layer, network runtime integration, SessionManager, OnlineService, Redis Cluster, Pub/Sub, Streams, or distributed locks introduced. |
| Step 28 RedisClient and RedisPool | done | Added hiredis-backed blocking `RedisClient`, fixed-size `RedisPool`, move-only `RedisConnectionGuard`, Redis auth/db selection, `ping`, `setex`, `get`, `del`, `incr`, `expire`, `eval`, acquire timeout, explicit `release()`, close semantics, borrow-time ping/reconnect, and Docker-backed tests. No online-status cache, unread counter, login limiter, service layer, network runtime integration, key schema beyond test keys, Redis Cluster, Pub/Sub, Streams, or distributed locks introduced. |
| Step 27 FriendDao and GroupDao | done | Added `FriendDao` / `GroupDao` for bidirectional idempotent friendships, friend listing through `users`, group creation with owner membership in one transaction, idempotent member join, normal-member removal, member listing, and `findGroupById()`. No Redis, service layer, network runtime integration, schema changes, group admin, mute, announcement, or approval workflow introduced. |
| Step 26 MessageDao and OfflineMessageDao | done | Added `MessageDao` / `OfflineMessageDao` for private/group message persistence, pending offline-message save/fetch/delivered flow, and conversation history cursor pagination with `limit` capped at 50. `MySqlConnection::executeSimple()` covers transaction control statements for delivered marking. No Redis, service layer, network runtime integration, or schema changes introduced. |
| Step 25 UserDao and AuthDao | done | Added `UserDao` / `AuthDao` for users-table data access, `ErrorCode::AlreadyExists`, structured duplicate-key conversion through `PreparedStatement::lastErrorNumber()`, and Docker-backed tests for create, duplicate username, find by username/id, missing user, and username existence. No service, session, Redis, message DAO, or runtime server integration introduced. |
| Step 24 MySqlPool and ConnectionGuard | done | Added fixed-size `MySqlPool`, move-only `ConnectionGuard`, blocking `acquire(timeout, guard)`, close semantics, borrow-time ping/reconnect, and Docker-backed tests for capacity, timeout, RAII return, close failure, concurrent borrow/release, and reconnect after a closed borrowed connection. No DAO, Redis client, business service, or runtime server integration introduced. |
| Step 23 MySqlConnection and PreparedStatement | done | Added `MySqlConnection`, `PreparedStatement`, `MySqlQueryResult`, real MySQL C API linking through `pkg-config mysqlclient`, and Docker-backed integration tests for ping, prepared select, insert/query, bad SQL, and special-character binding. No MySQL pool, DAO, Redis client, or runtime server integration introduced. |
| Step 22 Docker Compose and MySQL init SQL | done | Added `docker/docker-compose.yml`, MySQL schema init SQL, idempotent seed data with `alice` / `bob` / `mira_bot` / `dev_group`, README local dependency docs, Step 22 tutorial, and verified real MySQL/Redis startup with a temporary Compose project. Local development now uses MySQL 8.0 series on host port `33060`, Redis on `63790`, and password `6` for MySQL/Redis dev credentials. No C++ MySQL/Redis client, DAO, or runtime server integration introduced. |
| Step 21 storage/cache interfaces | done | Added `IStorage` / `ICache` abstractions, DTO type headers, header-only CMake interface targets, fake/null interface tests, README/tutorial/planning docs. No MySQL/Redis client, DAO, business service, or runtime dependency introduced. |
| Markdown documentation alignment | done | Markdown-only pass against `9dd671b`: aligned current-facing docs with owner-loop-only `Acceptor::close()`, removed `EventLoop::isStopped()` / stale public APIs, `SessionState`, direct `FrameDecoder` read path, heartbeat activity semantics, and `sendToSession()` NotFound semantics. `git diff --check` passed; source stale-API scan had no matches; Markdown scan leaves only allowed history/current notes. |
| Network lifecycle/API cleanup | done | Independent cleanup after Step 20: made `Acceptor::close()` owner-loop-only, removed `EventLoop::isStopped()`, removed premature public APIs, consolidated `SessionState`, tightened `sendToSession()` closed-session NotFound semantics, and added accumulated small-packet high-water coverage. |
| P0 session activity semantics fix | done | Fixed `last_active_time` so only complete inbound Packets refresh heartbeat activity; outbound server writes no longer keep an idle client alive. Targeted Session/TcpServer tests, server smoke, and full CTest 172/172 passed. |
| Documentation boundary correction | done | Clarified that PROJECT_MEMORY is long-term design, AGENTS/CLAUDE are constraints, README is public overview, and planning files hold progress/process memory. |
| Step 20 slow-client backpressure hardening | done | Added configurable per-Session output high-water mark, warning log on overflow, Config key `server.output_high_water_mark_bytes`, TcpServer propagation, slow-client close cleanup tests, server smoke, and full CTest 181/181. |
| Optional Step 18.6 Session input-path simplification | done | Removed `Session::input_buffer_` and `feedInputBuffer()`; `handleRead()` now feeds stack read bytes directly into `FrameDecoder` while preserving split/sticky packet handling, malformed-packet close, heartbeat activity semantics, and Step 20 output backpressure. |
| Step 0 concept | done | Step 0 is a cleanup/reset step, not feature implementation. |
| Step 0 delete old route files | done | Removed old source, tests, docs/tutorials, SQLite/InMemoryStorage route files, and build output. |
| Step 0 keep minimal root | done | Removed premature empty folders and `.gitkeep`; future directories will be created by the Step that needs them. |
| Step 0 docs | done | Rewrote README, findings, progress, task plan, docs, and tutorial index for the minimal Step 0 route. |
| Step 0 verification | done | CMake configure/build and CTest passed; `.gitkeep` and stale-route filename checks returned no matches. |
| Step 0 commit | done | Commit: `29e41e9 chore: keep LiteIM step0 minimal`. |
| Step 1 concept | done | Step 1 creates a minimal buildable server/test project and introduces GoogleTest from the start. |
| Step 1 code | done | Added root CMake wiring, GoogleTest FetchContent, minimal `server`, and gtest-based `tests` target. |
| Step 1 tests | done | CMake configure/build, server smoke run, and CTest passed with `SmokeTest.GoogleTestWorks`. |
| Step 1 commit | done | Commit message: `init: create LiteIM project structure with googletest`. |
| Step 2 concept | done | Step 2 introduces shared base components used by later networking, protocol, storage, cache, Qt, and tests. |
| Step 2 code | done | Added `liteim_base` with `Config`, `Logger`, `ErrorCode`, `Status`, and `Timestamp`; server now initializes logging from default config. |
| Step 2 tests | done | Added base GoogleTest coverage; CTest passed with 15 total tests. |
| Step 2 docs | done | README, docs, findings, progress, and tutorials were updated for the Step 2 route. |
| Step 2 commit | done | Commit message: `feat(base): add config logger and error code`. |
| Step 3 concept | done | Step 3 defines protocol message and TLV field types only; Packet encoding is reserved for Step 4. |
| Step 3 code | done | Added `liteim_protocol` with `MessageType`, `TlvType`, string conversion, and request/response/push classification. |
| Step 3 tests | done | Added protocol GoogleTest coverage; CTest passed with 23 total tests after push classification fix. |
| Step 3 docs | done | README, docs, findings, progress, and tutorials were updated for the Step 3 route. |
| Step 3 commit | done | Commit message: `feat(protocol): define message and tlv types`. |
| Step 4 concept | done | Step 4 defines a fixed 20-byte Packet header and keeps TLV body parsing for Step 5. |
| Step 4 code | done | Added `PacketHeader`, `Packet`, `validateHeader()`, `encodePacket()`, and `parseHeader()`. |
| Step 4 tests | done | Added Packet GoogleTest coverage; CTest passes with 33 total tests. |
| Step 4 docs | done | README, docs, findings, progress, and tutorials were updated for the Step 4 route. |
| Step 4 commit | done | Commit message: `feat(protocol): add packet encoding and header validation`. |
| Step 5 concept | done | Step 5 defines TLV body format as `type(2) + len(4) + value`. |
| Step 5 code | done | Added `TlvCodec` append, parse, getter helpers, and repeated-field storage. |
| Step 5 tests | done | Added TLV codec GoogleTest coverage; CTest passes with 45 total tests. |
| Step 5 docs | done | README, docs, findings, progress, and tutorials were updated for the Step 5 route. |
| Step 5 commit | done | Commit message: `feat(protocol): implement tlv codec`. |
| Step 6 concept | done | Step 6 defines a socket-agnostic TCP byte-stream frame decoder. |
| Step 6 code | done | Added `FrameDecoder` with internal buffering, multi-packet output, error state, and reset. |
| Step 6 tests | done | Added FrameDecoder GoogleTest coverage; CTest passes with 54 total tests. |
| Step 6 docs | done | README, docs, findings, progress, and tutorials were updated for the Step 6 route. |
| Step 6 commit | done | Commit message: `feat(protocol): implement tcp frame decoder`. |
| Step 7 concept | done | Step 7 introduces the socket-agnostic network `Buffer` used later by `Session` input/output buffers. |
| Step 7 code | done | Added `liteim_net` with `Buffer` append/retrieve/readable/writable/auto-grow behavior. |
| Step 7 tests | done | Added Buffer GoogleTest coverage; pre-check CTest passes with 67 total tests. |
| Step 7 docs | done | README, docs, findings, progress, tutorials, and PROJECT_MEMORY were updated. |
| Step 7 verification | done | CMake configure/build, server smoke, CTest, diff check, `.gitkeep`, and stale-route checks passed. |
| Step 7 commit | done | Commit message: `feat(net): add buffer abstraction`. |
| Step 8 concept | done | Step 8 introduces Linux socket helper functions used by later Acceptor/Session code. |
| Step 8 code | done | Added `SocketUtil` under `include/liteim/net/` and `src/net/`. |
| Step 8 tests | done | Added GoogleTest coverage for nonblocking sockets, socket options, invalid fds, and close behavior. |
| Step 8 docs | done | README, docs, findings, progress, tutorials, and PROJECT_MEMORY were updated for SocketUtil. |
| Step 8 verification | done | CMake configure/build, server smoke, CTest, diff check, `.gitkeep`, and stale-route path checks passed. |
| Step 8 commit | done | Commit message: `feat(net): add socket utility functions`. |
| Step 9 concept | done | Step 9 defines Reactor core interfaces only: `Epoller`, `Channel`, and `EventLoop`. |
| Step 9 code | done | Added headers under `include/liteim/net/` without implementing epoll behavior yet. |
| Step 9 tests | done | Added compile/include tests; RED failed on missing `Channel.hpp`, GREEN passes for the 3 new ReactorInterface tests. |
| Step 9 docs | done | Synced README, docs, findings, progress, tutorials, and PROJECT_MEMORY for the interface-only boundary. |
| Step 9 verification | done | CMake configure/build, server smoke, CTest 76/76, diff check, `.gitkeep`, and stale-route path checks passed. |
| Step 9 commit | done | Commit message: `feat(net): define reactor core interfaces`. |
| Step 10 concept | done | Step 10 implements the real `Epoller` wrapper over Linux epoll in LT mode. |
| Step 10 tests | done | Added real `pipe()` fd tests for add/mod/del, timeout, and invalid operations. |
| Step 10 code | done | Added `src/net/Epoller.cpp` plus minimal `Channel.cpp` state helpers required by Epoller tests. |
| Step 10 docs | done | Synced README, docs, findings, progress, tutorials, and PROJECT_MEMORY for Epoller behavior. |
| Step 10 verification | done | CMake configure/build, server smoke, CTest 81/81, diff check, `.gitkeep`, and stale-route path checks passed. |
| Step 10 commit | done | Commit message: `feat(net): implement epoller wrapper`. |
| Step 11 concept | done | Step 11 implements `Channel` event dispatching and keeps fd ownership outside `Channel`. |
| Step 11 tests | done | Added `ChannelTest` coverage for event mask changes and read/write/close/error callback dispatch. |
| Step 11 code | done | Implemented `Channel::handleEvent()`, automatic `Channel::update()`, and minimal `EventLoop` update/remove bridge to `Epoller`. |
| Step 11 docs | done | Synced README, docs, findings, progress, tutorials, and PROJECT_MEMORY for Channel behavior. |
| Step 11 verification | done | CMake configure/build, server smoke, CTest 88/88, diff check, `.gitkeep`, and stale-route path checks passed. |
| Step 11 commit | done | Commit message: `feat(net): implement channel event dispatching`. |
| Step 12 concept | done | Step 12 implements the real `EventLoop` dispatcher and eventfd wakeup path. |
| Step 12 tests | done | Added `EventLoopTest` coverage for run/queue task execution, fd event dispatch, and multiple queued tasks. |
| Step 12 code | done | Implemented `EventLoop::loop()`, `runInLoop()`, `queueInLoop()`, eventfd wakeup, and pending task execution. |
| Step 12 docs | done | Synced README, docs, findings, progress, tutorials, and PROJECT_MEMORY for EventLoop behavior. |
| Step 12 verification | done | CMake configure/build, server smoke, CTest 92/92, diff check, `.gitkeep`, and stale-route path checks passed. |
| Step 12 commit | done | Commit message: `feat(net): add event loop with eventfd task queue`. |
| Step 13 concept | done | Step 13 implements the nonblocking listen socket and accept loop boundary. |
| Step 13 tests | done | Added `Acceptor` interface, real localhost connection tests, and `UniqueFd` ownership regression tests. |
| Step 13 code | done | Implemented `Acceptor` with bind/listen, listen `Channel`, `accept4()` loop, callback, close cleanup, and `UniqueFd` fd ownership protection. |
| Step 13 docs | done | Synced README, docs, findings, progress, tutorials, and PROJECT_MEMORY for Acceptor behavior and `UniqueFd` ownership semantics. |
| Step 13 verification | done | CMake configure/build, server smoke, CTest 97/97, diff check, `.gitkeep`, and stale-route path checks passed. |
| Step 13 commit | done | Commit message: `feat(net): implement nonblocking acceptor`. |
| Step 13 review hardening concept | done | Verified external review points and only fixed confirmed local net-layer issues before Step 14. |
| Step 13 review hardening tests | done | Added regression coverage for cross-thread Acceptor close, callback exception fd cleanup, Channel tie, and UniqueFd ownership. |
| Step 13 review hardening code | done | Added `UniqueFd`, made Acceptor close cleanup run on the loop thread, and added Channel weak tie support. |
| Step 13 review hardening docs | done | Synced README/docs/tutorials/planning files and moved public roadmap link inside the repo. |
| Step 13 review hardening verification | done | CMake configure/build, server smoke, CTest 105/105, path stale-route checks, README external-link check, and final diff review passed. |
| Step 13 hardening round 2 concept | done | Evaluated external review points and accepted Logger, Epoller, EventLoop, Acceptor, and Channel hardening within the current Step 13 boundary. |
| Step 13 hardening round 2 tests | done | Added regressions for Logger level stability, EventLoop exception isolation/stopped state, Acceptor stopped close/fd exhaustion, and Channel no-copy callback dispatch. |
| Step 13 hardening round 2 code | done | Fixed Logger level reset, Epoller constructor failure semantics, EventLoop RAII wakeup/stopped/exception handling, Acceptor idle-fd close fallback, and Channel callback dispatch copies. |
| Step 13 hardening round 2 docs | done | Synced README, docs, findings, progress, task plan, and Step 2/10/11/12/13 tutorials for hardening round 2 behavior. |
| Step 13 hardening round 2 verification | done | CMake configure/build, server smoke, targeted hardening tests, and full CTest 112/112 passed. |
| Step 13 hardening round 3 concept | done | Evaluated follow-up review points and accepted the precise EventLoop stopped-state, Acceptor no-throw logging, and Channel no-copy callback contract fixes. |
| Step 13 hardening round 3 tests | done | Added regressions for the previous stopped-state API, queued tasks when quit predates loop startup, and the old cross-thread Acceptor close path; these stopped-state and cross-thread close contracts are now superseded by the network cleanup. |
| Step 13 hardening round 3 code | done | Fixed the then-existing stopped-state API, made loop drain pending tasks before honoring pre-start quit, protected Acceptor noexcept logging, and documented Channel callback lifetime requirements; the stopped-state API was later removed. |
| Step 13 hardening round 3 docs | done | Synced README, docs, findings, progress, task plan, and Step 11/12/13 tutorials for hardening round 3 behavior. |
| Step 13 hardening round 3 verification | done | CMake build, targeted hardening tests, server smoke, full CTest 116/116, and diff whitespace check passed. |
| Step 14 concept | done | Step 14 introduces `Session` as the owner of one connected fd, one `Channel`, input/output buffers, packet decode/encode, and close lifecycle. |
| Step 14 tests | done | Added Session header and socketpair tests for complete packets, half packets, sticky packets, peer close, cross-thread send, large pending output, and last-active initialization. |
| Step 14 code | done | Added `Session.hpp` / `Session.cpp`, linked `liteim_net` to `liteim_protocol`, and implemented nonblocking read/write, `FrameDecoder`, output buffering, `sendPacket()`, close cleanup, and `last_active_time`. |
| Step 14 docs | done | Synced README, docs, findings, progress, task plan, tutorial index, Step 14 tutorial, and PROJECT_MEMORY current progress snapshot. |
| Step 14 verification | done | CMake build, targeted Session tests, server smoke, full CTest 124/124, diff whitespace check, stale route file-path check, and final code review passed. |
| Step 14 commit | done | Commit message: `feat(net): implement session lifecycle and packet IO`. |
| Pre-Step 15 byte API cleanup concept | done | Before adding EventLoopThreadPool, normalize raw wire bytes through `liteim::Byte` / `liteim::Bytes` and remove public `std::string_view`/mixed byte-vector APIs. |
| Pre-Step 15 byte API cleanup code | done | Added `include/liteim/base/Types.hpp`; updated Packet, TLV, FrameDecoder, Buffer, Session, and tests to use `Byte` / `Bytes`. |
| Pre-Step 15 byte API cleanup docs | done | Synced README, docs, tutorials, findings, progress, task plan, and PROJECT_MEMORY with the normalized byte API. |
| Pre-Step 15 byte API cleanup verification | done | Build, server smoke, full CTest 124/124, diff check, stale-route check, and API stale-reference scan passed. |
| Step 15 concept | done | Step 15 implements one-loop-per-thread I/O foundations, not TcpServer or business pools. |
| Step 15 tests | done | Added RED tests for EventLoopThread headers, worker-thread loop startup/stop, EventLoopThreadPool startup, round-robin, zero-thread fallback, and distinct child loop threads. |
| Step 15 code | done | Added `EventLoopThread` and `EventLoopThreadPool`, wired them into `liteim_net`, and passed the targeted Step 15 tests. |
| Step 15 docs | done | Synced README, docs, findings, progress, tutorials, task plan, and PROJECT_MEMORY for Step 15. |
| Step 15 verification | done | Build, server smoke, targeted tests, full CTest 133/133, diff check, stale-route checks, and final diff review passed. |
| Pre-Step 16 code cleanup concept | done | Accepted external review points that strengthen Step 16 boundaries without implementing TcpServer. |
| Pre-Step 16 code cleanup tests | done | Added ByteOrder tests, Epoller owner-loop regression, and updated Acceptor callback tests to use `UniqueFd`. |
| Pre-Step 16 code cleanup code | done | Added protocol `ByteOrder.hpp`, reused it from Packet/TLV, enforced Epoller owner-loop checks, moved accepted fd ownership through `UniqueFd`, removed Acceptor duplicate listening state, replaced test-only fd guards with `UniqueFd`, and trimmed long teaching comments from production code. |
| Pre-Step 16 code cleanup docs | done | Synced README, docs, tutorials, findings, progress, task plan, and PROJECT_MEMORY with the cleanup result. |
| Pre-Step 16 code cleanup verification | done | Build, server smoke, full CTest 136/136, diff check, stale-route checks, and final diff review passed. |
| Step 16 concept | done | Step 16 implements the multi-Reactor `TcpServer` network coordinator above `Acceptor`, `EventLoopThreadPool`, and `Session`. |
| Step 16 tests | done | Added RED tests for the `TcpServer` header, echo, multi-loop connection distribution, disconnect cleanup, and cross-thread `sendToSession()` behavior. |
| Step 16 code | done | Added `TcpServer.hpp` / `TcpServer.cpp`, wired them into `liteim_net`, and passed targeted Step 16 tests. |
| Step 16 docs | done | Synced README, docs, tutorial index, Step 16 tutorial, findings, progress, task plan, and PROJECT_MEMORY for the new `TcpServer` boundary. |
| Step 16 verification | done | Build, server smoke, full CTest 142/142, diff check, `.gitkeep` check, stale-route path check, and final diff review passed. |
| Step 16 commit | done | Commit message: `feat(net): implement multi reactor tcp server`. |
| Step 17 concept | done | Step 17 introduces a fixed-size business `ThreadPool` for later MySQL / Redis / password hash / history query work. |
| Step 17 tests | done | Added RED tests for `ThreadPool` header, zero-worker rejection, task execution, multi-worker concurrency, stop rejection, worker-origin stop cleanup, concurrent stop serialization, destructor drain, and queue length. |
| Step 17 code | done | Added `liteim_concurrency` with `ThreadPool`, `submit()`, graceful `stop()`, `running_` state, serialized owner cleanup, worker loop, exception isolation, worker-origin stop cleanup, and queue length tracking. |
| Step 17 docs | done | Synced README, docs, tutorial index, Step 17 tutorial, debug case notes, findings, progress, task plan, and PROJECT_MEMORY for the business thread pool boundary. |
| Step 17 verification | done | Build, server smoke, full CTest 151/151, diff check, `.gitkeep` check, stale-route path check, and final diff review passed. |
| Step 17 commit | done | Commit message: `feat(concurrency): add business thread pool`. |
| Step 17 review hardening concept | done | Evaluated external review points and accepted only locally reproducible lifecycle / high-water / session-id / event-dispatch bugs. |
| Step 17 review hardening tests | done | Added regressions for EventLoopThread self-stop owner cleanup, Acceptor queued-close exit race, Session output high-water close, Channel error+read dispatch, and TcpServer logical session id API. |
| Step 17 review hardening code | done | Moved EventLoopThread state cleanup to threadFunc exit, fixed Acceptor close wait fallback, added Session id/high-water mark, changed TcpServer sessions_ to logical uint64 ids, and let Channel continue read dispatch after EPOLLERR. |
| Step 17 review hardening docs | done | Synced README, architecture, roadmap, project layout, Step 11/13/14/15/16 tutorials, findings, progress, PROJECT_MEMORY, and added net lifecycle debug case notes. |
| Step 17 review hardening verification | done | Build, full CTest 155/155, server smoke, diff check, `.gitkeep` check, stale-route path check, and final diff review passed. |
| Step 18 concept | done | Step 18 adds a timer module and wires timerfd-driven heartbeat timeout into TcpServer without adding HeartbeatService, login state, MySQL, or Redis. |
| Step 18 tests | done | Added RED tests for TimerHeap, TimerManager timerfd firing, idle session close, active session survival, and lazy deletion safety. |
| Step 18 code | done | Added `TimerHeap` / `TimerManager`, connected a base-loop timer to `TcpServer`, and kept all Session closes on the owning I/O loop. |
| Step 18 docs | done | Synced README, docs, tutorials, findings, progress, task plan, and PROJECT_MEMORY for the timerfd heartbeat boundary. |
| Step 18 verification | done | Build, server smoke, targeted timer/TcpServer tests, full CTest 164/164, diff check, `.gitkeep`, stale-route checks, and final diff review passed. |
| Step 18 commit | done | Commit message: `feat(timer): integrate timerfd heartbeat timeout`. |
| Step 18.5 concept | done | Accepted muduo-style owner-loop lifecycle hardening before signalfd, without mixing Session input/state rewrites. |
| Step 18.5 tests | done | Added owner-loop death tests and Session `UniqueFd` / `pendingOutputBytes()` interface coverage. |
| Step 18.5 code | done | `TcpServer` and `TimerManager` stop/destruct are owner-loop-only; `Session` now receives `UniqueFd`; server main starts a real echo server. |
| Step 18.5 docs | done | Synced README, tutorials, findings, progress, task plan, and PROJECT_MEMORY with lifecycle hardening result. |
| Step 18.5 verification | done | Build, server smoke, targeted tests, full CTest 167/167, diff check, stale-route checks, and final diff review passed. |
| Step 18.5 commit | done | Commit: `146353a refactor(net): harden reactor owner loop lifetimes`. |
| PROJECT_MEMORY markdown alignment | done | Merged muduo rewrite into the single authoritative `/home/yolo/jianli/PROJECT_MEMORY.md`, updated constraint docs, and translated the net lifecycle debug case to Chinese. |
| Step 19 concept | done | Step 19 adds process-level graceful shutdown by routing SIGINT/SIGTERM through signalfd into the base EventLoop. |
| Step 19 tests | done | Added SignalWatcher API/signalfd owner-loop tests and a real liteim_server SIGTERM CTest script. |
| Step 19 code | done | Added `SignalWatcher`, wired it into `liteim_net`, and updated `server/main.cpp` to stop `TcpServer` then quit the base loop. |
| Step 19 docs | done | Synced README, Step 19 tutorial, planning files, PROJECT_MEMORY, AGENTS, and CLAUDE. |
| Step 19 verification | done | Configure/build, targeted signal tests, server SIGTERM smoke, full CTest 171/171, diff check, and stale-route scans passed. |
| Step 19 commit | done | Commit message: `feat(server): add signalfd graceful shutdown`. |

## Historical Route Snapshot

This section is historical process memory from the route-planning phase. The authoritative current progress is the `Current Phase` table near the top of this file plus `findings.md` and `progress.md`; the authoritative long-term route remains `/home/yolo/jianli/PROJECT_MEMORY.md`.

Route status at the time this snapshot was last refreshed:

- Step 18 `TimerManager + timerfd heartbeat timeout` is complete.
- Step 18.5 `muduo-style lifecycle ownership hardening` is complete.
- Step 19 `signalfd graceful shutdown` is complete.
- Step 20 `slow-client backpressure hardening` is complete.
- Optional Step 18.6 `Session` input-path simplification is complete.
- Optional Step 18.7 `Session` state consolidation is complete as part of the independent network cleanup.
- Step 21 `IStorage / ICache abstractions` is complete.
- Step 22 `Docker Compose and MySQL init SQL` is complete.
- Step 23 `MySqlConnection and PreparedStatement` is complete.
- Step 24 `MySqlPool and ConnectionGuard` is complete.
- Step 25 `UserDao and AuthDao` is complete.
- Step 26 `MessageDao and OfflineMessageDao` is complete.
- Step 27 `FriendDao and GroupDao` is complete.
- Step 28 `RedisClient and RedisPool` is complete.
- Step 29 `OnlineStatusCache` is complete.
- Step 30 `UnreadCounter and LoginRateLimiter` is complete.
- Step 31 `MySqlStorage and RedisCache adapters` is complete.
- Step 32 `SessionManager and OnlineService` is complete.
- Step 33 `MessageRouter async dispatch framework` is complete.
- Step 34 `AuthService register and login` is complete.
- Step 35 `FriendService` is complete.
- Step 36 `ChatService private chat` is complete.
- Default next implementation step is Step 39 `HistoryService`.

LiteIM phases:

1. Step 0: reset workspace and keep only the minimal current-step files.
2. Step 1-20: high-performance network base and multi-Reactor echo server.
3. Step 21-31: MySQL / Redis storage and cache layer, ending with the `IStorage` / `ICache` adapters.
4. Step 32-41: async IM business services and BotGateway.
5. Step 42-46: CLI, Python E2E, benchmark, GoogleTest/gMock, ASan/UBSan, CI.
6. Step 47-54: Qt Widgets demo client.
7. Step 55: README, architecture diagrams, Qt screenshots, benchmark report, and interview docs.
8. PersonaAgent Step 1-6: Python BotClient, protocol compatibility, FastAPI `/chat`, login/heartbeat/reconnect, Echo Bot.
9. PersonaAgent Step 7-20: six-node LangGraph + Knowledge/Memory/Authorized Style RAG + Persona + Safety + Tool Calling + Checkpoint + Trace + Evaluation.

## Important Boundaries

- Do not continue the old single-Reactor business baseline.
- Do not make `InMemoryStorage` the main storage path. It may only be a future test double/mock.
- Do not reintroduce SQLite.
- Do not use Boost.Asio.
- Do not run MySQL / Redis calls in I/O threads.
- Do not let business threads directly mutate `Session`; responses must go through `EventLoop::queueInLoop()` or `runInLoop()`.
- Do not stop or destroy `TcpServer` outside the base loop thread.
- Do not stop or destroy `TimerManager` outside its owner loop thread.
- Do not queue destructor/stop cleanup as `queueInLoop([this] { ... })` for Reactor-owned objects.
- Keep accepted fd ownership on `UniqueFd` from `Acceptor` through `TcpServer` into `Session`.
- Treat `Session::pendingOutputBytes()` as owner-loop-only.
- Do not embed Python/LangGraph into the C++ server; PersonaAgent connects as a BotClient.
- PersonaAgent uses six core LangGraph nodes: `dialogue_policy`, `retrieve`, `tool_router`, `generate_reply`, `safety_check`, and `send_message`.
- Authorized Style RAG data must have consent manifest, source metadata, allowed usage, PII redaction, revocation support, and SafetyGuard protection.
- LiteIM should only expose BotGateway/protocol integration points; Knowledge/Memory/Style RAG, Tool Calling, Trace, Checkpoint, and Evaluation belong to PersonaAgent.
- Do not use WeChat logo, name, icons, or assets in the Qt client.
- Qt is a demo layer; service logic stays in the server.

## Historical Step 0 Kept Files

- `.gitignore`
- `LICENSE`
- `CMakeLists.txt` as an empty Step 0 scaffold
- `README.md`
- `task_plan.md`
- `findings.md`
- `progress.md`
- `tutorials/step00_reset.md`

Step 0 intentionally does not keep empty future directories or `.gitkeep` files.

## Step 1 Target

Step 1 should add the first real buildable project structure:

```text
LiteIM/
├── CMakeLists.txt
├── server/main.cpp
├── server/CMakeLists.txt
├── tests/test_main.cpp
└── tests/CMakeLists.txt
```

Step 1 intentionally does not create `include/`, `src/`, `client_qt/`, `bench/`, `scripts`, or `docker`. It does introduce GoogleTest through CMake `FetchContent` because all later steps should use gtest cases instead of a custom test framework.
Those directories will be created in the Step that first needs them.

Step 1 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

## Step 2 Target

Step 2 adds the first real reusable library module:

```text
LiteIM/
├── include/liteim/base/
│   ├── Config.hpp
│   ├── ErrorCode.hpp
│   ├── Logger.hpp
│   ├── Status.hpp
│   └── Timestamp.hpp
├── src/
│   ├── CMakeLists.txt
│   └── base/
│       ├── CMakeLists.txt
│       ├── Config.cpp
│       ├── ErrorCode.cpp
│       ├── Logger.cpp
│       ├── Status.cpp
│       └── Timestamp.cpp
└── tests/base/
    ├── config_test.cpp
    ├── error_code_test.cpp
    ├── logger_test.cpp
    └── timestamp_test.cpp
```

Step 2 intentionally creates only the `base` module. Protocol, network, MySQL, Redis, CLI, Qt, benchmark, scripts, and Docker directories still wait for their own Steps.

Step 2 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected tests:

- `TEST(ConfigTest, DefaultsContainExpectedValues)`
- `TEST(ConfigTest, LoadFromFileOverridesConfiguredValues)`
- `TEST(ConfigTest, MissingValuesKeepDefaults)`
- `TEST(ConfigTest, MissingFileReturnsNotFound)`
- `TEST(ConfigTest, UnknownKeyFails)`
- `TEST(ConfigTest, InvalidPortFails)`
- `TEST(ErrorCodeTest, ToStringReturnsReadableNames)`
- `TEST(StatusTest, OkStatusHasOkCode)`
- `TEST(StatusTest, ErrorStatusCarriesCodeAndMessage)`
- `TEST(LoggerTest, ParseLogLevelReturnsExpectedLevel)`
- `TEST(LoggerTest, UnknownLogLevelFallsBackToInfo)`
- `TEST(LoggerTest, InitCreatesReusableLogger)`
- `TEST(TimestampTest, NowReturnsPositiveEpochMilliseconds)`
- `TEST(TimestampTest, Iso8601StringUsesUtcFormat)`

Next Step: `Step 3: define MessageType and TLV types`.

## Step 3 Target

Step 3 adds the first protocol module:

```text
LiteIM/
├── include/liteim/protocol/
│   ├── MessageType.hpp
│   └── Tlv.hpp
├── src/protocol/
│   ├── CMakeLists.txt
│   ├── MessageType.cpp
│   └── Tlv.cpp
└── tests/protocol/
    ├── message_type_test.cpp
    └── tlv_type_test.cpp
```

Step 3 intentionally defines only protocol type identifiers and classification helpers. It does not create `PacketHeader`, encode/decode TLV, handle byte order, manage buffers, or implement TCP half-packet/sticky-packet logic.

Step 3 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(MessageTypeTest, CoreTypesReturnReadableNames)`
- `TEST(MessageTypeTest, UnknownTypeReturnsUnknown)`
- `TEST(MessageTypeTest, RequestTypesAreClassified)`
- `TEST(MessageTypeTest, ResponseTypesAreClassified)`
- `TEST(MessageTypeTest, PushTypesAreClassified)`
- `TEST(MessageTypeTest, UnknownTypesAreNotClassified)`
- `TEST(TlvTypeTest, CoreTypesReturnReadableNames)`
- `TEST(TlvTypeTest, UnknownTypeReturnsUnknown)`

## Step 4 Target

Step 4 extends the protocol module with Packet header encoding and validation:

```text
LiteIM/
├── include/liteim/protocol/
│   ├── MessageType.hpp
│   ├── Packet.hpp
│   └── Tlv.hpp
├── src/protocol/
│   ├── CMakeLists.txt
│   ├── MessageType.cpp
│   ├── Packet.cpp
│   └── Tlv.cpp
└── tests/protocol/
    ├── message_type_test.cpp
    ├── packet_test.cpp
    └── tlv_type_test.cpp
```

Step 4 intentionally implements only fixed Packet header handling. It does not encode TLV fields, decode TCP streams, create Buffer, create FrameDecoder, or enter socket / epoll / Reactor code.

Step 4 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(PacketTest, EncodePacketThenParseHeader)`
- `TEST(PacketTest, EmptyBodyCanBeEncoded)`
- `TEST(PacketTest, Utf8BodyCanBeEncoded)`
- `TEST(PacketTest, HeaderUsesNetworkByteOrder)`
- `TEST(PacketTest, InvalidMagicReturnsError)`
- `TEST(PacketTest, InvalidVersionReturnsError)`
- `TEST(PacketTest, OversizedBodyLengthReturnsError)`
- `TEST(PacketTest, EncodingOversizedBodyReturnsError)`
- `TEST(PacketTest, IncompleteHeaderReturnsError)`
- `TEST(PacketTest, NullHeaderDataReturnsError)`

## Step 5 Target

Step 5 extends the protocol module with TLV body encoding and parsing:

```text
LiteIM/
├── include/liteim/protocol/
│   ├── MessageType.hpp
│   ├── Packet.hpp
│   ├── Tlv.hpp
│   └── TlvCodec.hpp
├── src/protocol/
│   ├── CMakeLists.txt
│   ├── MessageType.cpp
│   ├── Packet.cpp
│   ├── Tlv.cpp
│   └── TlvCodec.cpp
└── tests/protocol/
    ├── message_type_test.cpp
    ├── packet_test.cpp
    ├── tlv_type_test.cpp
    └── tlv_codec_test.cpp
```

Step 5 intentionally implements only TLV body field encoding and parsing. It does not decode TCP streams, create Buffer, create FrameDecoder, or enter socket / epoll / Reactor code.

Step 5 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(TlvCodecTest, StringFieldCanBeEncodedAndDecoded)`
- `TEST(TlvCodecTest, MultipleFieldsCanBeEncodedAndDecoded)`
- `TEST(TlvCodecTest, Utf8StringCanBeEncodedAndDecoded)`
- `TEST(TlvCodecTest, RepeatedStringFieldsArePreserved)`
- `TEST(TlvCodecTest, RepeatedUint64FieldsArePreserved)`
- `TEST(TlvCodecTest, Uint64UsesNetworkByteOrder)`
- `TEST(TlvCodecTest, TlvLengthOutOfBoundsReturnsError)`
- `TEST(TlvCodecTest, IncompleteTlvHeaderReturnsError)`
- `TEST(TlvCodecTest, MissingStringFieldReturnsError)`
- `TEST(TlvCodecTest, MissingUint64FieldReturnsError)`
- `TEST(TlvCodecTest, WrongUint64LengthReturnsError)`
- `TEST(TlvCodecTest, UnknownTypeCannotBeEncoded)`

## Step 6 Target

Step 6 extends the protocol module with TCP byte-stream frame decoding:

```text
LiteIM/
├── include/liteim/protocol/
│   ├── FrameDecoder.hpp
│   ├── MessageType.hpp
│   ├── Packet.hpp
│   ├── Tlv.hpp
│   └── TlvCodec.hpp
├── src/protocol/
│   ├── CMakeLists.txt
│   ├── FrameDecoder.cpp
│   ├── MessageType.cpp
│   ├── Packet.cpp
│   ├── Tlv.cpp
│   └── TlvCodec.cpp
└── tests/protocol/
    ├── frame_decoder_test.cpp
    ├── message_type_test.cpp
    ├── packet_test.cpp
    ├── tlv_type_test.cpp
    └── tlv_codec_test.cpp
```

Step 6 intentionally implements only socket-agnostic stream decoding. It does not create the network `Buffer`, socket helpers, epoll, Reactor, or `Session`.

Step 6 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(FrameDecoderTest, CompletePacketEmitsOnePacket)`
- `TEST(FrameDecoderTest, PacketSplitAcrossFeedsEmitsAfterSecondFeed)`
- `TEST(FrameDecoderTest, MultiplePacketsInOneFeedAreDecoded)`
- `TEST(FrameDecoderTest, HalfPacketThenStickyPacketAreDecodedTogether)`
- `TEST(FrameDecoderTest, InvalidMagicEntersErrorState)`
- `TEST(FrameDecoderTest, InvalidVersionEntersErrorState)`
- `TEST(FrameDecoderTest, OversizedBodyLengthEntersErrorState)`
- `TEST(FrameDecoderTest, ErrorStateRejectsFurtherFeedUntilReset)`
- `TEST(FrameDecoderTest, NullInputWithNonzeroLengthReturnsError)`

## Step 7 Target

Step 7 creates the first network utility module:

```text
LiteIM/
├── include/liteim/net/
│   └── Buffer.hpp
├── src/net/
│   ├── Buffer.cpp
│   └── CMakeLists.txt
└── tests/net/
    └── buffer_test.cpp
```

Step 7 intentionally implements only a socket-agnostic byte buffer. It does not create socket helpers, epoll, Reactor, `Session`, or cross-thread sending.

Step 7 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(BufferTest, DefaultBufferHasNoReadableBytes)`
- `TEST(BufferTest, AppendIncreasesReadableBytes)`
- `TEST(BufferTest, AppendStringStoresReadableData)`
- `TEST(BufferTest, AppendBytePointerStoresBytes)`
- `TEST(BufferTest, RetrieveAdvancesReadIndex)`
- `TEST(BufferTest, RetrieveAllResetsBuffer)`
- `TEST(BufferTest, RetrieveAllAsStringReturnsReadableDataAndClearsBuffer)`
- `TEST(BufferTest, AppendExpandsWhenNeeded)`
- `TEST(BufferTest, AppendCompactsReadableDataBeforeExpanding)`
- `TEST(BufferTest, AppendExpandsAndPreservesExistingData)`
- `TEST(BufferTest, RetrievePastReadableBytesReturnsError)`
- `TEST(BufferTest, NullAppendWithNonzeroLengthReturnsError)`
- `TEST(BufferTest, NullAppendWithZeroLengthIsOk)`

## Step 8 Target

Step 8 extends the network module with Linux socket helpers:

```text
LiteIM/
├── include/liteim/net/
│   ├── Buffer.hpp
│   └── SocketUtil.hpp
├── src/net/
│   ├── Buffer.cpp
│   ├── SocketUtil.cpp
│   └── CMakeLists.txt
└── tests/net/
    ├── buffer_test.cpp
    └── socket_util_test.cpp
```

Step 8 intentionally implements only small socket utility wrappers. It does not create `Epoller`, `Channel`, `EventLoop`, `Acceptor`, `Session`, or `TcpServer`.

Step 8 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(SocketUtilTest, CreateNonBlockingSocketReturnsNonblockingFd)`
- `TEST(SocketUtilTest, SetNonBlockingMarksPlainSocketNonblocking)`
- `TEST(SocketUtilTest, SocketOptionsCanBeEnabled)`
- `TEST(SocketUtilTest, InvalidFdReturnsError)`
- `TEST(SocketUtilTest, CloseFdInvalidatesDescriptorAndCanBeRepeated)`
- `TEST(SocketUtilTest, GetSocketErrorReturnsCurrentSoError)`

Next Step: `Step 9: define Epoller / Channel / EventLoop interface`.

## Step 9 Target

Step 9 defines the Reactor core interfaces without implementing runtime epoll behavior:

```text
LiteIM/
├── include/liteim/net/
│   ├── Buffer.hpp
│   ├── Channel.hpp
│   ├── Epoller.hpp
│   ├── EventLoop.hpp
│   └── SocketUtil.hpp
└── tests/net/
    ├── buffer_test.cpp
    ├── channel_header_test.cpp
    ├── epoller_header_test.cpp
    ├── event_loop_header_test.cpp
    └── socket_util_test.cpp
```

Step 9 intentionally defines only headers. It does not create `src/net/Epoller.cpp`, `src/net/Channel.cpp`, `src/net/EventLoop.cpp`, does not call `epoll_create1()` / `epoll_ctl()` / `epoll_wait()`, and does not create `Acceptor`, `Session`, or `TcpServer`.

Step 9 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(ReactorInterfaceTest, ChannelHeaderIsSelfContained)`
- `TEST(ReactorInterfaceTest, EpollerHeaderIsSelfContained)`
- `TEST(ReactorInterfaceTest, EventLoopHeaderIsSelfContained)`

Next Step: `Step 10: implement Epoller`.

## Step 10 Target

Step 10 implements the real Linux epoll wrapper behind the Step 9 interface:

```text
LiteIM/
├── include/liteim/net/
│   ├── Channel.hpp
│   ├── Epoller.hpp
│   └── EventLoop.hpp
├── src/net/
│   ├── Channel.cpp
│   ├── Epoller.cpp
│   ├── Buffer.cpp
│   ├── SocketUtil.cpp
│   └── CMakeLists.txt
└── tests/net/
    ├── epoller_test.cpp
    ├── channel_header_test.cpp
    ├── epoller_header_test.cpp
    └── event_loop_header_test.cpp
```

Step 10 intentionally implements only `Epoller` plus minimal `Channel` state helpers needed to test epoll registration. It does not implement `Channel::handleEvent()` callback dispatch, automatic `Channel::update()` wiring, `EventLoop::loop()`, `eventfd`, `Acceptor`, `Session`, or `TcpServer`.

Step 10 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(EpollerTest, AddChannelReceivesReadableEvent)`
- `TEST(EpollerTest, ModifyChannelToWriteInterestTakesEffect)`
- `TEST(EpollerTest, RemoveChannelStopsEvents)`
- `TEST(EpollerTest, PollTimeoutReturnsEmptyActiveList)`
- `TEST(EpollerTest, InvalidChannelOperationsReturnError)`
- `TEST(EpollerTest, RejectsChannelOwnedByDifferentEventLoop)`

Next Step: `Step 11: implement Channel`.

## Step 11 Target

Step 11 implements the real `Channel` event dispatching behind the Step 9 interface:

```text
LiteIM/
├── include/liteim/net/
│   ├── Channel.hpp
│   ├── Epoller.hpp
│   └── EventLoop.hpp
├── src/net/
│   ├── Channel.cpp
│   ├── EventLoop.cpp
│   ├── Epoller.cpp
│   ├── Buffer.cpp
│   ├── SocketUtil.cpp
│   └── CMakeLists.txt
└── tests/net/
    ├── channel_test.cpp
    ├── channel_header_test.cpp
    ├── epoller_header_test.cpp
    ├── epoller_test.cpp
    └── event_loop_header_test.cpp
```

Step 11 intentionally implements only `Channel::handleEvent()` callback dispatch, event mask changes, and the minimal `EventLoop` bridge needed for `Channel::update()` to reach `Epoller`. It does not implement `EventLoop::loop()`, `eventfd`, task queues, `Acceptor`, `Session`, or `TcpServer`.

Step 11 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(ChannelTest, EnableAndDisableEventsUpdateInterestMask)`
- `TEST(ChannelTest, ReadableEventInvokesReadCallback)`
- `TEST(ChannelTest, WritableEventInvokesWriteCallback)`
- `TEST(ChannelTest, ReadWriteEventInvokesCallbacksInStableOrder)`
- `TEST(ChannelTest, HangupWithoutReadableEventInvokesCloseOnly)`
- `TEST(ChannelTest, ErrorEventInvokesErrorCallback)`
- `TEST(ChannelTest, HandleEventToleratesMissingCallbacks)`

Next Step: `Step 12: implement EventLoop + eventfd task dispatch`.

## Step 12 Target

Step 12 implements the real `EventLoop` event dispatch loop and `eventfd` task wakeup path:

```text
LiteIM/
├── include/liteim/net/
│   ├── Channel.hpp
│   ├── Epoller.hpp
│   └── EventLoop.hpp
├── src/net/
│   ├── Channel.cpp
│   ├── EventLoop.cpp
│   ├── Epoller.cpp
│   ├── Buffer.cpp
│   ├── SocketUtil.cpp
│   └── CMakeLists.txt
└── tests/net/
    ├── event_loop_test.cpp
    ├── event_loop_header_test.cpp
    ├── channel_test.cpp
    ├── epoller_test.cpp
    └── socket_util_test.cpp
```

Step 12 intentionally implements only `EventLoop::loop()`, `runInLoop()`, `queueInLoop()`, internal `eventfd` wakeup, pending task execution, and active `Channel` dispatch. It does not implement `Acceptor`, `Session`, `TcpServer`, `EventLoopThread`, `EventLoopThreadPool`, business thread pool, MySQL, or Redis.

Step 12 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(EventLoopTest, RunInLoopExecutesImmediatelyOnOwnerThread)`
- `TEST(EventLoopTest, QueueInLoopFromOtherThreadWakesAndExecutesTask)`
- `TEST(EventLoopTest, LoopHandlesRegisteredFdEvent)`
- `TEST(EventLoopTest, QueueInLoopRunsMultipleTasksAfterWakeup)`

Next Step: `Step 13: implement Acceptor`.

## Step 13 Target

Step 13 implements the nonblocking `Acceptor` listen socket and new-connection callback boundary. It also introduces `UniqueFd`, a small RAII owner for Linux fd lifetime, because `Acceptor` needs explicit ownership for both the long-lived listen fd and the short-lived accepted fd before the callback takes over.

```text
LiteIM/
├── include/liteim/net/
│   ├── Acceptor.hpp
│   ├── Channel.hpp
│   ├── Epoller.hpp
│   ├── EventLoop.hpp
│   └── UniqueFd.hpp
├── src/net/
│   ├── Acceptor.cpp
│   ├── Channel.cpp
│   ├── EventLoop.cpp
│   ├── Epoller.cpp
│   ├── Buffer.cpp
│   ├── SocketUtil.cpp
│   ├── UniqueFd.cpp
│   └── CMakeLists.txt
└── tests/net/
    ├── acceptor_header_test.cpp
    ├── acceptor_test.cpp
    ├── event_loop_test.cpp
    ├── channel_test.cpp
    ├── epoller_test.cpp
    ├── socket_util_test.cpp
    └── unique_fd_test.cpp
```

Step 13 intentionally implements only listen socket creation, socket options, bind/listen, listen fd registration in `EventLoop`, `accept4()` loop to `EAGAIN`, new-connection callback, fd RAII cleanup through `UniqueFd`, and listen fd cleanup. `UniqueFd` exists to make fd ownership explicit: destructor closes the owned fd, `release()` hands ownership to the next layer without closing, move transfers ownership, and `reset()` closes the old fd before taking a new one. It does not implement `Session`, `TcpServer`, `EventLoopThread`, `EventLoopThreadPool`, business thread pool, MySQL, or Redis.

Step 13 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(ReactorInterfaceTest, AcceptorHeaderIsSelfContained)`
- `TEST(AcceptorTest, ServerCanListenOnEphemeralPort)`
- `TEST(AcceptorTest, ClientConnectionTriggersNewConnectionCallback)`
- `TEST(AcceptorTest, MultiplePendingConnectionsAreAccepted)`
- `TEST(AcceptorTest, ClosedListenSocketRejectsNewConnections)`
- `TEST(AcceptorTest, AcceptedFdIsClosedWhenCallbackThrowsBeforeTakingOwnership)`
- `TEST(AcceptorTest, CloseFromNonOwnerThreadTerminates)`
- `TEST(AcceptorTest, FdExhaustionRejectsPendingConnectionWithoutLaterCallback)`
- `TEST(UniqueFdTest, DestructorClosesOwnedFd)`
- `TEST(UniqueFdTest, ReleaseReturnsFdWithoutClosing)`
- `TEST(UniqueFdTest, MoveTransfersOwnership)`
- `TEST(UniqueFdTest, ResetClosesPreviousFd)`
- `TEST(ChannelTest, TiedExpiredOwnerSkipsCallbacks)`
- `TEST(ChannelTest, TiedOwnerStaysAliveDuringCallback)`
- `TEST(ChannelTest, HandleEventDoesNotCopyStoredCallbacks)`
- `TEST(EventLoopTest, ChannelCallbackExceptionDoesNotEscapeLoop)`
- `TEST(EventLoopTest, IsStoppedBecomesTrueAfterLoopReturns)`
- `TEST(LoggerTest, GetDoesNotResetConfiguredLevel)`
- `TEST(LoggerTest, SetLevelSurvivesLaterGetCalls)`

## Step 14 Target

Step 14 adds the single-connection owner in the network module:

```text
LiteIM/
├── include/liteim/net/
│   ├── Session.hpp
│   └── ...
├── src/net/
│   ├── Session.cpp
│   └── CMakeLists.txt
└── tests/net/
    ├── session_header_test.cpp
    └── session_test.cpp
```

Step 14 intentionally implements only one connected fd lifecycle: `Session` owns the fd,
holds a `Channel`, reads bytes into input `Buffer`, feeds `FrameDecoder`, invokes message
callback for complete `Packet`, buffers encoded outgoing packets, writes output on
`EPOLLOUT`, supports cross-thread `sendPacket()` through the owning `EventLoop`, closes the
channel/fd in the loop thread, and maintains `last_active_time`.

It does not implement `TcpServer`, `EventLoopThread`, `EventLoopThreadPool`, business
thread pool, MySQL, Redis, login/chat routing, heartbeat timeout, or output-buffer
high-water-mark policy.

Step 14 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(ReactorInterfaceTest, SessionHeaderIsSelfContained)`
- `TEST(SessionTest, CompletePacketInvokesMessageCallback)`
- `TEST(SessionTest, HalfPacketDoesNotInvokeMessageCallback)`
- `TEST(SessionTest, StickyPacketsInvokeCallbackForEachPacket)`
- `TEST(SessionTest, PeerCloseInvokesCloseCallback)`
- `TEST(SessionTest, SendPacketFromOtherThreadDeliversEncodedPacket)`
- `TEST(SessionTest, LargePacketLeavesPendingOutputWhenPeerDoesNotRead)`
- `TEST(SessionTest, LastActiveTimeIsInitialized)`

Step 15 completed `EventLoopThread` and `EventLoopThreadPool` with worker-thread loop startup, safe stop/join behavior, round-robin loop selection, and zero-thread base-loop fallback.

Pre-Step 16 cleanup completed the code hygiene items needed before `TcpServer` takes ownership of accepted connections:

- `include/liteim/protocol/ByteOrder.hpp` now holds shared big-endian read/write helpers for Packet and TLV wire data.
- `Epoller::owner_loop_` is active: `updateChannel()` and `removeChannel()` reject channels owned by a different `EventLoop`.
- `Acceptor::NewConnectionCallback` now receives `UniqueFd` by value, so accepted fd ownership is expressed in the callback type instead of through a bare `int` plus manual `release()`.
- `Acceptor::listening()` is derived from `listen_fd_`, so the duplicate `listening_` flag is gone.
- `tests/net/acceptor_test.cpp` and `tests/net/socket_util_test.cpp` use production `UniqueFd` instead of old test-only `FdGuard` helpers.
- Long teaching comments were removed from production source and left for tutorials/docs.

Pre-Step 16 cleanup verification baseline:

- `TEST(ByteOrderTest, AppendsUnsignedIntegersAsBigEndianBytes)`
- `TEST(ByteOrderTest, ReadsUnsignedIntegersFromBigEndianBytes)`
- `TEST(EpollerTest, RejectsChannelOwnedByDifferentEventLoop)`
- Acceptor callback tests now assert the `UniqueFd` ownership signature.
- Full CTest count is now 136 tests.

## Step 18 Target

Step 18 adds timer infrastructure and server-side idle connection cleanup:

```text
LiteIM/
├── include/liteim/timer/
│   ├── TimerHeap.hpp
│   └── TimerManager.hpp
├── src/timer/
│   ├── CMakeLists.txt
│   ├── TimerHeap.cpp
│   └── TimerManager.cpp
└── tests/timer/
    ├── timer_heap_header_test.cpp
    ├── timer_heap_test.cpp
    ├── timer_manager_header_test.cpp
    └── timer_manager_test.cpp
```

Step 18 intentionally implements only:

- `TimerHeap`: one-shot timer ids, min-heap by expiration time, callback storage, cancellation, and lazy deletion of cancelled or stale heap entries.
- `TimerManager`: `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)`, owner-loop `Channel`, periodic tick interval, expired callback dispatch, and `stop()` cleanup.
- `TcpServer` idle heartbeat timeout: base `EventLoop` owns the timer; every tick checks a session snapshot; sessions idle for 90 seconds are closed through their owning loop.
- `Session` activity refresh on decoded incoming packets.

Step 18 does not implement login heartbeat packets, `HeartbeatService`, user online state, MySQL, Redis, signalfd shutdown, business routing, or client-side reconnect.

Step 18 verification:

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

Expected new tests:

- `TEST(TimerInterfaceTest, TimerHeapHeaderIsSelfContained)`
- `TEST(TimerInterfaceTest, TimerManagerHeaderIsSelfContained)`
- `TEST(TimerHeapTest, PopExpiredRunsCallbacksInDeadlineOrder)`
- `TEST(TimerHeapTest, CancelUsesLazyDeletionWithoutRemovingNewTimer)`
- `TEST(TimerHeapTest, PopExpiredIgnoresFutureTimers)`
- `TEST(TimerManagerTest, TimerFdTickRunsExpiredTimer)`
- `TEST(TimerManagerTest, CancelledTimerDoesNotRun)`
- `TEST(TcpServerTest, IdleSessionIsClosedByHeartbeatTimeout)`
- `TEST(TcpServerTest, ActiveSessionSurvivesHeartbeatTimeout)`

Step 18 is complete. Step 19 implements `signalfd` graceful shutdown.

## Persistent Requirements

- Every Step follows: concept -> code -> tests -> commit.
- Every Step tutorial explains new public functions, important private helpers, tests, edge cases, and interview talking points.
- Tests must be explained, not just listed.
- Tests use GoogleTest from Step 1; later Step plans should name concrete `TEST` / `TEST_F` / `TEST_P` cases when possible.
- README benchmark numbers must only use real local benchmark results.
- Each code Step must build and pass relevant tests before commit.

## Errors Encountered

| Error | Attempt | Resolution |
| --- | --- | --- |
| Existing worktree contained old SQLite/InMemoryStorage route files | Step 0 cleanup | Deleted old implementation files. |
| Step 0 initially created all future directories with `.gitkeep` | User review | Removed them; future directories will be created only when each Step needs them. |
| Sandbox `bwrap` uid map failure | Running shell commands | Used approved escalated execution for repository inspection and cleanup. |
| `session-catchup.py` reported old Buffer pure-Q&A context | Step 2 continuation | Ignored it for implementation because the current authoritative route restarts from Step 0 and old Buffer files no longer exist. |

## 2026-05-09 Markdown Cleanup Correction

This correction restores useful memory that was removed too aggressively by `d6a3830 docs: simplify LiteIM markdown memory`.

Planned correction scope:

- Restore `docs/debug_cases/thread_pool_worker_stop.md`.
- Restore `docs/debug_cases/net_lifecycle_review_hardening.md`.
- Restore this planning file, `findings.md`, and `progress.md` from `HEAD^`, then append this correction note instead of rewriting them into summaries.
- Keep removed top-level docs files out of the repository: `docs/architecture.md`, `docs/project_layout.md`, and `docs/roadmap.md`.
- Delete `tutorials/README.md`; future tutorial navigation is per Step file, not a separate index.
- Rewrite the main README as a GitHub-facing project overview without `Current Status` / `当前状态` headings and without using planning files as public documentation.
- Sync `/home/yolo/jianli/PROJECT_MEMORY.md` and `/home/yolo/jianli/AGENTS.md` so `docs/debug_cases/` is treated as useful internal retrospective material.

Verification commands:

```bash
find docs -type f | sort
find tutorials -maxdepth 1 -type f -name 'README.md'
rg -n "Current Status|当前状态" README.md
rg -n "tutorials/README|docs/architecture|docs/project_layout|docs/roadmap" README.md tutorials /home/yolo/jianli/AGENTS.md /home/yolo/jianli/PROJECT_MEMORY.md
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
```

Verification results:

- `cmake -S . -B build && cmake --build build` passed.
- `ctest --test-dir build -R "SignalWatcher|LiteIMServerSignal" --output-on-failure` passed, 4/4.
- `timeout 1s ./build/server/liteim_server || test $? -eq 124` passed and logged SIGTERM graceful shutdown.
- `ctest --test-dir build --output-on-failure` passed, 171/171.
- `git diff --check` produced no output.
- `.gitkeep` and old SQLite / `InMemoryStorage` / old `server/net` path scans produced no output.

## 2026-05-09 Muduo-style Lifecycle Hardening

This hardening pass follows the external review but keeps the scope narrow:

- Make `TcpServer::stop()` and `TcpServer` destruction owner-loop-only. Non-owner calls terminate instead of queueing a lambda that captures raw `this`.
- Make `TimerManager::stop()` and `TimerManager` destruction owner-loop-only for the same reason.
- Change `Session` construction to receive `UniqueFd`, so accepted fd ownership stays RAII from `Acceptor` through `TcpServer` into `Session`.
- Require `Session::pendingOutputBytes()` to run on the owner loop thread and remove `noexcept`.
- Change `server/main.cpp` from a scaffold log to a real `EventLoop + TcpServer` echo server.

Do not include in this pass:

- deleting `Session::input_buffer_`;
- a `Session` enum state-machine refactor;
- dynamic rearm `TimerManager` / full muduo `TimerQueue`;
- `signalfd` graceful shutdown;
- MySQL / Redis / service-layer work.

Expected new or updated tests:

- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `SessionTest.PendingOutputBytesRequiresOwnerLoopThread`
- `TcpServerTest.StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis`
- `TimerManagerTest.StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis`

Verification commands:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build -R "(Session|TcpServer|TimerManager)" --output-on-failure
timeout 1s ./build/server/liteim_server || test $? -eq 124
ctest --test-dir build --output-on-failure
git diff --check
```

## 2026-05-09 PROJECT_MEMORY Markdown Alignment

This documentation-only pass follows the newly merged `/home/yolo/jianli/PROJECT_MEMORY.md`:

- Keep only one authoritative project memory file: `/home/yolo/jianli/PROJECT_MEMORY.md`.
- Treat Step 18 and Step 18.5 as complete.
- Treat Step 19 `signalfd graceful shutdown` as the default next implementation step.
- Keep Optional Step 18.6 and Step 18.7 as non-blocking future cleanup.
- Update `/home/yolo/jianli/AGENTS.md` as the compact constraint file for future agents.
- Update `/home/yolo/jianli/CLAUDE.md` so it no longer points agents at broad `LiteIM/docs/` maintenance.
- Translate `docs/debug_cases/net_lifecycle_review_hardening.md` to Chinese while preserving the technical content.

Verification commands:

```bash
find /home/yolo/jianli -maxdepth 1 -name 'PROJECT_MEMORY*.md' -printf '%f\n' | sort
rg -n --glob '!task_plan.md' "下一步不要直接进入|用于替换或升级" /home/yolo/jianli/PROJECT_MEMORY.md /home/yolo/jianli/AGENTS.md /home/yolo/jianli/CLAUDE.md README.md findings.md progress.md tutorials docs
rg -n "Current Status|当前状态|tutorials/README|docs/architecture|docs/project_layout|docs/roadmap" README.md tutorials /home/yolo/jianli/AGENTS.md /home/yolo/jianli/PROJECT_MEMORY.md
git diff --check
```

## 2026-05-09 Step 19 Signalfd Graceful Shutdown

This Step adds process-level graceful shutdown without weakening the Step 18.5 owner-loop lifecycle rules.

Scope:

- Add `SignalWatcher` as a Reactor-owned wrapper around `pthread_sigmask()`, `signalfd()`, `Channel`, and owner-loop signal callbacks.
- Route `SIGINT` and `SIGTERM` through the base `EventLoop`.
- Start `SignalWatcher` before `TcpServer` so I/O worker threads inherit the blocked signal mask.
- In the signal callback, call `server.stop()` in the base loop thread, then `loop.quit()`.
- Keep `TcpServer`, `TimerManager`, and `SignalWatcher` stop/destruct owner-loop-only.

Do not include in this Step:

- business `ThreadPool` shutdown sequencing;
- MySQL / Redis connection pool cleanup;
- traditional signal handlers;
- Session input-path or state-machine refactors;
- dynamic rearm `TimerManager`.

New tests:

- `ReactorInterfaceTest.SignalWatcherHeaderIsSelfContained`
- `SignalWatcherTest.SignalfdDispatchesSignalInOwnerLoop`
- `SignalWatcherTest.StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis`
- `LiteIMServerSignalTest.TerminatesOnSigterm`

Verification commands:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build -R "SignalWatcher|LiteIMServerSignal" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```
