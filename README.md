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
- Main Reactor + sub Reactor thread pool. I/O threads handle fd events, Packet/TLV codec work, output-buffer backpressure, and `Session` lifetime.
- Business `ThreadPool` for future blocking work such as MySQL queries, Redis operations, password hashing, and history loading.
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
|  - output-buffer high-water-mark protection                                 |
|                                                                            |
|  Business Thread Pool                                                       |
|  - AuthService / ChatService / GroupService / HistoryService                |
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
- Reactor-owned objects such as `TcpServer` and `TimerManager` must stop and destruct in their owner loop thread.
- Redis is cache/state, not the final source of message truth. Message entities belong in MySQL.

## Current Modules

- `liteim_base`: `Config`, `Logger`, `ErrorCode`, `Status`, `Timestamp`, and raw-byte aliases `Byte` / `Bytes`.
- `liteim_protocol`: `MessageType`, `TlvType`, `ByteOrder`, `Packet`, `TlvCodec`, and `FrameDecoder`.
- `liteim_net`: `Buffer`, `SocketUtil`, `UniqueFd`, `Channel`, `Epoller`, `EventLoop`, `Acceptor`, `Session`, `EventLoopThread`, `EventLoopThreadPool`, `SignalWatcher`, and `TcpServer`.
- `liteim_concurrency`: fixed-size business `ThreadPool`.
- `liteim/timer`: `TimerHeap` and `TimerManager`, currently linked into the network layer because `TimerManager` depends on `EventLoop` and `Channel`.

## Build And Test

Requirements:

- Linux
- CMake
- A C++17 compiler
- POSIX sockets, `epoll`, `eventfd`, `timerfd`, and `signalfd`

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run the echo server executable:

```bash
./build/server/liteim_server
```

The server starts a real `EventLoop + TcpServer` echo server on the configured host and port. It handles `Ctrl-C` / `SIGTERM` through `signalfd`, stops `TcpServer` in the base loop thread, and exits cleanly. For a bounded smoke check, use:

```bash
timeout 1s ./build/server/liteim_server || test $? -eq 124
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
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
├── include/liteim/
│   ├── base/
│   ├── concurrency/
│   ├── net/
│   ├── protocol/
│   └── timer/
├── src/
│   ├── base/
│   ├── concurrency/
│   ├── net/
│   ├── protocol/
│   └── timer/
├── server/
├── tests/
│   ├── base/
│   ├── concurrency/
│   ├── net/
│   ├── protocol/
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

These are not a public architecture manual. They are retained because they document concrete lifetime, threading, and cleanup bugs that shaped the current implementation.

## Roadmap

LiteIM is being built in phases:

| Phase | Goal |
| --- | --- |
| Network base | CMake, GoogleTest, base utilities, TLV protocol, frame decoder, Buffer, socket helpers, Reactor interfaces, `Epoller`, `Channel`, `EventLoop`, `Acceptor`, `Session`, multi-Reactor `TcpServer`, business `ThreadPool`, timerfd heartbeat cleanup, and signalfd shutdown. |
| Storage and cache | MySQL connection pool, RAII connection guard, prepared statement wrapper, DAO layer, Redis client/pool, online status, unread counters, and login rate limiting. |
| IM services | Register/login, friend list, private chat, group chat, offline messages, history loading, heartbeat protocol, graceful shutdown, and BotGateway routing. |
| Tooling and validation | CLI client, Python E2E tests, benchmark tooling, broader GoogleTest/gMock coverage, ASan/UBSan, and CI. |
| Demo clients | Qt Widgets chat client with login, conversation list, message bubbles, group chat, AI Bot entry, heartbeat state, and disconnect feedback. |
| PersonaAgent integration | Python BotClient and separate FastAPI / LangGraph AgentService using Knowledge, Memory, Authorized Style RAG, Persona, Safety, tracing, checkpointing, and evaluation. |

README performance numbers and benchmark claims should only be added after real local measurements exist.
