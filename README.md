# LiteIM

LiteIM is a C++17 instant messaging server project built around Linux networking and a one-loop-per-thread Reactor model. The project is developed step by step so each layer remains understandable: protocol encoding, TCP stream decoding, nonblocking I/O, connection lifetime, multi-Reactor dispatch, business-thread isolation, timer-driven heartbeat cleanup, and later MySQL / Redis backed IM services.

The long-term target is a small but realistic IM system:

```text
Qt / CLI / Python Client
    -> LiteIM C++ TCP Server
    -> business ThreadPool
    -> MySQL / Redis
    -> BotGateway
    -> PersonaAgent Python BotClient
```

PersonaAgent is intentionally a separate Python service. LiteIM exposes the protocol and Bot user boundary; Python, LangGraph, RAG, LLM calls, and safety logic do not run inside the C++ server process.

## Project Focus

- High-performance C++ backend engineering with C++17, CMake, RAII, GoogleTest, and Linux system calls.
- Nonblocking socket I/O with `epoll` LT mode, `eventfd` cross-thread wakeups, `timerfd` based timers, and `signalfd` graceful shutdown handling.
- Main Reactor + sub Reactor thread pool. I/O threads handle fd events, Packet/TLV codec work, configurable output-buffer high-water-mark backpressure, and `Session` lifetime.
- Business `ThreadPool` for dispatching heavier protocol handlers such as MySQL queries, Redis operations, password hashing, and history loading.
- MySQL C API wrapper with RAII connection ownership, prepared statements, a fixed-size connection pool, and users-table DAO access.
- Custom TLV binary protocol with TCP sticky-packet / half-packet handling.
- Safe cross-thread connection access through `EventLoop::runInLoop()` and `EventLoop::queueInLoop()`.
- Future demo clients: CLI, Python E2E client, and Qt Widgets three-column chat client.

## Architecture

```text
                         +----------------------+
                         |  Qt / CLI / BotClient|
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
|  - AuthService / FriendService / ChatService / GroupService                 |
|  - HistoryService                                                           |
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
- `liteim_service`: `SessionManager`, `OnlineService`, `MessageRouter`, `AuthService`, `FriendService`, and `ChatService`. `SessionManager` / `OnlineService` provide in-process user/session binding plus Redis-backed online-state synchronization. `MessageRouter` parses request TLVs, dispatches handlers inline or through the business `ThreadPool`, and sends responses back through `Session::sendPacket()`. `AuthService` handles register/login with MySQL users, Redis login-failure limiting, PBKDF2-HMAC-SHA256 password hashing, and login-time session binding. `FriendService` handles add-friend and friend-list requests with MySQL friendships plus Redis-backed online-status fields. `ChatService` handles private-message requests by validating the logged-in sender, saving the message through `IStorage`, pushing to an in-process online receiver, or recording offline delivery plus unread count for an offline receiver. Repeated login uses a kick-old-keep-new policy.

## Build And Test

Requirements:

- Linux
- CMake
- A C++17 compiler
- POSIX sockets, `epoll`, `eventfd`, `timerfd`, and `signalfd`
- `pkg-config`, MySQL client development files that provide `mysqlclient`, hiredis development files that provide `hiredis`, and OpenSSL development files that provide `openssl`

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run the server executable:

```bash
./build/server/liteim_server
```

The server starts a real `EventLoop + TcpServer` on the configured host and port, starts MySQL / Redis pools, starts the business `ThreadPool`, and wires incoming packets into `MessageRouter`. In the current Step 36 runtime, heartbeat requests get lightweight heartbeat responses; register/login requests are handled by `AuthService`; add-friend and friend-list requests are handled by `FriendService`; private-message requests are handled by `ChatService`; and unknown or unsupported request types get `ErrorResponse`. Business handlers run in the business pool. The server handles `Ctrl-C` / `SIGTERM` through `signalfd`, stops `TcpServer` in the base loop thread, stops the business pool, closes MySQL / Redis pools, and exits cleanly.

Because Step 36 runtime starts real MySQL / Redis pools, start local dependencies before a bounded server smoke check:

```bash
docker compose -f docker/docker-compose.yml up -d --wait
timeout 1s ./build/server/liteim_server || test $? -eq 124
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

