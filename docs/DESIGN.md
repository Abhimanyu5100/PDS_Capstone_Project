# Plutus Fincorp OS Edition: design notes

## 1. Process architecture

```
                 +-----------------------------------------------------+
                 | plutusd (supervisor)                                |
                 |   flock(plutusd.pid)  shm_open  pipe  sigaction     |
                 |   main loop: poll(self-pipe) -> waitpid -> restart  |
                 +----+--------------+-------------------+-------------+
                      | fork         | fork              | fork
             +--------v---+   +------v-------------+   +-v--------------+
             | logger     |   | engine             |   | auditor        |
             | read(pipe) |   | threads: sessions, |   | heartbeat      |
             | -> file    |   | tellers, timer,    |   | watchdog,      |
             | (rotates)  |   | admin, WAL flusher,|   | AUDIT every N s|
             +------^-----+   | deadlock detector  |   +---^------------+
                    |         +--+--------------+--+       |
       log lines ---+            |              |          | admin socket
       (one write() each,        | plutus.sock  | admin.sock (0600 + getpeereid)
        <= PIPE_BUF: atomic)     |              |
                          plutus / plutus-load  plutus-admin
                                                plutus-admin top --(mmap, read-only)--> shared memory
```

* **Supervisor.** It turns `SIGCHLD`, `SIGTERM`, `SIGINT` and `SIGHUP` into
  bytes on a non-blocking self-pipe, so nothing complex runs in a signal
  handler.
  - When the engine dies unexpectedly, the supervisor restarts it after
    200 ms. The delay doubles on each further crash, up to 5 s. Five crashes
    within 30 s count as a crash loop, and it shuts down.
  - On `SIGTERM` it stops the engine first (the engine drains its queue and
    checkpoints), then the auditor. It then closes the log pipe, so the logger
    sees EOF after the last line and exits.
* **Shared memory.** `/plutus-<uid>-<crc32(datadir)>` holds a single
  `shm_stats_t`. The name is kept under the 31-character limit of macOS. All
  fields are C11 atomics, which are lock-free and therefore valid across
  processes. The engine writes the segment; `plutus-admin top` and the auditor
  read it.
* **Auditor.** It is a separate process, so it keeps working even if the engine
  wedges. A heartbeat that is stale for 5 s gets the engine `SIGKILL`ed, and
  WAL recovery takes over on restart. Every audit period it asks the engine to
  sum all balances and compare the total with the ledger.

## 2. Request lifecycle and CPU scheduling

```
session thread                    ready queue (runq_t)               teller thread (CPU)
--------------                    --------------------               -------------------
parse line, auth check
csem_timedwait(slots, 5 s) --BUSY on timeout
rq_push(job) ------------------>  [policy-ordered lists]  <-------- csem_wait(items); rq_pop()
csem_post(items)                                                     for each unit:
wait on request condvar                                                sleep(tick); remaining--
                                                                       rq_should_preempt()? -> rq_push, keep slot
                                                                     commit phase: bank op
<-------------------------------------------------------------------- signal condvar; csem_post(slots)
send response
```

* **Bursts.** Each operation has a burst measured in work units: BALANCE 1,
  DEPOSIT 2, TRANSFER 4, LOGIN 4 (password hashing), LOAN_APPLY 8, MONTH_END
  20 + accounts/4. One unit is one `tick` of teller time (default 2 ms).
* **Priority.** It comes from the customer's tier, which is derived from their
  balance: VIP 0, Premium 1, Regular 2. Batch jobs run at priority 3.
* **One run queue.** The same `runq_t` serves both the live tellers (time in
  ns) and `sim.c` (time in ticks). The unit tests check the simulator against
  textbook answers (Silberschatz chapter 5), so the live scheduler runs
  verified code.
* **No self-deadlock in the scheduler.** `slots` counts jobs admitted to the
  system, not queue positions. A preempted job therefore re-enters the queue
  without waiting for a slot. Otherwise every teller could block re-queueing
  into a full queue that nobody drains.
* **MLFQ.** It has three levels, and the quantum is `q << level`. A job that
  uses its whole allotment is demoted. A job at a higher level preempts a
  running job at a lower level. Every `boost` ms all jobs return to level 0.
* **PRIO.** It is preemptive. Effective priority = priority - (time waited /
  aging interval).

## 3. Locking, deadlock and the Banker's algorithm

* **Lock order.** The order is: account locks (lock manager), then the audit
  gate, then the DB checkpoint gate, then WAL, then the buffer pool, then a
  page latch. The index rwlock is never held while waiting for an account lock.
* **Lock manager.** It keeps one entry per account: owner, waiter count and a
  condvar, all under one manager mutex. Waiting uses a 100 ms timed wait, so a
  victim flag set by the detector is noticed promptly.
