# Net Lifecycle Review Hardening

Date: 2026-05-09

Context: after Step 17, an external review pointed out several lifecycle and slow-client risks in the network layer. This note records which points were accepted, how they were reproduced with tests, and what was changed.

## Accepted Bugs

### 1. EventLoopThread self-stop lifecycle

Old shape:

```text
loop thread calls EventLoopThread::stop()
  -> stop() calls loop_->quit()
  -> stop() detaches its own std::thread
  -> stop() clears loop_ / running_
  -> threadFunc() later also clears loop_ / running_
```

The detach path avoided self-join, but it weakened ownership. The owner could think cleanup had completed while the worker was still executing. If the `EventLoopThread` object was destroyed before `threadFunc()` finished, the worker could still touch `this`.

Fix:

- `stop()` only requests `loop_->quit()`.
- `stop()` returns immediately when called from the managed worker thread.
- owner-thread `stop()` joins the worker.
- `loop_`, `thread_id_`, and `running_` are cleared only in `threadFunc()` when the worker is actually exiting.
- `join_started_` prevents concurrent external stop calls from racing on `std::thread::join()`.

Regression test:

```cpp
TEST(EventLoopThreadTest, OwnerStopWaitsAfterStopIsRequestedInsideLoop)
```

The test queues a task that calls `thread.stop()` from inside the loop, then sleeps. The owner calls `stop()` and must wait until the task finishes and the worker exits.

### 2. Session output buffer had no high-water mark

Old shape:

```text
Session::sendEncodedInLoop()
  -> output_buffer_.append(encoded)
  -> enableWriting()
```

If a client stopped reading, repeated sends could grow `output_buffer_` without an upper bound.

Fix:

- Added `kSessionOutputHighWaterMark = 4 * 1024 * 1024`.
- Before appending, `Session` checks `pending bytes + encoded bytes`.
- If the next append would exceed 4MB, the connection is closed in its owner I/O loop.

Regression test:

```cpp
TEST(SessionTest, CloseWhenPendingOutputExceedsHighWaterMark)
```

The test queues multiple max-size packets while the peer does not read. The session must close and clear pending output when the high-water mark is exceeded.

### 3. TcpServer used fd as session identity

Old shape:

```text
session_id = accepted_fd
sessions_[session_id] = session
close callback -> removeSession(session_id)
```

fd is a kernel resource number, not a stable connection identity. After close, Linux can quickly reuse the same fd. If an old close callback is delayed and a new connection receives the same fd, the old remove can erase the new session.

Fix:

- Added `next_session_id_`.
- `TcpServer::createSessionInLoop()` assigns a monotonic `std::uint64_t` id.
- `Session` stores the id and exposes `id()`.
- `sessions_` is keyed by logical session id, not fd.
- `sendToSession()` now accepts `std::uint64_t`.

Regression coverage:

```cpp
TEST(ReactorInterfaceTest, TcpServerHeaderIsSelfContained)
TEST(TcpServerTest, SendToSessionFromOtherThreadDeliversPacket)
```

The API and test now use `session->id()` instead of `session->fd()`.

### 4. Channel returned immediately after EPOLLERR

Old shape:

```text
EPOLLERR -> error callback -> return
```

If epoll delivered `EPOLLERR | EPOLLIN`, the read callback was skipped. There can still be readable data or EOF state that `Session::handleRead()` should consume.

Fix:

- `EPOLLERR` still runs the error callback first.
- It no longer returns immediately.
- Read and write dispatch continue based on the same `revents_` snapshot.

Regression test:

```cpp
TEST(ChannelTest, ErrorWithReadableEventInvokesErrorThenRead)
```

### 5. Acceptor close could wait forever after loop exit race

The review suggested making `Acceptor::close()` owner-loop-only. LiteIM already has a tested cross-thread close contract, so the contract was kept. But the review still exposed a real race in the current implementation:

```text
non-loop thread calls Acceptor::close()
  -> loop is not stopped yet
  -> close task is queued
  -> close waits on future
  -> loop exits before running the close task
  -> future is never fulfilled
```

Fix:

- Keep cross-thread close.
- After queueing the close task, wait in short intervals.
- If `EventLoop::isStopped()` becomes true before the future is ready, run the stopped-loop fallback cleanup and return.

Regression test:

```cpp
TEST(AcceptorTest, CloseFromOtherThreadWhileLoopExitsWithQueuedCloseDoesNotBlock)
```

The test holds the loop inside a pending task, starts cross-thread `close()`, then lets the task call `quit()` before the queued close task can run. The close call must return instead of blocking forever.

## Not Accepted As-Is

### Acceptor::close owner-loop-only rewrite

The owner-loop-only contract was not accepted as a direct rewrite. It is a valid muduo-style design, but the current LiteIM code already has explicit tests for cross-thread Acceptor close, including the case where the loop is created but not yet running. This pass kept that contract and fixed the concrete permanent-wait race instead. A future redesign can still simplify the contract if the higher-level owner lifecycle is changed at the same time.

### ThreadPool swap-and-join rewrite

The review suggested replacing `stop_mutex_` with a swap-and-join shutdown. The current code already serializes external `stop()` calls and has regression coverage for concurrent stop. Worker-origin `stop()` is intentionally an owner-cleanup boundary: the worker only requests shutdown, and the owner later joins and clears `workers_`.

Deleting a `ThreadPool` object from inside one of its own tasks remains unsupported. Even detaching the current worker would not make that safe, because the worker would return to `workerLoop()` and continue using `this` after the object was destroyed.

### Broad variable merges

Some suggested merges are good future cleanup candidates, but were not part of this pass:

- `Session` bools to a full enum state machine.
- `EventLoop` flag reduction.
- `FrameDecoder` input-buffer redesign.
- Removing testing getters from `EventLoopThreadPool`.

Those need separate tests and should not be mixed into lifecycle bug fixes.

## Verification

Targeted verification:

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "(Acceptor|EventLoopThread|Session|TcpServer|Channel)"
```

Expected result: all targeted tests pass.

Full verification for the final patch also runs:

```bash
ctest --test-dir build --output-on-failure
./build/server/liteim_server
git diff --check
```

## Interview Answer

One concise answer:

> After an external review I reproduced five real issues with regression tests: self-stop in `EventLoopThread`, a queued-close wait race in `Acceptor`, missing output-buffer high-water mark in `Session`, fd reuse in `TcpServer` session ids, and `Channel` returning after `EPOLLERR` even when readable data was pending. I fixed them by moving `EventLoopThread` state cleanup into `threadFunc()`, keeping `Acceptor` cross-thread close but adding a stopped-loop wait fallback, adding a 4MB output high-water close path, assigning monotonic logical session ids, and letting `EPOLLERR | EPOLLIN` dispatch error first and then read. I did not blindly accept all style suggestions; for `ThreadPool::stop()` and broad variable merges I kept the existing tested contracts and documented why the proposed rewrites should be separate design changes.
