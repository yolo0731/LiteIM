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

## Current Phase

| Phase | Status | Notes |
| --- | --- | --- |
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
| Step 13 tests | done | Added `Acceptor` interface and real localhost connection tests. |
| Step 13 code | done | Implemented `Acceptor` with bind/listen, listen `Channel`, `accept4()` loop, callback, and close cleanup. |
| Step 13 docs | done | Synced README, docs, findings, progress, tutorials, and PROJECT_MEMORY for Acceptor behavior. |
| Step 13 verification | done | CMake configure/build, server smoke, CTest 97/97, diff check, `.gitkeep`, and stale-route path checks passed. |
| Step 13 commit | done | Commit message: `feat(net): implement nonblocking acceptor`. |
| Step 13 review hardening concept | done | Verified external review points and only fixed confirmed local net-layer issues before Step 14. |
| Step 13 review hardening tests | done | Added regression coverage for cross-thread Acceptor close, callback exception fd cleanup, Channel tie, and UniqueFd ownership. |
| Step 13 review hardening code | done | Added `UniqueFd`, made Acceptor close cleanup run on the loop thread, and added Channel weak tie support. |
| Step 13 review hardening docs | done | Synced README/docs/tutorials/planning files and moved public roadmap link inside the repo. |
| Step 13 review hardening verification | done | CMake configure/build, server smoke, CTest 105/105, path stale-route checks, README external-link check, and final diff review passed. |

## Current Decision

Use `/home/yolo/jianli/PROJECT_MEMORY.md` as the source of truth.

LiteIM phases:

1. Step 0: reset workspace and keep only the minimal current-step files.
2. Step 1-20: high-performance network base and multi-Reactor echo server.
3. Step 21-30: MySQL / Redis storage and cache layer.
4. Step 31-40: async IM business services and BotGateway.
5. Step 41-45: CLI, Python E2E, benchmark, GoogleTest/gMock, ASan/UBSan, CI.
6. Step 46-53: Qt Widgets demo client.
7. Step 54: README, architecture diagrams, Qt screenshots, benchmark report, and interview docs.
8. PersonaAgent Step 1-6: Python BotClient, protocol compatibility, FastAPI `/chat`, login/heartbeat/reconnect, Echo Bot.
9. PersonaAgent Step 7-20: six-node LangGraph + Knowledge/Memory/Authorized Style RAG + Persona + Safety + Tool Calling + Checkpoint + Trace + Evaluation.

## Important Boundaries

- Do not continue the old single-Reactor business baseline.
- Do not make `InMemoryStorage` the main storage path. It may only be a future test double/mock.
- Do not reintroduce SQLite.
- Do not use Boost.Asio.
- Do not run MySQL / Redis calls in I/O threads.
- Do not let business threads directly mutate `Session`; responses must go through `EventLoop::queueInLoop()` or `runInLoop()`.
- Do not embed Python/LangGraph into the C++ server; PersonaAgent connects as a BotClient.
- PersonaAgent uses six core LangGraph nodes: `dialogue_policy`, `retrieve`, `tool_router`, `generate_reply`, `safety_check`, and `send_message`.
- Authorized Style RAG data must have consent manifest, source metadata, allowed usage, PII redaction, revocation support, and SafetyGuard protection.
- LiteIM should only expose BotGateway/protocol integration points; Knowledge/Memory/Style RAG, Tool Calling, Trace, Checkpoint, and Evaluation belong to PersonaAgent.
- Do not use WeChat logo, name, icons, or assets in the Qt client.
- Qt is a demo layer; service logic stays in the server.

## Step 0 Kept Files

- `.gitignore`
- `LICENSE`
- `CMakeLists.txt` as an empty Step 0 scaffold
- `README.md`
- `task_plan.md`
- `findings.md`
- `progress.md`
- `docs/architecture.md`
- `docs/project_layout.md`
- `tutorials/README.md`
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
- `TEST(BufferTest, AppendUint8PointerStoresBytes)`
- `TEST(BufferTest, RetrieveAdvancesReadIndex)`
- `TEST(BufferTest, RetrieveAllResetsBuffer)`
- `TEST(BufferTest, RetrieveAllAsStringReturnsReadableDataAndClearsBuffer)`
- `TEST(BufferTest, EnsureWritableBytesExpandsWhenNeeded)`
- `TEST(BufferTest, EnsureWritableBytesCompactsReadableDataBeforeExpanding)`
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

Step 13 implements the nonblocking `Acceptor` listen socket and new-connection callback boundary:

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

Step 13 intentionally implements only listen socket creation, socket options, bind/listen, listen fd registration in `EventLoop`, `accept4()` loop to `EAGAIN`, new-connection callback, fd RAII cleanup, and listen fd cleanup. It does not implement `Session`, `TcpServer`, `EventLoopThread`, `EventLoopThreadPool`, business thread pool, MySQL, or Redis.

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
- `TEST(AcceptorTest, CloseFromOtherThreadRemovesChannelBeforeClosingFd)`
- `TEST(AcceptorTest, AcceptedFdIsClosedWhenCallbackThrowsBeforeTakingOwnership)`
- `TEST(UniqueFdTest, DestructorClosesOwnedFd)`
- `TEST(UniqueFdTest, ReleaseReturnsFdWithoutClosing)`
- `TEST(UniqueFdTest, MoveTransfersOwnership)`
- `TEST(UniqueFdTest, ResetClosesPreviousFd)`
- `TEST(ChannelTest, TiedExpiredOwnerSkipsCallbacks)`
- `TEST(ChannelTest, TiedOwnerStaysAliveDuringCallback)`

Next Step: `Step 14: implement Session`.

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