* **Detector.** It runs every 50 ms. It builds the wait-for graph: each waiting
  transaction has an edge to the owner of the lock it wants. A white/grey/black
  DFS finds the cycles. Under `detect` it marks the youngest transaction in the
  cycle as the victim. The victim releases its locks, backs off for a random,
  exponentially growing time, and retries.
  - The back-off matters. Without it, the victim grabs its first lock again
    before the woken waiter can run. The same cycle forms forever: a
    livelock, which the unit tests caught.
* **Banker's algorithm.**
  - Resources are the four loan fund pools.
  - Max is a customer's sanctioned amount per pool, Allocation is what they
    have drawn, and Need = Max - Allocation.
  - A sanction is refused if it would leave the state unsafe.
  - A DRAW is granted only if a safe sequence still exists after pretending to
    grant it. Otherwise it is queued FIFO and retried after every repayment.
  - Repayment lowers both Allocation and Max, as for a term loan.
  - State is rebuilt from the persisted loan records at start-up.

## 4. Storage engine

* **`plutus.db`.**
  - Page 0 is the superblock: magic, CRC, checkpoint LSN, next LSN and creation
    time.
  - Every other page has a 64-byte header (magic, CRC-32, page id, page_lsn)
    followed by 15 slots of 256 bytes.
  - A record is `{type, key, owner, body[232]}`, and a RID is `page << 4 | slot`.
* **Commit.** A transaction appends UPDATE records (full 256-byte after-images)
  and a COMMIT record to the WAL, contiguously. It waits until the COMMIT is
  durable, then copies the images into buffer-pool pages and stamps page_lsn.
  - Uncommitted data never reaches a page (no-steal), so recovery is redo-only.
  - Redo is idempotent: an image is applied only if `page_lsn < record lsn`.
* **Group commit.** The flusher thread swaps the log buffer, writes it and calls
  fsync once. It then wakes every committer whose LSN is now durable.
  `--sync full` uses `F_FULLFSYNC`, which on macOS also flushes the drive cache.
* **Checkpoint.** It runs every 30 s, at start-up and at shutdown.
  - It takes the write side of a writer-preference rwlock, which commits hold
    as readers, so no commit is half-applied while it runs.
  - It flushes all dirty pages, fsyncs, writes the superblock with the
    checkpoint LSN, and then truncates the WAL.
* **Recovery.** It replays committed transactions from the WAL. A CRC mismatch
  or short read marks a torn tail, and the incomplete transaction is
  discarded. Recovery then rebuilds the free-slot map, and a checkpoint makes
  the result durable.
* **Buffer pool.**
  - Page faults pick a victim among unpinned frames by FIFO (load time), LRU
    (last use) or Clock (reference bit plus hand).
  - A dirty victim is written back after the WAL rule is checked.
  - If every frame is pinned, the fetcher waits on a condvar.
  - Page I/O happens under the pool mutex. That keeps it simple and correct, at
    the cost of serialising page faults.
* **Statement history.** It is an append-only `history.log` of fixed 96-byte
  entries, read backwards to show a statement.

## 5. Memory allocator

`g_heap` is an `mmap`'d region, 64 MiB by default. Requests, session
connection buffers, index entries and their child arrays, and deferred
disbursements are all allocated there with `pmalloc` and `pfree`.

* **Free-list fits.**
  - Each block has a 16-byte header (size | used bit, requested size) and an
    8-byte footer.
  - Free blocks sit on an address-ordered, doubly linked list.
  - On free, the allocator coalesces with the next block (via the header) and
    with the previous block (via the footer).
  - It splits a block when the remainder can hold a minimum block of 48 bytes.
* **Buddy.**
  - Blocks are powers of two from 32 bytes up; free lists are kept per order.
  - A block's buddy is at `offset ^ size`. It merges only when the buddy's
    header says it is free and of the same order.
* **Stats and checks.** The allocator reports external fragmentation
  (1 - largest free / total free), internal fragmentation (1 - requested /
  allocated) and search steps per allocation. `heap_check` walks every block to
  verify footers, find missed coalesces and check that the free list matches
  the heap.

## 6. Security notes

* Passwords are stored as SHA-256 iterated 20,000 times over a 16-byte random
  salt. The SHA-256 is implemented here and tested against FIPS vectors.
  Verification uses a constant-time comparison.
* An account locks after 5 failed logins. The counter is kept in memory, so a
  restart clears it.
* Account numbers carry a Luhn check digit.
* The customer socket is world-connectable (0666). The admin socket is 0600,
  and it also checks the peer's uid with `getpeereid`.
* The protocol is plain text over a local socket, with no TLS. This is
  intended for a single machine.

## 7. Known limitations

* The database index and the free-slot map are rebuilt by a full scan at
  start-up, which is fine for thousands of accounts.
* Records are fixed at 256 bytes, so an account can have at most 14 FDs, loans
  and policies processed in one month-end transaction.
* The bank month is simulated, `--month` seconds long (default 120 s). FD
  maturity, interest and premiums follow this clock.
* Loan EMI uses the original capstone's formula (`P * (1+r)^(n/12) / n`), kept
  on purpose for continuity. It is not the standard amortisation formula.
