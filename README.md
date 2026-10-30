# Plutus Fincorp: OS Edition

The original PDS capstone was a single-threaded banking program for Windows:
register or login, then fixed deposits, loans and insurance (it is preserved in
[`legacy/`](legacy/)). This edition rebuilds it, in pure C, as a **multi-process,
multi-threaded bank server**. Here, operating-system problems are part of the
bank itself, not separate exercises:

* Every customer request is a **job** scheduled onto **teller threads (the CPUs)**.
* Transfers lock two accounts and can **deadlock**.
* Loan disbursements go through the **Banker's algorithm**.
* Accounts live in a **paged database file** behind a **buffer pool**, made
  crash-safe by a **write-ahead log**.
* Server objects come from a **custom heap allocator**.

The business rules are unchanged: same FD rates, loan CIBIL tables and EMI
formula, and insurance premiums as the original.

About 9,500 lines of C11 plus 1,100 lines of tests. There are no dependencies
beyond libc and pthreads. Built and tested on macOS (Apple clang).

## Quick start

```sh
make                      # builds bin/plutusd, bin/plutus, bin/plutus-admin, bin/plutus-load
make test                 # unit tests + end-to-end integration test

bin/plutusd               # start the bank (foreground; Ctrl-C stops it cleanly)
bin/plutus                # customer terminal (another window)
bin/plutus-admin top      # live dashboard straight from shared memory
bin/plutus-admin          # operator console (HELP lists commands)
bin/plutus-load -p 4 -t 8 -n 100     # 32 simulated customers in 4 processes
```

Data goes into `./plutus-data/` (database, WAL, statement history, log,
sockets). Use `-D DIR` on every program to pick another directory.

## Where each OS concept lives

| OS topic | In the bank | Code |
|---|---|---|
| Processes, `fork`, `waitpid`, daemons | Supervisor forks logger, engine and auditor, restarts a crashed engine with exponential back-off, guards against crash loops; `-d` double-fork daemonises | `src/server/main.c` |
| Signals | Self-pipe pattern; graceful `SIGTERM` (drain, then checkpoint); `SIGCHLD` reaping; watchdog `SIGKILL` | `main.c`, `engine.c`, `auditor.c` |
| IPC | Unix-domain sockets (clients, admin), pipes (log stream, load-test results), POSIX shared memory with C11 atomics (live stats), `flock` pid file | `src/common/proto.c`, `src/server/shm.c` |
| Threads, mutexes, condition variables | Session, teller, timer, WAL-flusher and deadlock-detector threads | everywhere |
| Semaphores | Counting semaphore built from mutex + condvar (macOS lacks `sem_init`) | `src/common/sync.c` |
| Producer-consumer | Ready queue is a bounded buffer (`slots`/`items`); WAL group commit | `engine.c`, `src/storage/wal.c` |
| Readers-writers | Hand-written rwlock with reader-pref / writer-pref / fair modes; guards the account index, the checkpoint gate and the audit gate | `src/common/sync.c`, `src/bank/bank.c` |
| Race conditions | `set race on` drops the account lock and real deposits lose updates; the auditor process catches the ledger mismatch | `bank.c`, `auditor.c` |
| CPU scheduling | FCFS, SJF, SRTF, RR, priority + aging, MLFQ + boost; switchable live; preemption at work-unit granularity; live Gantt chart; offline simulator using the same run-queue code | `src/sched/`, `engine.c` |
| Deadlock | Lock manager: `none` (really hangs), `detect` (wait-for graph + DFS, abort youngest), `ordered` (prevention: no circular wait), `trylock` (prevention: no hold-and-wait); live dining-philosophers ring | `src/deadlock/lockmgr.c` |
| Deadlock avoidance | Banker's algorithm over loan fund pools; unsafe disbursements are deferred and granted automatically when repayments make the state safe | `src/deadlock/banker.c` |
| Memory allocation | Custom heap on `mmap`: first, next, best and worst fit with boundary-tag coalescing, plus a buddy allocator; fragmentation stats and a heap map | `src/mem/heap.c` |
| Paging / virtual memory | 4 KiB pages, buffer pool with FIFO, LRU and Clock replacement, pinning, dirty write-back; OPT and Belady's anomaly in the simulator | `src/storage/pager.c` |
| File systems / crash consistency | Slotted page file, CRC-32 per page and per log record, redo-only WAL, group commit, checkpoints, torn-write handling, recovery after `kill -9` | `src/storage/wal.c`, `src/storage/db.c` |
| Security | Salted, iterated SHA-256 password hashing (own implementation), constant-time compare, lockout after 5 failures, Luhn check digits, admin socket restricted by `getpeereid` | `src/common/crypto.c`, `bank.c`, `engine.c` |

