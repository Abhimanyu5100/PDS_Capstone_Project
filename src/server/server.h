/* server.h - configuration and the shared-memory segment of the Plutus daemon.
 *
 * Process tree (plutusd):
 *
 *   plutusd (supervisor)  -- forks, watches SIGCHLD, restarts the engine on a crash
 *    |-- plutus-logger    -- reads one pipe, timestamps, writes plutus.log
 *    |-- plutus-engine    -- sockets, scheduler, tellers, bank (threads)
 *    `-- plutus-auditor   -- watchdog + periodic ledger audit over the admin socket
 *
 * All four map the same POSIX shared-memory segment (shm_open + mmap). The
 * engine publishes live statistics there with C11 atomics, which are lock-free
 * and therefore safe across processes; `plutus-admin top` maps it read-only. */
#ifndef PLUTUS_SERVER_H
#define PLUTUS_SERVER_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "../bank/bank.h"
#include "../mem/heap.h"
#include "../sched/sched.h"

#define MAX_TELLERS 32
#define SHM_MAGIC 0x504C534Du /* "PLSM" */
#define SHM_VERSION 3

typedef struct {
    char data_dir[400];
    char sock_path[512];
    char admin_path[512];
    int tellers;
    int queue_cap;
    sched_policy_t sched;
    int quantum;
    int64_t tick_us;
    int64_t aging_ms, boost_ms;
    alloc_policy_t alloc;
    size_t heap_bytes;
    int max_sessions;
    int64_t checkpoint_secs;
    int64_t audit_secs;
    bool foreground;
    int log_level;
    bank_config_t bank;
} server_config_t;

typedef struct {
    uint32_t magic, version;
    _Atomic int64_t supervisor_pid, engine_pid, logger_pid, auditor_pid;
    _Atomic int64_t heartbeat_ns;    /* CLOCK_MONOTONIC, system-wide */
    _Atomic int64_t engine_started;  /* wall clock */
    _Atomic uint64_t engine_restarts;
    _Atomic int32_t engine_ready;
    /* scheduler */
    _Atomic int32_t sched_policy, quantum, tellers, queue_len, queue_cap, busy_tellers;
    _Atomic int64_t tick_us;
    _Atomic uint64_t jobs_submitted, jobs_done, jobs_rejected, preemptions;
    _Atomic int64_t wait_ns, turnaround_ns, response_ns;
    _Atomic uint64_t tier_done[4];
    _Atomic int64_t tier_wait_ns[4];
    _Atomic int32_t teller_op[MAX_TELLERS];
    _Atomic uint64_t teller_job[MAX_TELLERS];
    /* deadlock */
    _Atomic int32_t dl_policy;
    _Atomic uint64_t deadlocks, victims, lock_contended;
    /* memory */
    _Atomic int32_t heap_policy;
    _Atomic uint64_t heap_cap, heap_used, heap_free_blocks, heap_largest_free, heap_allocs;
    /* paging */
    _Atomic int32_t repl_policy, frames;
    _Atomic uint64_t page_hits, page_faults, evictions;
    /* WAL */
    _Atomic uint64_t wal_commits, wal_flushes;
    /* audit (written by the auditor) */
    _Atomic int64_t ledger_expected, ledger_actual, last_audit;
    _Atomic uint64_t accounts, audits, audit_violations;
    _Atomic int32_t audit_ok, race_mode;
    _Atomic int32_t sessions;
    _Atomic int32_t bank_month;
} shm_stats_t;

/* shared memory helpers (shm.c) */
void shm_name_for(const char *data_dir, char *out, size_t n);
shm_stats_t *shm_create(const char *data_dir);
shm_stats_t *shm_attach(const char *data_dir, bool writable);
void shm_unlink_for(const char *data_dir);

/* engine.c */
int engine_main(const server_config_t *cfg, shm_stats_t *shm);

/* auditor.c */
int auditor_main(const server_config_t *cfg, shm_stats_t *shm);

/* the request kinds the engine schedules */
typedef enum {
    OP_REGISTER, OP_LOGIN, OP_BALANCE, OP_DEPOSIT, OP_WITHDRAW, OP_TRANSFER, OP_STATEMENT,
    OP_FD_QUOTE, OP_FD_OPEN, OP_FD_LIST, OP_FD_CLOSE,
    OP_LOAN_QUOTE, OP_LOAN_APPLY, OP_LOAN_LIST, OP_LOAN_DRAW, OP_LOAN_REPAY,
    OP_INS_QUOTE, OP_INS_BUY, OP_INS_LIST,
    OP_MONTH_END, OP_SYNTHETIC,
    OP_COUNT
} op_kind_t;

typedef struct {
    const char *name;
    int burst;      /* CPU units */
    int min_fields; /* including the command name */
    bool needs_auth;
    char glyph;     /* Gantt chart letter */
} op_info_t;

extern const op_info_t OP_INFO[OP_COUNT];
int op_lookup(const char *name);

#endif
