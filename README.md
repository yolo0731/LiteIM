# LiteIM

LiteIM is a C++17 instant messaging project for internship preparation. The server will use Linux socket, non-blocking I/O and epoll to implement a simplified Reactor model. The desktop client will use Qt Widgets and QTcpSocket. The protocol will use a fixed-size TLV-style header plus JSON body to handle TCP sticky packets and partial packets.

When developing in the `/home/yolo/jianli` workspace, also read `../PROJECT_MEMORY.md` for the current project goals, Step rules and teaching workflow.

The first milestone is the server MVP:

1. CMake project skeleton
2. Packet protocol encoding and validation
3. TCP frame decoder
4. epoll-based Reactor
5. Session lifecycle
6. TcpServer connection manager
7. MessageRouter heartbeat response
8. Register and login
9. Private chat
10. Group chat
11. SQLite persistence
12. Heartbeat timeout

## Tech Stack

- Language: C++17
- Build: CMake
- Server networking: Linux socket, non-blocking I/O, epoll
- Client UI: Qt 6 Widgets, QTcpSocket
- Protocol: fixed-size header + JSON body
- Storage: SQLite
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
│       └── service/
│           └── MessageRouter.hpp
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
│   └── service/
│       └── MessageRouter.cpp
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

Current tests cover Packet encoding/validation, TCP frame decoding, Buffer behavior, SocketUtil helpers, Reactor interface declarations, Epoller add/mod/del plus LT poll behavior, EventLoop dispatch/quit behavior, Channel automatic EventLoop update plus callback dispatch behavior, Acceptor bind/listen/accept callback behavior, Session read/decode/write/close lifecycle behavior, TcpServer accept/session tracking/send/shutdown behavior, and MessageRouter heartbeat/error response routing.