## Local MySQL And Redis

Step 22 adds the local development database environment for storage/cache work. Step 23-27 use the MySQL side for storage tests, Step 28 adds a blocking hiredis-based Redis client/pool, Step 29 adds Redis online-status cache tests, and Step 30 adds Redis unread-counter and login-limiter tests. Step 31 adds `MySqlStorage : IStorage`, `RedisCache : ICache`, unsigned MySQL binding for `BIGINT UNSIGNED`, public friend-profile DTOs, and a single MySQL transaction for message + offline-message writes. Step 32 adds `SessionManager` and `OnlineService`; it uses `RedisCache` to write, refresh, and clear online status. Step 33 adds `MessageRouter` and connects `TcpServer` runtime packets to the service layer without calling MySQL or Redis from Reactor I/O threads. Step 34 adds `AuthService` and registers `RegisterRequest` / `LoginRequest` handlers on the business thread pool. Step 35 adds `FriendService`, registers `AddFriendRequest` / `ListFriendsRequest`, and returns friend online state through `TlvType::OnlineStatus`. Step 36 adds `ChatService`, registers `PrivateMessageRequest`, saves private messages through `IStorage`, pushes online private messages through `Session`, and records offline delivery plus unread count for offline receivers.

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

The seed script inserts local test users `alice`, `bob`, the Bot user `mira_bot`, a `dev_group`, sample messages, and pending offline-message rows. Redis starts empty but requires the local development password. Step 29 uses `online:user:<user_id>` keys with TTL for online sessions. Step 30 adds Redis keys for per-user/per-conversation unread counters and username/remote-ip login failure windows.

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

The Step 23-27 MySQL integration tests, Step 31 `MySqlStorage` tests, Step 28-32 Redis integration tests, and Step 34-35 service integration tests use `Config::defaults()`, so they target the local Docker endpoints shown above. If those containers are not running, integration tests skip instead of failing unrelated unit-test runs. Start the local dependency stack first when validating the storage/cache/service layer:

```bash
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build -R "MySql|UserDao|MessageDao|FriendGroupDao|MySqlStorage|Redis|OnlineStatusCache|UnreadCounter|LoginRateLimiter|RedisCache|SessionManager|OnlineService|AuthService|FriendService|ChatService" --output-on-failure
```

Useful repository checks:

```bash
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

## Repository Layout

```text
LiteIM/
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
├── scripts/
│   ├── init_mysql.sql
│   └── seed_test_data.sql
├── tests/
│   ├── base/
│   ├── cache/
│   ├── concurrency/
│   ├── net/
│   ├── protocol/
│   ├── service/
│   ├── storage/
│   └── timer/
├── tutorials/
└── docs/
    └── debug_cases/
```

Directory conventions:

- Public headers live under `include/liteim/<module>/`.
- Library implementations live under `src/<module>/`.
- Executable entry points live under `server/`, and future clients/tools belong in `client_cli/`, `client_qt/`, or `bench/`.
- `tutorials/` contains per-step teaching notes.
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
| IM services | Session binding, online-state synchronization, async message routing, register/login, friend list, private chat, group chat, offline messages, history loading, heartbeat protocol, graceful shutdown, and BotGateway routing. |
| Tooling and validation | CLI client, Python E2E tests, benchmark tooling, broader GoogleTest/gMock coverage, ASan/UBSan, and CI. |
| Demo clients | Qt Widgets chat client with login, conversation list, message bubbles, group chat, AI Bot entry, heartbeat state, and disconnect feedback. |
| PersonaAgent integration | Python BotClient and separate FastAPI / LangGraph AgentService using Knowledge, Memory, Authorized Style RAG, Persona, Safety, tracing, checkpointing, and evaluation. |

README performance numbers and benchmark claims should only be added after real local measurements exist.
