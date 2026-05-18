# LiteIM

![CI](https://github.com/yolo0731/LiteIM/actions/workflows/ci.yml/badge.svg)

LiteIM is a C++17 instant messaging server project built around Linux networking and a one-loop-per-thread Reactor model. The project is developed step by step so each layer remains understandable: protocol encoding, TCP stream decoding, nonblocking I/O, connection lifetime, multi-Reactor dispatch, business-thread isolation, timer-driven heartbeat cleanup, and later MySQL / Redis backed IM services.

The long-term target is a small but realistic IM system:

```text
Qt / CLI / Python Client
    -> LiteIM C++ TCP Server
    -> business ThreadPool
    -> MySQL / Redis
    -> future PersonaAgent Python BotClient
```

PersonaAgent is intentionally a separate Python service. LiteIM exposes only the normal account protocol boundary; Python, LangGraph, RAG, LLM calls, and safety logic do not run inside the C++ server process, and the C++ server does not know whether a logged-in account is controlled by a human or by an external agent.

## Project Focus

- High-performance C++ backend engineering with C++17, CMake, RAII, GoogleTest, and Linux system calls.
- Nonblocking socket I/O with `epoll` LT mode, `eventfd` cross-thread wakeups, `timerfd` based timers, and `signalfd` graceful shutdown handling.
- Main Reactor + sub Reactor thread pool. I/O threads handle fd events, Packet/TLV codec work, configurable output-buffer high-water-mark backpressure, and `Session` lifetime.
- Business `ThreadPool` for dispatching heavier protocol handlers such as MySQL queries, Redis operations, password hashing, and history loading.
- MySQL C API wrapper with RAII connection ownership, prepared statements, a fixed-size connection pool, and users-table DAO access.
- Custom TLV binary protocol with TCP sticky-packet / half-packet handling.
- Safe cross-thread connection access through `EventLoop::runInLoop()` and `EventLoop::queueInLoop()`.
- Command-line protocol debug client, Python E2E tests, local benchmark tooling, and future Qt Widgets three-column chat client.

## Architecture

```text
                         +----------------------+
                         | Qt / CLI / future BotClient |
                         +----------+-----------+
                                    |
                              TLV over TCP
                                    |
+------------------------------- LiteIM Server -------------------------------+
|                                                                            |
|  Main Reactor                                                               |
|  - nonblocking listen socket                                                |
|  - epoll accept events                                                      |
|  - dispatch accepted connections                                            |
|  - signalfd driven SIGINT / SIGTERM shutdown                                |
|                                                                            |
|  Sub Reactor Thread Pool                                                    |
|  - one EventLoop per I/O thread                                             |
|  - eventfd queueInLoop wakeup                                               |
|  - Session read/write lifecycle                                             |
|  - FrameDecoder + Packet/TLV codec                                          |
|  - configurable output-buffer high-water-mark protection                    |
|                                                                            |
|  Business Thread Pool                                                       |
|  - AuthService / FriendService / ChatService / OfflineMessageService        |
|  - GroupService / HistoryService / HeartbeatService                         |
|  - MySQL DAO and Redis cache operations                                     |
|  - post responses back to the owning EventLoop                              |
|                                                                            |
+---------------------------+----------------------+-------------------------+
                            |                      |
                         MySQL                  Redis
                   persistent entities      online/unread/rate-limit
```

Important boundaries:

- I/O threads must not execute MySQL or Redis blocking calls.
- Business threads must not directly mutate `Session`.
- Responses generated outside the owning I/O thread must be delivered through the owning `EventLoop`.
- `Session` lifetime is protected with `shared_ptr` / `weak_ptr`, not long-lived raw pointers.
- Reactor-owned objects such as `Acceptor`, `TcpServer`, `TimerManager`, and `SignalWatcher` must stop and destruct in their owner loop thread.
- Redis is cache/state, not the final source of message truth. Message entities belong in MySQL.

## Core Components

- `liteim_base`: `Config`, `Logger`, `ErrorCode`, `Status`, `Timestamp`, and raw-byte aliases `Byte` / `Bytes`.
- `liteim_protocol`: `MessageType`, `TlvType`, `ByteOrder`, `Packet`, `TlvCodec`, and `FrameDecoder`.
- `liteim_net`: `Buffer`, `SocketUtil`, `UniqueFd`, `Channel`, `Epoller`, `EventLoop`, `Acceptor`, `Session`, `EventLoopThread`, `EventLoopThreadPool`, `SignalWatcher`, and `TcpServer`.
- `liteim_concurrency`: fixed-size business `ThreadPool`.
- `liteim/timer`: `TimerHeap` and `TimerManager`, linked into the network layer because `TimerManager` depends on `EventLoop` and `Channel`.
- `liteim_storage`: storage DTOs, the `IStorage` interface, `MySqlConnection`, `PreparedStatement`, `MySqlQueryResult`, `MySqlPool`, `ConnectionGuard`, `UserDao`, `AuthDao`, `MessageDao`, `OfflineMessageDao`, `FriendDao`, `GroupDao`, and `MySqlStorage` for MySQL-backed users, public friend profiles, groups, messages, offline messages, and history.
- `liteim_cache`: cache DTOs, the `ICache` interface, `RedisClient`, `RedisPool`, `RedisConnectionGuard`, `OnlineStatusCache`, `UnreadCounter`, `LoginRateLimiter`, and `RedisCache` for Redis-backed online sessions, unread counters, and login failure limiting.
- `liteim_service`: `SessionManager`, `OnlineService`, `MessageRouter`, `AuthService`, `FriendService`, `ChatService`, `GroupService`, `OfflineMessageService`, `HistoryService`, `HeartbeatService`, and shared service helpers for validation and message TLV response building. `SessionManager` / `OnlineService` provide in-process user/session binding plus Redis-backed online-state synchronization, and runtime session close cleanup removes current online state through the business pool. `MessageRouter` parses request TLVs, dispatches handlers inline or through the business `ThreadPool`, and sends responses back through `Session::sendPacket()`. `AuthService` handles register/login with MySQL users, Redis login-failure limiting, PBKDF2-HMAC-SHA256 password hashing, login-time session binding, and service-level username/nickname/password length validation before database access. `FriendService` handles add-friend and friend-list requests with MySQL friendships; Redis online-status lookup failure degrades that friend's online field to offline instead of failing the whole list. `ChatService` handles private-message requests by validating the logged-in sender, enforcing the service message-size limit, saving the message through `IStorage`, pushing to an in-process online receiver, or recording offline delivery plus unread count for an offline receiver. `GroupService` handles basic create/join/list group flows and group-message routing, validates group-name/message-text lengths, saves group messages through `IStorage`, pushes to in-process online members, and records offline delivery plus unread counts for offline members. If the message and offline row are already saved, Redis unread increment failure is logged but does not turn the sender response into a failure. `OfflineMessageService` handles client-triggered `OfflineMessagesRequest`, asks storage for only the requested batch size, returns pending offline messages, marks the delivered batch, and treats Redis unread-counter clearing as best-effort after the MySQL response data is assembled. `HistoryService` handles `HistoryRequest`, validates that the logged-in user can read the private or group conversation, applies default/max cursor pagination limits, and returns repeated TLV message fields from `IStorage::getHistory()`. `HeartbeatService` handles `HeartbeatRequest` in the business pool, returns `HeartbeatResponse` for valid heartbeats, and refreshes Redis online TTL only for logged-in sessions. LiteIM has no C++ AI/assistant identity service: an external LLM-controlled account logs in and sends messages exactly like any other account. Redis TTL/unread refresh failures are logged as degraded side effects when the message source of truth is already saved. Repeated login uses a kick-old-keep-new policy.
- `liteim_client_cli`: command parsing, TLV `Packet` construction, debug packet formatting, and a blocking TCP protocol client used by the `liteim_cli` executable.
- `liteim_bench_core` / `liteim_bench`: local benchmark helpers and executable for generating ordinary register/login/private-message load, sender request/response RTT percentiles, QPS, error count, client-process RSS, client-process CPU usage, and JSON or Markdown reports.
- `liteim_qt_client_core` / `liteim_qt_client`: optional Qt Widgets client components built only with `LITEIM_BUILD_QT_CLIENT=ON`. The core target wraps the shared LiteIM Packet/TLV protocol for Qt, decodes TCP half/sticky packets, provides a `QTcpSocket` based `TcpClient`, and keeps client-side seq_id, pending-request, and login state in `ClientSession`.

## Build And Test

Requirements:

- Linux
- CMake
- A C++17 compiler
- Python 3 for end-to-end protocol tests
- POSIX sockets, `epoll`, `eventfd`, `timerfd`, and `signalfd`
- `pkg-config`, MySQL client development files that provide `mysqlclient`, hiredis development files that provide `hiredis`, and OpenSSL development files that provide `openssl`

Build:

```bash
cmake -S . -B build
cmake --build build
```

Build the optional Qt Widgets client only when Qt development packages are available:

```bash
cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON
cmake --build build-qt --target liteim_qt_client
```

Run the Qt client protocol tests from the Qt-enabled build:

```bash
ctest --test-dir build-qt -R LiteIMQtClient.Step46 --output-on-failure
```

The default build keeps `LITEIM_BUILD_QT_CLIENT=OFF`, so server, CLI, benchmark, and tests do not require Qt. The current local Qt package is discovered from Anaconda Qt5; the Qt CTest entry sets `LD_LIBRARY_PATH` so the system `libstdc++` stays ahead of Anaconda's older `libstdc++`.

Run the server executable:

```bash
./build/server/liteim_server
# or explicitly choose a config file
./build/server/liteim_server --config config/liteim.conf
```

Without `--config`, the server first tries `config/liteim.conf` if it exists and otherwise falls back to built-in local-development defaults. The server starts a real `EventLoop + TcpServer` on the configured host and port, starts MySQL / Redis pools, starts the business `ThreadPool`, and wires incoming packets into `MessageRouter`. In the current Step 44 server runtime, heartbeat requests are handled by `HeartbeatService`; register/login requests are handled by `AuthService`; add-friend and friend-list requests are handled by `FriendService`; private-message requests are handled by `ChatService`; group create/join/list/message requests are handled by `GroupService`; offline-message pull requests are handled by `OfflineMessageService`; history requests are handled by `HistoryService`; and unknown or unsupported request types get `ErrorResponse`. LiteIM treats every logged-in account as an ordinary account, so a future PersonaAgent-controlled account is just another client session. Business handlers run in the business pool. Session close cleanup submits `OnlineService::unbindSession(session_id)` into the business pool so Redis online-state cleanup does not run in an I/O callback. The server handles `Ctrl-C` / `SIGTERM` through `signalfd`, stops `TcpServer` in the base loop thread, stops the business pool, closes MySQL / Redis pools, and exits cleanly.

Because the current server runtime starts real MySQL / Redis pools, start local dependencies before a bounded server smoke check:

```bash
docker compose -f docker/docker-compose.yml up -d --wait
timeout 1s ./build/server/liteim_server || test $? -eq 124
```

Run the command-line protocol client:

```bash
./build/client_cli/liteim_cli --host 127.0.0.1 --port 9000
```

Useful CLI commands:

```text
register cli_alice secret CLI Alice
login cli_alice secret
add-friend 1002
friends
private 1002 hello bob
create-group project room
join-group 2001
groups
group 2001 hello team
history group 2001 20
offline 20
heartbeat
quit
```

`liteim_cli` is a protocol debugging tool: it sends ordinary TLV requests, prints decoded response/push packets, and sends a background `HeartbeatRequest` every 30 seconds after connecting. It does not provide a curses UI, local persistence, or automatic reconnect in the first version.

Run the Python end-to-end tests:

```bash
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build -R LiteIME2E --output-on-failure
```

The E2E tests use Python standard-library `unittest`, start the built `liteim_server` through CTest, speak the same TCP/TLV protocol as real clients, and use unique generated usernames so they do not depend on seed users having real PBKDF2 password hashes. CI sets `LITEIM_E2E_STRICT=1` so a missing server binary or unavailable server startup fails loudly instead of being reported as a skip.

Run a local benchmark after starting `liteim_server` in another terminal:

```bash
docker compose -f docker/docker-compose.yml up -d --wait
./build/server/liteim_server
./build/bench/liteim_bench \
  --host 127.0.0.1 \
  --port 9000 \
  --connections 10 \
  --message-size 128 \
  --interval-ms 10 \
  --duration-sec 10 \
  --format markdown
```

`liteim_bench` uses one receiver connection and `connections - 1` sender connections. All clients register unique users, log in, and send ordinary `PrivateMessageRequest` packets. The report includes connection success, successful request count, QPS, average latency, p50, p95, p99, error count, process RSS, and process CPU usage of the `liteim_bench` client itself. The reported latency is sender request/response round trip time, not end-to-end push delivery latency; for server-side CPU/RSS, run a separate tool such as `pidstat -p <server_pid>` while the benchmark runs. Treat generated numbers as local measurements only; publish benchmark data only with the exact command, server configuration, Docker dependency state, CPU, memory, OS, compiler, and build type.

Verified local smoke result, not a capacity claim:

```text
Date: 2026-05-16
OS: Linux 6.8.0-111-generic x86_64
CPU: 13th Gen Intel(R) Core(TM) i9-13900HX, 32 logical CPUs
Memory: 31 GiB total
Compiler: g++ 13.3.0
Build type: Release
Dependencies: Docker Compose MySQL and Redis healthy on local default ports
Server config: host=0.0.0.0, port=9000, io_threads=4, business_threads=4
Command: ./build/bench/liteim_bench --host 127.0.0.1 --port 9000 --connections 4 --message-size 64 --interval-ms 20 --duration-sec 1 --format json
Result: connection_success=4/4, request_success=114, error_count=0, qps=111.874, average_latency_us=6665.71, p50_us=6494, p95_us=8984, p99_us=9403
```

Configuration keys are parsed by `liteim::Config::loadFromFile()`. Network-facing defaults include:

```text
server.host = 0.0.0.0
server.port = 9000
server.io_threads = 4
server.business_threads = 4
server.output_high_water_mark_bytes = 4194304
```

`server.output_high_water_mark_bytes` controls the per-`Session` pending output-buffer limit. When a server push would exceed the limit, LiteIM logs the pending bytes, incoming bytes, and limit, then closes that slow connection.

Service-layer validation currently caps usernames and nicknames at 64 bytes, group names at 128 bytes, passwords at 128 bytes, and message text at 8192 bytes. The message-text cap is intentionally below the protocol packet body limit so invalid user input is rejected before MySQL `TEXT` storage or offline-message fanout paths.

## Local MySQL And Redis

Step 22 adds the local development database environment for storage/cache work. Step 23-27 use the MySQL side for storage tests, Step 28 adds a blocking hiredis-based Redis client/pool, Step 29 adds Redis online-status cache tests, and Step 30 adds Redis unread-counter and login-limiter tests. Step 31 adds `MySqlStorage : IStorage`, `RedisCache : ICache`, unsigned MySQL binding for `BIGINT UNSIGNED`, public friend-profile DTOs, and a single MySQL transaction for message + offline-message writes. Step 32 adds `SessionManager` and `OnlineService`; it uses `RedisCache` to write, refresh, and clear online status, and close cleanup now supports session-id-only unbinding. Step 33 adds `MessageRouter` and connects `TcpServer` runtime packets to the service layer without calling MySQL or Redis from Reactor I/O threads. Step 34 adds `AuthService` and registers `RegisterRequest` / `LoginRequest` handlers on the business thread pool. Step 35 adds `FriendService`, registers `AddFriendRequest` / `ListFriendsRequest`, and returns friend online state through `TlvType::OnlineStatus`. Step 36 adds `ChatService`, registers `PrivateMessageRequest`, saves private messages through `IStorage`, pushes online private messages through `Session`, and records offline delivery plus unread count for offline receivers. Step 37 adds `OfflineMessageService`, registers `OfflineMessagesRequest`, returns at most 100 pending offline messages, marks the returned batch delivered, and clears unread counters for the returned conversations. Current storage pushes the requested offline-message limit down to SQL instead of fetching all pending rows and truncating in memory. Step 38 adds `GroupService`, registers `CreateGroupRequest` / `JoinGroupRequest` / `ListGroupsRequest` / `GroupMessageRequest`, lists groups through `IStorage::getGroupsForUser()`, saves group messages, pushes to online members, and records offline rows plus unread counts for offline members.

Step 39 adds `HistoryService`, registers `HistoryRequest`, reads `ConversationType` / `ConversationId` / optional `MessageId` cursor / optional `Limit`, validates private or group membership, and returns recent messages through `HistoryResponse`. History pages are returned newest-first (`ORDER BY message_id DESC`); UI clients should reverse each page before rendering messages top-to-bottom.

Step 40 adds `HeartbeatService`, registers `HeartbeatRequest` on the business thread pool, returns `HeartbeatResponse` for valid heartbeats, and refreshes Redis `online:user:<user_id>` TTL only when the session is logged in. `HeartbeatResponse` means the server successfully received and processed a legal heartbeat packet; it does not guarantee Redis online TTL refresh succeeded. When Redis refresh fails, LiteIM logs a warning and relies on later heartbeats to recover the online-state TTL.

The old C++ assistant route has been removed. LiteIM no longer defines AI identity, assistant-only message types, built-in echo replies, mention triggers, or special offline/unread handling. A future PersonaAgent account must connect through the same register/login/private/group/history/offline/heartbeat protocol as any other account, with LLM behavior implemented outside the C++ server.

Step 41 adds `liteim_cli`, a command-line protocol debug client. It connects to `127.0.0.1:9000` by default, supports `--host` / `--port`, builds normal TLV packets for register/login/friend/private/group/history/offline/heartbeat commands, prints decoded server responses and push packets, and sends a periodic heartbeat every 30 seconds.

Step 42 adds Python black-box E2E tests. The tests implement a minimal TLV codec/client in Python, start the built `liteim_server`, and verify register/login, wrong-password/login-limit errors, private chat, group chat, history, offline delivery, heartbeat, and slow-client backpressure over the real TCP protocol.

Step 43 adds `liteim_bench`, a local benchmark executable. It creates one receiver connection and configurable sender connections, registers and logs in generated users, sends ordinary private messages, and emits JSON or Markdown metrics for connection success, QPS, latency percentiles, errors, RSS, and CPU usage. The README does not hard-code performance claims; benchmark numbers must come from real runs with parameters and machine details.

Step 44 expands the validation layer. It adds gMock-based service boundary tests for `AuthService`, `ChatService`, `GroupService`, and `HistoryService`; adds extra protocol, thread-pool, and timer edge-case coverage; labels CTest entries for `unit`, `integration`, `mysql`, `redis`, `docker`, and `e2e` filtering; and adds an optional ASan/UBSan build through `LITEIM_ENABLE_SANITIZERS`. The post-Step-44 hardening pass keeps CI in Step 44's validation scope: it adds strict E2E behavior for CI, E2E strict coverage, ASan-friendly handling for the fd-exhaustion test, server config loading, service input-size checks, SQL-limited offline pulls, and best-effort Redis cache degradation where MySQL remains the source of truth.

Step 45 starts the optional Qt Widgets client project without affecting the default server build. Step 46 adds Qt-side protocol and networking foundations: `PacketCodec` reuses the server's `liteim_protocol` wire format, `TcpClient` owns a `QTcpSocket` and emits connected/disconnected/packet/error signals, and `ClientSession` tracks seq_id, pending requests, and login state. Login and registration windows begin in Step 47.

Start MySQL and Redis:

```bash
docker compose -f docker/docker-compose.yml up -d
```

Default local endpoints:

```text
MySQL: 127.0.0.1:33060
Redis: 127.0.0.1:63790
Database: liteim
MySQL user: liteim
MySQL password: 6
MySQL root password: 6
Redis password: 6
```

The MySQL service uses the `mysql:8.0` image so MySQL Workbench 8.0 can connect without the MySQL 8.4 compatibility warning. The host port `33060` maps to classic MySQL port `3306` inside the container; it is not MySQL X Protocol.

The MySQL container runs `scripts/init_mysql.sql` and `scripts/seed_test_data.sql` the first time its data volume is created. The init script creates the main LiteIM tables:

- `users`
- `friendships`
- `chat_groups`
- `group_members`
- `messages`
- `offline_messages`

The seed script inserts local test users, a `dev_group`, sample messages, and pending offline-message rows. Redis starts empty but requires the local development password. Step 29 uses `online:user:<user_id>` keys with TTL for online sessions. Step 30 adds Redis keys for per-user/per-conversation unread counters and username/remote-ip login failure windows.

Useful checks:

```bash
docker compose -f docker/docker-compose.yml ps
docker compose -f docker/docker-compose.yml exec mysql mysql -uliteim -p6 liteim -e "SHOW TABLES;"
docker compose -f docker/docker-compose.yml exec mysql mysql -uliteim -p6 liteim -e "SELECT user_id, username FROM users ORDER BY user_id;"
docker compose -f docker/docker-compose.yml exec redis sh -c 'REDISCLI_AUTH=6 redis-cli ping'
```

Stop the local services:

```bash
docker compose -f docker/docker-compose.yml down
```

To recreate the database from the init scripts, remove the local dev data volumes:

```bash
docker compose -f docker/docker-compose.yml down -v
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Useful label filters:

```bash
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L integration --output-on-failure
ctest --test-dir build -L mysql --output-on-failure
ctest --test-dir build -L redis --output-on-failure
ctest --test-dir build -L docker --output-on-failure
ctest --test-dir build -L e2e --output-on-failure
```

The Step 23-27 MySQL integration tests, Step 31 `MySqlStorage` tests, Step 28-32 Redis integration tests, Step 34-40 service tests, Step 42 Python E2E tests, and Step 44 Docker-tagged integration tests use `Config::defaults()` where they need real dependencies, so they target the local Docker endpoints shown above. If those containers are not running, integration tests skip instead of failing unrelated unit-test runs. Start the local dependency stack first when validating the storage/cache/service/E2E layer:

```bash
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build -R "MySql|UserDao|MessageDao|FriendGroupDao|MySqlStorage|Redis|OnlineStatusCache|UnreadCounter|LoginRateLimiter|RedisCache|SessionManager|OnlineService|AuthService|FriendService|ChatService|GroupService|OfflineMessageService|HistoryService|HeartbeatService|ClientCli|Benchmark|LiteIME2E" --output-on-failure
```

Run the sanitizer build when checking memory and undefined-behavior risks:

```bash
cmake -S . -B build-asan -DLITEIM_ENABLE_SANITIZERS=ON
cmake --build build-asan -j2
ctest --test-dir build-asan --output-on-failure
```

`LITEIM_ENABLE_SANITIZERS=ON` is supported for GNU and Clang builds. It enables AddressSanitizer and UndefinedBehaviorSanitizer with frame pointers and non-recovering sanitizer failures. It does not change the default C++ standard or require new production dependencies.

## Repository CI

The repository includes GitHub Actions workflow configuration in `.github/workflows/ci.yml`. It is project infrastructure, not a numbered LiteIM Step. On push or pull request to `main`, the workflow runs three checks on a clean Ubuntu runner:

- `unit`: configure, build, and run `ctest -L unit`.
- `integration`: configure, build, start Docker MySQL/Redis, and run `ctest -L integration`.
- `sanitizers`: configure with `LITEIM_ENABLE_SANITIZERS=ON`, build, start Docker MySQL/Redis, and run full CTest under ASan/UBSan.

The badge at the top of this README reflects the current GitHub Actions result after the workflow file is pushed to GitHub.

Useful repository checks:

```bash
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

## Repository Layout

```text
LiteIM/
├── .github/
│   └── workflows/
│       └── ci.yml
├── CMakeLists.txt
├── README.md
├── docker/
│   └── docker-compose.yml
├── include/liteim/
│   ├── base/
│   ├── cache/
│   ├── concurrency/
│   ├── net/
│   ├── protocol/
│   ├── service/
│   ├── storage/
│   └── timer/
├── src/
│   ├── base/
│   ├── cache/
│   ├── concurrency/
│   ├── net/
│   ├── protocol/
│   ├── service/
│   ├── storage/
│   └── timer/
├── server/
├── client_cli/
├── client_qt/
│   ├── include/liteim_client/
│   ├── src/
│   └── resources/
├── bench/
├── scripts/
│   ├── init_mysql.sql
│   └── seed_test_data.sql
├── tests/
│   ├── base/
│   ├── bench/
│   ├── cache/
│   ├── client_cli/
│   ├── concurrency/
│   ├── e2e/
│   ├── net/
│   ├── protocol/
│   ├── service/
│   ├── storage/
│   └── timer/
└── docs/
    ├── process/
    ├── tutorials/
    └── debug_cases/
```

Directory conventions:

- Public headers live under `include/liteim/<module>/`.
- Library implementations live under `src/<module>/`.
- Executable entry points live under `server/`, `client_cli/`, `client_qt/`, and `bench/`.
- `client_qt/` is optional and only builds when `LITEIM_BUILD_QT_CLIENT=ON`; its resources must not use third-party IM product branding.
- `.github/workflows/` contains repository CI automation.
- `docs/tutorials/` contains per-step teaching notes.
- `docs/process/` contains active planning, findings, and progress memory.
- `docs/debug_cases/` contains internal postmortems for useful bug and review hardening cases.

## Debug Notes

The repository keeps focused debug case writeups when they preserve useful engineering lessons:

- `docs/debug_cases/net_lifecycle_review_hardening.md`
- `docs/debug_cases/thread_pool_worker_stop.md`

These are not a public architecture manual. They are retained because they document concrete lifetime, threading, and cleanup bugs that shaped the network implementation.

## Roadmap

LiteIM is being built in phases:

| Phase | Goal |
| --- | --- |
| Network base | CMake, GoogleTest, base utilities, TLV protocol, frame decoder, Buffer, socket helpers, Reactor interfaces, `Epoller`, `Channel`, `EventLoop`, `Acceptor`, `Session`, multi-Reactor `TcpServer`, business `ThreadPool`, timerfd heartbeat cleanup, and signalfd shutdown. |
| Storage and cache | MySQL C API wrapper, prepared statement wrapper with signed/unsigned 64-bit binding, MySQL connection pool, RAII connection guard, user/auth DAO layer, message/offline-message DAO layer, `MySqlStorage` adapter, Redis client/pool, online status cache, unread counters, login rate limiting, and `RedisCache` adapter. |
| IM services | Session binding, online-state synchronization, async message routing, register/login, friend list, private chat, group chat, offline messages, history loading, heartbeat protocol, and graceful shutdown. |
| Tooling and validation | CLI client, Python E2E tests, benchmark tooling, broader GoogleTest/gMock coverage, CTest labels, ASan/UBSan, and repository CI. |
| Demo clients | Qt Widgets chat client with login, conversation list, message bubbles, group chat, ordinary contact entry for the external PersonaAgent account, heartbeat state, and disconnect feedback. |
| PersonaAgent integration | Planned Python BotClient and separate FastAPI / LangGraph AgentService using Knowledge, Memory, Authorized Style RAG, Persona, Safety, tracing, checkpointing, and evaluation. |

README performance numbers and benchmark claims should only be added after real local measurements exist.
