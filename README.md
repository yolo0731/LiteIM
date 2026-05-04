# LiteIM

LiteIM is a C++17 instant messaging project for internship preparation. The main learning line is Linux network programming: non-blocking sockets, epoll, Reactor, TCP stream framing and Session lifecycle management. The desktop client will use Qt Widgets and QTcpSocket to provide a WeChat-style chat UI without copying WeChat branding or assets.

When developing in the `/home/yolo/jianli` workspace, also read `../PROJECT_MEMORY.md` for the current project goals, Step rules and teaching workflow.

The current completed milestone is Step 15: the server networking foundation, protocol layer, heartbeat router, storage/cache interfaces and SQLite storage implementation are in place. The next milestones are:

1. Service MVP: register/login, private chat, group chat, history query, heartbeat timeout and CLI client.
2. Networking resume highlights: eventfd wakeup, queued cross-thread tasks, EventLoopThreadPool, business ThreadPool and Session high-water-mark backpressure.
3. Qt client: optional Qt Widgets target, QTcpSocket codec, login/register UI, three-column chat window, message bubbles, heartbeat and AI bot contact entry.
4. Verification: end-to-end tests, simple benchmark client, screenshots and final interview notes.

## Tech Stack

- Language: C++17
- Build: CMake
- Server networking: Linux socket, non-blocking I/O, epoll
- Client UI: Qt Widgets, QTcpSocket
- Protocol: fixed-size header + JSON body
- Storage: `IStorage` abstraction with current SQLite implementation; MySQL/Redis may be added later as simple supporting components, not the main project claim
- JSON: nlohmann_json
- Tests: lightweight C++ test executable first, GoogleTest or Catch2 later

Boost.Asio is intentionally not used. The server networking layer will be implemented with Linux socket and epoll so the project can demonstrate event-driven I/O, connection lifecycle management, heartbeat detection and TCP stream framing.

## Directory Structure

```text
LiteIM/
├── CMakeLists.txt
├── README.md
├── include/
│   └── liteim/
│       ├── net/
│       │   ├── Acceptor.hpp
│       │   ├── Buffer.hpp
│       │   ├── Channel.hpp
│       │   ├── Epoller.hpp
│       │   ├── EventLoop.hpp
│       │   ├── Session.hpp
│       │   ├── SocketUtil.hpp
│       │   └── TcpServer.hpp
│       ├── protocol/
│       │   ├── FrameDecoder.hpp
│       │   ├── MessageType.hpp
│       │   └── Packet.hpp
│       ├── service/
│       │   └── MessageRouter.hpp
│       └── storage/
│           ├── ICache.hpp
│           ├── IStorage.hpp
│           ├── NullCache.hpp
│           ├── SQLiteStorage.hpp
│           └── StorageTypes.hpp
├── src/
│   ├── CMakeLists.txt
│   ├── net/
│   │   ├── Acceptor.cpp
│   │   ├── Buffer.cpp
│   │   ├── Channel.cpp
│   │   ├── Epoller.cpp
│   │   ├── EventLoop.cpp
│   │   ├── Session.cpp
│   │   ├── SocketUtil.cpp
│   │   └── TcpServer.cpp
│   ├── protocol/
│   │   ├── FrameDecoder.cpp
│   │   └── Packet.cpp
│   ├── service/
│   │   └── MessageRouter.cpp
│   └── storage/
│       ├── NullCache.cpp
│       └── SQLiteStorage.cpp
├── docs/
├── server/
│   ├── CMakeLists.txt
│   └── main.cpp
├── client_qt/
│   └── CMakeLists.txt
├── sql/
└── tests/
    ├── CMakeLists.txt
    ├── TestUtil.hpp
    ├── test_acceptor.cpp
    ├── test_protocol.cpp
    ├── test_frame_decoder.cpp
    ├── test_message_router.cpp
    ├── test_buffer.cpp
    ├── test_channel.cpp
    ├── test_epoller.cpp
    ├── test_event_loop.cpp
    ├── test_session.cpp
    ├── test_socket_util.cpp
    ├── test_sqlite_storage.cpp
    ├── test_storage_interfaces.cpp
    ├── test_tcp_server.cpp
    └── test_reactor_interfaces.cpp
```

Headers and implementation files are intentionally separated:

- `include/liteim/...` contains headers used by other targets.
- `src/...` contains library implementation files.
- `server/main.cpp` is only the server executable entry point.
- Tests include project headers through paths such as `liteim/net/Buffer.hpp`.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/server/liteim_server
```

Expected output:

```text
LiteIM server listening on port 9000
```

Press `Ctrl+C` to stop the server through the `signalfd` shutdown path.

## Test

```bash
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

Current tests cover Packet encoding/validation, TCP frame decoding, Buffer behavior, SocketUtil helpers, Reactor interface declarations, Epoller add/mod/del plus LT poll behavior, EventLoop dispatch/quit behavior, Channel automatic EventLoop update plus callback dispatch behavior, Acceptor bind/listen/accept callback behavior, Session read/decode/write/close lifecycle behavior, TcpServer accept/session tracking/send/shutdown behavior, MessageRouter heartbeat/error response routing, storage/cache interface contracts, and SQLiteStorage persistence behavior.
