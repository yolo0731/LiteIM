# LiteIM Findings

## Authoritative Sources

- `/home/yolo/jianli/PROJECT_MEMORY.md` is the project route source of truth.
- `/home/yolo/jianli/AGENTS.md` is the Codex collaboration and code constraint source.
- `README.md` is the repository's public overview.
- `task_plan.md`, `findings.md`, and `progress.md` are lightweight working memory files only.
- `tutorials/` contains per-Step teaching material.

## Documentation Cleanup Decision

The repository-local `docs/` markdown files were removed because they duplicated other sources:

- Public project summary belongs in `README.md`.
- Long-term route and future Step definitions belong in `/home/yolo/jianli/PROJECT_MEMORY.md`.
- Codex rules and constraints belong in `/home/yolo/jianli/AGENTS.md`.
- Session working memory belongs in `task_plan.md`, `findings.md`, and `progress.md`.
- Step-by-step explanations belong in `tutorials/`.

Do not add a new LiteIM `docs/` markdown tree unless the user explicitly re-approves it.

## Current Valid Technical Findings

- `liteim_net` currently owns the network stack: `Buffer`, `SocketUtil`, `UniqueFd`, `Channel`, `Epoller`, `EventLoop`, `Acceptor`, `Session`, `EventLoopThread`, `EventLoopThreadPool`, and `TcpServer`.
- `liteim_protocol` currently owns TLV wire format, Packet header encoding, network byte order helpers, and TCP stream frame decoding.
- `liteim_concurrency::ThreadPool` is the future boundary for blocking business work such as MySQL, Redis, password hashing, and history queries.
- `liteim/timer` currently provides `TimerHeap` and `TimerManager`; `TimerManager` is compiled into `liteim_net` because it depends on `EventLoop` and `Channel`.
- `TcpServer` uses logical `std::uint64_t` session ids, not fd numbers, to avoid fd reuse bugs.
- `Session` refreshes active time after complete decoded packets, not after arbitrary half-packet bytes.
- `Session` has a 4MB output-buffer high-water mark; slow clients are closed before unbounded memory growth.
- `Channel::handleEvent()` handles `EPOLLERR | EPOLLIN` by running the error callback and then still allowing readable data to be drained.
- `Acceptor::close()` keeps the tested cross-thread close contract and avoids waiting forever if a queued close task cannot run because the loop exits.
- `ThreadPool::stop()` keeps worker-origin stop as an owner-cleanup boundary and serializes external join/cleanup.

## Current Non-Goals

- Do not implement MySQL, Redis, login, MessageRouter, HeartbeatService, CLI, Qt, benchmark, CI, or PersonaAgent during Step 19.
- Do not embed Python, LangGraph, or LLM calls inside the C++ server.
- Do not use Boost.Asio.
- Do not make SQLite or `InMemoryStorage` part of the mainline route.

## Next Step Finding

`Step 19: implement signalfd graceful shutdown` should focus on Linux signal delivery through `signalfd`, integration with `EventLoop`, and graceful server shutdown behavior. It should preserve the current one-loop-per-thread ownership model and avoid broad service-layer work.