## A 10-minute viva demo

Window 1: `bin/plutusd`. Window 2: `bin/plutus-admin top`. Window 3: the commands below.

```sh
A=bin/plutus-admin

# 1. Scheduling: compare all six algorithms on the same bank workload
$A scenario sched 3
$A scenario starvation          # priority starves the batch job; aging fixes it

# 2. Scheduling live: flood the real tellers, watch the Gantt chart, switch policy
$A scenario flood 300 ; $A gantt 2000
$A set sched rr ; $A scenario flood 300 ; $A gantt 2000

# 3. Deadlock live: 4 transfers in a ring (dining philosophers) on real accounts
$A set deadlock none    ; $A scenario deadlock 4   # hangs, shows the wait-for graph, then rescues
$A set deadlock ordered ; $A scenario deadlock 4   # prevention
$A set deadlock detect  ; $A scenario deadlock 4   # detection + victim abort

# 4. Race condition live: lost updates on a real account
$A set race on ; bin/plutus-load -m hot -p 2 -t 4 -n 25 -a 2
$A audit       # MISMATCH: the auditor process has logged it too
$A set race off ; $A reconcile

# 5. Banker's algorithm
$A scenario banker      # textbook walk-through
$A banker               # live matrices over the loan pools

# 6. Memory and paging
$A scenario alloc ; $A mem
$A scenario paging      # FIFO/LRU/CLOCK/OPT + Belady's anomaly
$A pager                # live buffer pool: start plutusd with -f 3, then plutus-load -a 100 to force evictions

# 7. Crash recovery
bin/plutus-load -p 2 -t 4 -n 200 &   # while it runs:
$A crash                # engine aborts; the supervisor restarts it; WAL redo replays every commit
$A audit                # CONSISTENT, nothing acknowledged was lost

# Also: scenario rw | prodcons | race | philosophers, and queue, locks, wal, stats
```

Useful server flags (`bin/plutusd --help`): `-t` tellers, `-s` policy, `-q`
quantum, `-T` tick length, `-k` deadlock policy, `-f` frames, `-r`
replacement, `-a` allocator, `--month` seconds per bank month (month-end
interest and premium batches), `--pools` loan fund pools in lakhs (try
`--pools 10,10,10,10` to make the Banker's algorithm defer disbursements).

## Customer commands (wire protocol)

Fields are TAB-separated, one request per line. You can try it with
`nc -U plutus-data/plutus.sock`.

```
REGISTER name city age phone password deposit   LOGIN account password   LOGOUT
BALANCE   DEPOSIT amount   WITHDRAW amount   TRANSFER to amount   STATEMENT [n]
FD_QUOTE amount months senior   FD_OPEN amount months senior   FD_LIST   FD_CLOSE fd
LOAN_QUOTE cibil type lakhs tenure income   LOAN_APPLY ...   LOAN_LIST
LOAN_DRAW loan amount   LOAN_REPAY loan amount
INS_QUOTE kind a1 a2 a3   INS_BUY kind holder beneficiary a1 a2 a3   INS_LIST
```

`bin/plutus -u ACCOUNT -p PASSWORD COMMAND ARGS...` runs one command from a script.

## Layout

```
src/common/    utilities, semaphore + rwlock, SHA-256/CRC-32, wire protocol
src/mem/       custom heap (free-list fits + buddy)
src/sched/     run queue with six policies, virtual-time simulator
src/deadlock/  lock manager + wait-for graph detector, Banker's algorithm
src/storage/   WAL, buffer pool / pager, crash-safe record store
src/bank/      business rules (ported from legacy/) and transactional operations
src/server/    supervisor, engine, auditor, shared memory, demo scenarios
src/client/    plutus, plutus-admin, plutus-load
tests/         unit tests per subsystem + end-to-end integration test
legacy/        the original capstone, untouched
docs/          design notes
```

See [`docs/DESIGN.md`](docs/DESIGN.md) for how each subsystem works and the
invariants it keeps.

## Testing

* `make test` runs all unit tests, then boots the real daemon and drives it
  (protocol, 150 concurrent deadlock-prone transfers, `kill -9` with recovery,
  race-mode detection, Banker's deferral, every scenario, restart).
* `make debug`: AddressSanitizer + UBSan build. `make tsan`: ThreadSanitizer
  build. Both pass the whole suite. TSan's only reports are the two deliberate
  races inside `scenario race`.
