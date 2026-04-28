# LiteIM

LiteIM is a C++17 instant messaging project for internship preparation. The server will use Linux socket, non-blocking I/O and epoll to implement a simplified Reactor model. The desktop client will use Qt Widgets and QTcpSocket. The protocol will use a fixed-size TLV-style header plus JSON body to handle TCP sticky packets and partial packets.

Before continuing development, read `PROJECT_MEMORY.md` for the current project goals, Step rules and teaching workflow.

The first milestone is the server MVP:

1. CMake project skeleton
2. Packet protocol encoding and validation
3. TCP frame decoder
4. epoll-based Reactor
5. Session lifecycle
6. Register and login
7. Private chat
8. Group chat
9. SQLite persistence
10. Heartbeat timeout

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
├── docs/
├── server/
│   ├── CMakeLists.txt
│   └── main.cpp
├── client_qt/
│   └── CMakeLists.txt
├── sql/
└── tests/
    ├── CMakeLists.txt
    └── test_smoke.cpp
```

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
LiteIM server starting...
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

At this stage, the test target is only a build-chain placeholder. Real protocol tests will be added after `Packet` and `FrameDecoder` are implemented.
