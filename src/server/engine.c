/* engine.c - the plutus-engine process.
 *
 * Threads:
 *   main        poll()s the customer socket, the admin socket and a self-pipe
 *               that the SIGTERM handler writes to
 *   session x N one per connected client (bounded by a semaphore); parses a
 *               request and submits it as a job, then sleeps until it completes
 *   teller  x T the "CPUs": pop the next job chosen by the scheduling policy,
 *               run it unit by unit (re-queueing it on preemption), then run
 *               its commit phase against the bank
 *   admin   x M admin console sessions (not scheduled: "kernel mode")
 *   timer       heartbeat, stats publishing, month-end batch jobs, checkpoints
 *   + the WAL flusher and the deadlock detector owned by lower layers
 *
 * The ready queue is a bounded buffer: `slots` counts jobs admitted to the
 * system (queued or running) and `items` counts queued jobs. A preempted job
 * keeps its slot, so re-queueing it never blocks. If it had to wait for a free
 * slot, every teller could block re-queueing into a full queue that nobody
 * drains: a deadlock inside our own scheduler. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../common/proto.h"
#include "../common/sync.h"
#include "engine.h"
#include "server.h"

#define GANTT_RING 8192

typedef struct request {
    job_t job;
    int op;
    char line[PROTO_MAX_LINE];
    char *f[PROTO_MAX_FIELDS];
    int nf;
    uint64_t acct;       /* authenticated account, 0 if none */
    uint64_t login_acct; /* set by a successful LOGIN */
    bool internal;       /* nobody waits: the teller frees it */
    sbuf_t out;
    bstatus_t st;
    char extra[64];
    bool done;
    pthread_mutex_t m;
    pthread_cond_t cv;
} request_t;

typedef struct {
    int teller, op, prio;
    uint64_t job;
    int64_t start, end;
} slice_t;

static struct {
    server_config_t cfg;
    shm_stats_t *shm;
    bank_t bank;
    pthread_mutex_t dm;
    runq_t rq;
    csem_t slots, items;
    int ntellers;
    pthread_t tellers[MAX_TELLERS];
    _Atomic int64_t tick_us;
    _Atomic bool stopping;
    _Atomic uint64_t job_seq;
    pthread_mutex_t gm;
    slice_t ring[GANTT_RING];
    uint64_t ring_n;
    _Atomic uint64_t op_done[OP_COUNT];
    _Atomic int64_t op_wait[OP_COUNT], op_turn[OP_COUNT];
    csem_t session_slots;
    int stop_pipe[2];
    int last_month;
    int64_t last_checkpoint;
} E;

/* ------------------------------------------------------------------ requests */

static request_t *request_new(int op, const char *line)
{
    request_t *r = pcalloc(sizeof *r); /* from the custom heap */
    if (!r)
        return NULL;
    r->op = op;
    snprintf(r->line, sizeof r->line, "%s", line);
    r->nf = proto_split(r->line, r->f, PROTO_MAX_FIELDS);
    sb_init(&r->out);
    pthread_mutex_init(&r->m, NULL);
    pthread_cond_init(&r->cv, NULL);
    return r;
}

static void request_free(request_t *r)
{
    sb_free(&r->out);
    pthread_mutex_destroy(&r->m);
    pthread_cond_destroy(&r->cv);
    pfree(r);
}

static bool submit(request_t *r, int priority, int burst, int64_t timeout_ns)
{
    if (!csem_timedwait(&E.slots, timeout_ns)) {
        atomic_fetch_add(&E.shm->jobs_rejected, 1);
        return false;
    }
    job_init(&r->job, atomic_fetch_add(&E.job_seq, 1) + 1, r->op, priority, burst);
    r->job.ctx = r;
    pthread_mutex_lock(&E.dm);
    rq_push(&E.rq, &r->job, now_ns());
    E.shm->queue_len = E.rq.count;
    pthread_mutex_unlock(&E.dm);
    atomic_fetch_add(&E.shm->jobs_submitted, 1);
    csem_post(&E.items);
    return true;
}

static void wait_done(request_t *r)
{
    pthread_mutex_lock(&r->m);
    while (!r->done)
        pthread_cond_wait(&r->cv, &r->m);
    pthread_mutex_unlock(&r->m);
}

static bool wait_done_for(request_t *r, int64_t ns)
{
    struct timespec dl;
    deadline_after(&dl, ns);
    pthread_mutex_lock(&r->m);
    while (!r->done)
        if (pthread_cond_timedwait(&r->cv, &r->m, &dl) == ETIMEDOUT)
            break;
    bool d = r->done;
    pthread_mutex_unlock(&r->m);
    return d;
}

/* ------------------------------------------------------------------ execution */

static bool u64_arg(const char *s, uint64_t *out)
{
    int64_t v;
    if (!parse_i64(s, &v) || v <= 0)
        return false;
    *out = (uint64_t)v;
    return true;
}

#define BAD(msg)                         \
    do {                                 \
        sb_printf(&r->out, "%s\n", msg); \
        r->st = B_BADREQ;                \
        return;                          \
    } while (0)

static void execute(request_t *r)
{
    bank_t *b = &E.bank;
    char **f = r->f;
    int64_t amt = 0, i64 = 0;
    int i1 = 0, i2 = 0, i3 = 0;
    uint64_t u = 0;
    loan_type_t lt;
    ins_kind_t ik;
    switch (r->op) {
    case OP_REGISTER: {
        int64_t phone;
        if (!parse_int(f[3], &i1) || !parse_i64(f[4], &phone) || !parse_money(f[6], &amt))
            BAD("Usage: REGISTER name city age phone password deposit");
        uint64_t acct = 0;
        r->st = bank_register(b, f[1], f[2], i1, phone, f[5], amt, &r->out, &acct);
        if (r->st == B_OK)
            snprintf(r->extra, sizeof r->extra, "%llu", (unsigned long long)acct);
        return;
    }
    case OP_LOGIN:
        if (!u64_arg(f[1], &u))
            BAD("Usage: LOGIN account password");
        r->st = bank_login(b, u, f[2], &r->out);
        if (r->st == B_OK) {
            r->login_acct = u;
            snprintf(r->extra, sizeof r->extra, "%llu", (unsigned long long)u);
        }
        return;
    case OP_BALANCE:
        r->st = bank_balance(b, r->acct, &r->out);
        return;
    case OP_DEPOSIT:
    case OP_WITHDRAW:
        if (!parse_money(f[1], &amt))
            BAD("Amount must look like 1500 or 1500.50");
        r->st = r->op == OP_DEPOSIT ? bank_deposit(b, r->acct, amt, &r->out)
                                    : bank_withdraw(b, r->acct, amt, &r->out);
        return;
    case OP_TRANSFER:
        if (!u64_arg(f[1], &u) || !parse_money(f[2], &amt))
            BAD("Usage: TRANSFER to_account amount");
        r->st = bank_transfer(b, r->acct, u, amt, &r->out);
        return;
    case OP_STATEMENT:
        if (r->nf < 2 || !parse_int(f[1], &i1))
            i1 = 15;
        r->st = bank_statement(b, r->acct, i1, &r->out);
        return;
    case OP_FD_QUOTE:
    case OP_FD_OPEN:
        if (!parse_money(f[1], &amt) || !parse_int(f[2], &i1) || !parse_int(f[3], &i2))
            BAD("Usage: FD_OPEN amount months senior(0/1)");
        r->st = r->op == OP_FD_QUOTE ? bank_fd_quote(amt, i1, i2 == 1, &r->out)
                                     : bank_fd_open(b, r->acct, amt, i1, i2 == 1, &r->out);
        return;
    case OP_FD_LIST:
        r->st = bank_fd_list(b, r->acct, &r->out);
        return;
    case OP_FD_CLOSE:
        if (!u64_arg(f[1], &u))
            BAD("Usage: FD_CLOSE fd_number");
        r->st = bank_fd_close(b, r->acct, u, &r->out);
        return;
    case OP_LOAN_QUOTE:
    case OP_LOAN_APPLY:
        if (!parse_int(f[1], &i1) || !loan_type_parse(f[2], &lt) || !parse_int(f[3], &i2) ||
            !parse_int(f[4], &i3) || !parse_money(f[5], &amt))
            BAD("Usage: LOAN_APPLY cibil home|personal|student|business lakhs tenure_months income");
        r->st = r->op == OP_LOAN_QUOTE ? bank_loan_quote(i1, lt, i2, i3, amt, &r->out)
                                       : bank_loan_apply(b, r->acct, i1, lt, i2, i3, amt, &r->out);
        return;
    case OP_LOAN_LIST:
        r->st = bank_loan_list(b, r->acct, &r->out);
        return;
    case OP_LOAN_DRAW:
    case OP_LOAN_REPAY:
        if (!u64_arg(f[1], &u) || !parse_money(f[2], &amt))
            BAD("Usage: LOAN_DRAW loan_number amount");
        r->st = r->op == OP_LOAN_DRAW ? bank_loan_draw(b, r->acct, u, amt, &r->out)
                                      : bank_loan_repay(b, r->acct, u, amt, &r->out);
        return;
    case OP_INS_QUOTE:
    case OP_INS_BUY: {
        int base = r->op == OP_INS_QUOTE ? 2 : 4;
        if (!ins_kind_parse(f[1], &ik) || !parse_int(f[base], &i1) || !parse_int(f[base + 2], &i3))
            BAD("Usage: INS_BUY life|health|car holder beneficiary a1 a2 a3");
        bool ok = ik == INS_CAR ? parse_money(f[base + 1], &i64) : parse_i64(f[base + 1], &i64);
        if (!ok)
            BAD("Bad insurance parameters.");
        r->st = r->op == OP_INS_QUOTE ? bank_ins_quote(ik, i1, i64, i3, &r->out)
                                      : bank_ins_buy(b, r->acct, ik, f[2], f[3], i1, i64, i3, &r->out);
        return;
    }
    case OP_INS_LIST:
        r->st = bank_ins_list(b, r->acct, &r->out);
        return;
    case OP_MONTH_END:
        r->st = bank_month_end(b, &r->out);
        LOGI("%s", r->out.buf);
        return;
    case OP_SYNTHETIC:
        r->st = B_OK;
        return;
    default:
        BAD("unknown operation");
    }
}

/* ------------------------------------------------------------------ tellers */

static void trace(int teller, const job_t *j, int64_t start, int64_t end)
{
    pthread_mutex_lock(&E.gm);
    E.ring[E.ring_n % GANTT_RING] = (slice_t){teller, j->kind, j->priority, j->id, start, end};
    E.ring_n++;
    pthread_mutex_unlock(&E.gm);
}

static void *teller_main(void *arg)
{
    int id = (int)(intptr_t)arg;
    char name[32];
    snprintf(name, sizeof name, "teller-%d", id);
    pthread_setname_np(name);
    E.shm->teller_op[id] = -1;
    for (;;) {
        csem_wait(&E.items);
        pthread_mutex_lock(&E.dm);
        job_t *j = rq_pop(&E.rq, now_ns());
        E.shm->queue_len = E.rq.count;
        pthread_mutex_unlock(&E.dm);
        if (!j) {
            if (atomic_load(&E.stopping))
                break;
            continue;
        }
        request_t *r = j->ctx;
        atomic_fetch_add(&E.shm->busy_tellers, 1);
        E.shm->teller_op[id] = j->kind;
        E.shm->teller_job[id] = j->id;
        int64_t start = now_ns();
        int ran = 0;
        bool preempted = false;
        /* CPU phase: one unit per tick, with a preemption check after each */
        while (j->remaining > 0) {
            sleep_us(atomic_load(&E.tick_us));
            j->remaining--;
            ran++;
            if (j->remaining == 0)
                break;
            pthread_mutex_lock(&E.dm);
            int64_t t = now_ns();
            if (rq_should_preempt(&E.rq, j, ran, t)) {
                rq_push(&E.rq, j, t);
                E.shm->queue_len = E.rq.count;
                preempted = true;
            }
            pthread_mutex_unlock(&E.dm);
            if (preempted)
                break;
        }
        if (preempted) {
            trace(id, j, start, now_ns());
            E.shm->teller_op[id] = -1;
            atomic_fetch_sub(&E.shm->busy_tellers, 1);
            atomic_fetch_add(&E.shm->preemptions, 1);
            csem_post(&E.items); /* it is queued again; its slot is kept */
            continue;
        }
        /* commit phase: locks, WAL, buffer pool */
        execute(r);
        int64_t end = now_ns();
        trace(id, j, start, end);
        j->finish = end;
        int64_t wait = j->waited, turn = end - j->arrival, resp = j->first_run - j->arrival;
        atomic_fetch_add(&E.op_done[j->kind], 1);
        atomic_fetch_add(&E.op_wait[j->kind], wait);
        atomic_fetch_add(&E.op_turn[j->kind], turn);
        atomic_fetch_add(&E.shm->jobs_done, 1);
        atomic_fetch_add(&E.shm->wait_ns, wait);
        atomic_fetch_add(&E.shm->turnaround_ns, turn);
        atomic_fetch_add(&E.shm->response_ns, resp);
        int tier = j->priority < 4 ? j->priority : 3;
        atomic_fetch_add(&E.shm->tier_done[tier], 1);
        atomic_fetch_add(&E.shm->tier_wait_ns[tier], wait);
        E.shm->teller_op[id] = -1;
        atomic_fetch_sub(&E.shm->busy_tellers, 1);
        if (r->internal) {
            request_free(r);
        } else {
            pthread_mutex_lock(&r->m);
            r->done = true;
            pthread_cond_signal(&r->cv);
            pthread_mutex_unlock(&r->m);
        }
        csem_post(&E.slots);
    }
    return NULL;
}

/* ------------------------------------------------------------------ responses */

static bool send_response(int fd, const sbuf_t *body, const char *status)
{
    sbuf_t o;
    sb_init(&o);
    const char *p = body ? body->buf : "";
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        sb_printf(&o, "| %.*s\n", (int)n, p);
        p = nl ? nl + 1 : NULL;
    }
    sb_printf(&o, "%s\n", status);
    bool ok = conn_write_all(fd, o.buf, o.len);
    sb_free(&o);
    return ok;
}

static bool send_simple(int fd, const char *status, const char *msg)
{
    sbuf_t b;
    sb_init(&b);
    if (msg)
        sb_printf(&b, "%s\n", msg);
    bool ok = send_response(fd, &b, status);
    sb_free(&b);
    return ok;
}

/* ------------------------------------------------------------------ customer sessions */

static void *session_main(void *arg)
{
    int fd = (int)(intptr_t)arg;
    pthread_setname_np("session");
    atomic_fetch_add(&E.shm->sessions, 1);
    conn_t *c = pmalloc(sizeof *c);
    if (!c) {
        close(fd);
        atomic_fetch_sub(&E.shm->sessions, 1);
        csem_post(&E.session_slots);
        return NULL;
    }
    conn_init(c, fd);
    uint64_t acct = 0;
    char line[PROTO_MAX_LINE];
    for (;;) {
        int n = conn_read_line(c, line, sizeof line);
        if (n == -2)
            continue;
        if (n <= 0)
            break;
        char copy[PROTO_MAX_LINE];
        snprintf(copy, sizeof copy, "%s", line);
        char *f[PROTO_MAX_FIELDS];
        int nf = proto_split(copy, f, PROTO_MAX_FIELDS);
        if (!strcmp(f[0], "PING")) {
            send_simple(fd, "OK\tPONG", NULL);
            continue;
        }
        if (!strcmp(f[0], "QUIT")) {
            send_simple(fd, "OK\tBYE", "Do visit us again..");
            break;
        }
        if (!strcmp(f[0], "LOGOUT")) {
            acct = 0;
            send_simple(fd, "OK", "Logged out.");
            continue;
        }
        if (!strcmp(f[0], "WHOAMI")) {
            char st[64];
            snprintf(st, sizeof st, "OK\t%llu", (unsigned long long)acct);
            send_simple(fd, st, NULL);
            continue;
        }
        int op = op_lookup(f[0]);
        if (op < 0 || op == OP_MONTH_END || op == OP_SYNTHETIC) {
            send_simple(fd, "ERR\tBAD_REQUEST", "Unknown command.");
            continue;
        }
        if (nf < OP_INFO[op].min_fields) {
            send_simple(fd, "ERR\tBAD_REQUEST", "Missing arguments.");
            continue;
        }
        if (OP_INFO[op].needs_auth && !acct) {
            send_simple(fd, "ERR\tAUTH", "Please login first.");
            continue;
        }
        request_t *r = request_new(op, line);
        if (!r) {
            send_simple(fd, "ERR\tBUSY", "Server memory exhausted, try again.");
            continue;
        }
        r->acct = acct;
        int prio = acct ? bank_account_tier(&E.bank, acct) : TIER_REGULAR;
        if (prio < 0)
            prio = TIER_REGULAR;
        if (!submit(r, prio, OP_INFO[op].burst, 5 * 1000000000LL)) {
            request_free(r);
            send_simple(fd, "ERR\tBUSY", "All tellers are busy and the queue is full. Try again.");
            continue;
        }
        wait_done(r);
        if (r->login_acct)
            acct = r->login_acct;
        char st[128];
        if (r->st == B_OK && r->extra[0])
            snprintf(st, sizeof st, "OK\t%s", r->extra);
        else if (r->st == B_OK)
            snprintf(st, sizeof st, "OK");
        else if (r->st == B_PENDING)
            snprintf(st, sizeof st, "OK\tPENDING");
        else
            snprintf(st, sizeof st, "ERR\t%s", bstatus_name(r->st));
        bool ok = send_response(fd, &r->out, st);
        request_free(r);
        if (!ok)
            break;
    }
    pfree(c);
    close(fd);
    atomic_fetch_sub(&E.shm->sessions, 1);
    csem_post(&E.session_slots);
    return NULL;
}

/* ------------------------------------------------------------------ admin */

static void render_gantt(int64_t window_ms, sbuf_t *out)
{
    enum { COLS = 96 };
    int64_t now = now_ns(), from = now - window_ms * 1000000LL;
    double bucket = (double)(now - from) / COLS;
    static char grid[MAX_TELLERS][COLS + 1];
    for (int t = 0; t < E.ntellers; t++) {
        memset(grid[t], '.', COLS);
        grid[t][COLS] = '\0';
    }
    pthread_mutex_lock(&E.gm);
    uint64_t n = E.ring_n < GANTT_RING ? E.ring_n : GANTT_RING;
    for (uint64_t k = 0; k < n; k++) {
        slice_t *s = &E.ring[(E.ring_n - 1 - k) % GANTT_RING];
        if (s->end < from)
            continue;
        int c0 = (int)((double)(s->start - from) / bucket), c1 = (int)((double)(s->end - from) / bucket);
        if (c0 < 0)
            c0 = 0;
        if (c1 >= COLS)
            c1 = COLS - 1;
        /* synthetic jobs show their priority, so priority scheduling is visible */
        char g = s->op == OP_SYNTHETIC ? (char)('0' + (s->prio & 3)) : OP_INFO[s->op].glyph;
        for (int c = c0; c <= c1 && s->teller < E.ntellers; c++)
            grid[s->teller][c] = g;
    }
    pthread_mutex_unlock(&E.gm);
    pthread_mutex_lock(&E.dm);
    sched_policy_t pol = E.rq.policy;
    int q = E.rq.quantum;
    pthread_mutex_unlock(&E.dm);
    sb_printf(out, "Live Gantt chart: last %lld ms, policy %s (q=%d, tick %lld us), 1 col = %.1f ms\n",
              (long long)window_ms, sched_policy_name(pol), q, (long long)atomic_load(&E.tick_us),
              bucket / 1e6);
    for (int t = 0; t < E.ntellers; t++)
        sb_printf(out, "  teller %-2d |%s|\n", t, grid[t]);
    sb_printf(out, "  legend:");
    for (int i = 0; i < OP_COUNT; i++)
        if (i != OP_FD_QUOTE && i != OP_INS_QUOTE && i != OP_SYNTHETIC)
            sb_printf(out, " %c=%s", OP_INFO[i].glyph, OP_INFO[i].name);
    sb_printf(out, " 0-3=synthetic job of that priority .=idle\n\n  %-11s %8s %12s %14s\n", "operation", "done", "avg wait ms",
              "avg turnaround");
    for (int i = 0; i < OP_COUNT; i++) {
        uint64_t d = atomic_load(&E.op_done[i]);
        if (!d)
            continue;
        sb_printf(out, "  %-11s %8llu %12.2f %14.2f\n", OP_INFO[i].name, (unsigned long long)d,
                  (double)atomic_load(&E.op_wait[i]) / (double)d / 1e6,
                  (double)atomic_load(&E.op_turn[i]) / (double)d / 1e6);
    }
    sb_printf(out, "\n  average wait by priority:");
    for (int t = 0; t < 4; t++) {
        uint64_t d = atomic_load(&E.shm->tier_done[t]);
        sb_printf(out, "  %s %.2f ms (%llu jobs)", tier_name(t),
                  d ? (double)atomic_load(&E.shm->tier_wait_ns[t]) / (double)d / 1e6 : 0.0,
                  (unsigned long long)d);
    }
    sb_printf(out, "\n");
}

static void render_queue(sbuf_t *out)
{
    pthread_mutex_lock(&E.dm);
    int64_t now = now_ns();
    sb_printf(out, "Ready queue: %d jobs, policy %s, quantum %d, aging %lld ms, boost %lld ms\n", E.rq.count,
              sched_policy_name(E.rq.policy), E.rq.quantum, (long long)(E.rq.aging_interval / 1000000),
              (long long)(E.rq.boost_interval / 1000000));
    sb_printf(out, "  dispatches %llu | preemptions %llu | MLFQ boosts %llu\n",
              (unsigned long long)E.rq.dispatches, (unsigned long long)E.rq.preemptions,
              (unsigned long long)E.rq.boosts);
    int shown = 0;
    for (int l = 0; l < MLFQ_LEVELS; l++)
        for (job_t *j = E.rq.head[l]; j && shown < 50; j = j->next, shown++)
            sb_printf(out, "  job %-6llu %-10s prio %d (eff %d) level %d  remaining %2d/%-2d  waiting %.1f ms\n",
                      (unsigned long long)j->id, OP_INFO[j->kind].name, j->priority,
                      rq_effective_priority(&E.rq, j, now), j->level, j->remaining, j->burst,
                      (double)(now - j->enqueued) / 1e6);
    pthread_mutex_unlock(&E.dm);
    sb_printf(out, "Tellers:\n");
    for (int t = 0; t < E.ntellers; t++) {
        int op = E.shm->teller_op[t];
        if (op >= 0)
            sb_printf(out, "  teller %d: job %llu (%s)\n", t, (unsigned long long)E.shm->teller_job[t],
                      OP_INFO[op].name);
        else
            sb_printf(out, "  teller %d: idle\n", t);
    }
}

static void render_mem(sbuf_t *out)
{
    heap_stats_t s;
    heap_stats(g_heap, &s);
    char map[97];
    heap_map(g_heap, map, 96);
    sb_printf(out, "Custom heap (%s): %zu KiB managed\n", alloc_policy_name(s.policy), s.capacity / 1024);
    sb_printf(out, "  used %zu B in %llu blocks (requested %zu B) | free %zu B in %llu blocks | largest free %zu B\n",
              s.used_bytes, (unsigned long long)s.used_blocks, s.requested_bytes, s.free_bytes,
              (unsigned long long)s.free_blocks, s.largest_free);
    sb_printf(out, "  allocs %llu | frees %llu | failures %llu | splits %llu | coalesces %llu | search steps %llu\n",
              (unsigned long long)s.allocs, (unsigned long long)s.frees, (unsigned long long)s.failures,
              (unsigned long long)s.splits, (unsigned long long)s.coalesces, (unsigned long long)s.search_steps);
    sb_printf(out, "  external fragmentation %.1f%% | internal fragmentation %.1f%%\n", s.external_frag * 100,
              s.internal_frag * 100);
    sb_printf(out, "  map |%s|  (# used, + partly used, . free)\n", map);
    char why[128];
    sb_printf(out, "  integrity check: %s\n", heap_check(g_heap, why, sizeof why) ? "OK" : why);
}

static void render_stats(sbuf_t *out)
{
    shm_stats_t *s = E.shm;
    uint64_t done = s->jobs_done;
    int64_t actual, expected;
    uint64_t naccts;
    bool ok = bank_audit(&E.bank, &actual, &expected, &naccts);
    char m1[48], m2[48];
    sb_printf(out, "Plutus engine pid %d | %llu accounts | bank month %d | sessions %d\n", getpid(),
              (unsigned long long)naccts, bank_month(&E.bank), (int)s->sessions);
    sb_printf(out, "Scheduler %s q=%d tick=%lldus | tellers %d (busy %d) | queue %d/%d\n",
              sched_policy_name((sched_policy_t)s->sched_policy), (int)s->quantum, (long long)s->tick_us,
              (int)s->tellers, (int)s->busy_tellers, (int)s->queue_len, (int)s->queue_cap);
    sb_printf(out, "  jobs submitted %llu | done %llu | rejected (queue full) %llu | preemptions %llu\n",
              (unsigned long long)s->jobs_submitted, (unsigned long long)done,
              (unsigned long long)s->jobs_rejected, (unsigned long long)s->preemptions);
    if (done)
        sb_printf(out, "  avg wait %.2f ms | avg turnaround %.2f ms | avg response %.2f ms\n",
                  (double)s->wait_ns / (double)done / 1e6, (double)s->turnaround_ns / (double)done / 1e6,
                  (double)s->response_ns / (double)done / 1e6);
    lm_stats_t ls;
    lm_get_stats(&E.bank.lm, &ls);
    sb_printf(out, "Deadlock policy %s | deadlocks %llu | victims %llu | contended locks %llu | retries %llu\n",
              dl_policy_name(ls.policy), (unsigned long long)ls.deadlocks_found, (unsigned long long)ls.victims,
              (unsigned long long)ls.contended, (unsigned long long)atomic_load(&E.bank.deadlock_retries));
    rw_mode_t mode;
    uint64_t reads, writes;
    int64_t max_rw, max_ww;
    rw_get_stats(&E.bank.idx_lock, &mode, &reads, &writes, &max_rw, &max_ww);
    sb_printf(out, "Index lock %s | reads %llu writes %llu | max read wait %.2f ms | max write wait %.2f ms\n",
              rw_mode_name(mode), (unsigned long long)reads, (unsigned long long)writes, (double)max_rw / 1e6,
              (double)max_ww / 1e6);
    sb_printf(out, "Race mode %s | ledger %s | actual %s | %s\n", atomic_load(&E.bank.race_mode) ? "ON" : "off",
              fmt_money(expected, m1, sizeof m1), fmt_money(actual, m2, sizeof m2),
              ok ? "CONSISTENT" : "MISMATCH (lost updates!)");
    sbuf_t tmp;
    sb_init(&tmp);
    render_mem(&tmp);
    pager_render(&E.bank.db.pager, &tmp);
    char *cut = strstr(tmp.buf, "  frame"); /* omit the per-frame table here */
    if (cut)
        *cut = '\0';
    sb_printf(out, "%s", tmp.buf);
    sb_free(&tmp);
    db_render(&E.bank.db, out);
}

/* live dining-philosophers deadlock through the real scheduler and bank */
static uint64_t g_demo[8];
static int g_ndemo;

static void scenario_deadlock(int n, sbuf_t *out)
{
    if (n < 2)
        n = 2;
    if (n > E.ntellers)
        n = E.ntellers;
    if (n > 8)
        n = 8;
    while (g_ndemo < n) {
        sbuf_t tmp;
        sb_init(&tmp);
        char name[40];
        snprintf(name, sizeof name, "Demo Philosopher %c", 'A' + g_ndemo);
        bank_register(&E.bank, name, "Demo", 30, 9000000000LL + g_ndemo, "demo1234", 100000 * 100, &tmp,
                      &g_demo[g_ndemo]);
        sb_free(&tmp);
        g_ndemo++;
    }
    lockmgr_t *lm = &E.bank.lm;
    pthread_mutex_lock(&lm->m);
    int64_t old_hold = lm->hold_ns;
    dl_policy_t pol = lm->policy;
    lm->hold_ns = 40 * 1000000LL; /* every transfer pauses while holding its first lock */
    uint64_t d0 = lm->deadlocks_found, v0 = lm->victims;
    pthread_mutex_unlock(&lm->m);
    sb_printf(out, "Dining philosophers with %d tellers: transfer i moves Rs 1 from account i to i+1 (mod %d).\n", n, n);
    sb_printf(out, "Each locks its source, holds it for 40 ms, then locks its destination. Policy: %s\n\n",
              dl_policy_name(pol));
    request_t *r[8];
    int64_t t0 = now_ns();
    for (int i = 0; i < n; i++) {
        char line[128];
        snprintf(line, sizeof line, "TRANSFER\t%llu\t1.00", (unsigned long long)g_demo[(i + 1) % n]);
        r[i] = request_new(OP_TRANSFER, line);
        r[i]->acct = g_demo[i];
        submit(r[i], TIER_VIP, 1, 5 * 1000000000LL);
    }
    int done = 0;
    for (int i = 0; i < n; i++)
        done += wait_done_for(r[i], (i == 0 ? 2500 : 50) * 1000000LL);
    if (done < n) {
        sb_printf(out, "DEADLOCK: after %.0f ms only %d of %d transfers finished; %d tellers are stuck in a\n"
                       "circular wait. The wait-for graph right now:\n\n",
                  (double)(now_ns() - t0) / 1e6, done, n, n - done);
        lm_render(lm, out);
        sb_printf(out, "\nRescuing: switching the policy to 'detect' so the detector aborts a victim...\n");
        lm_set_policy(lm, DL_DETECT);
        for (int i = 0; i < n; i++)
            wait_done_for(r[i], 5 * 1000000000LL);
        lm_set_policy(lm, pol);
    }
    int ok = 0;
    for (int i = 0; i < n; i++) {
        if (r[i]->done) {
            ok += r[i]->st == B_OK;
            sb_printf(out, "  transfer %d (%llu -> %llu): %s\n", i, (unsigned long long)g_demo[i],
                      (unsigned long long)g_demo[(i + 1) % n], bstatus_name(r[i]->st));
            request_free(r[i]);
        } else {
            /* a teller still owns it and will signal it later, so it is not freed */
            sb_printf(out, "  transfer %d: still blocked (left running)\n", i);
        }
    }
    pthread_mutex_lock(&lm->m);
    lm->hold_ns = old_hold;
    uint64_t d1 = lm->deadlocks_found, v1 = lm->victims;
    pthread_mutex_unlock(&lm->m);
    sb_printf(out, "\n%d/%d completed in %.0f ms | cycles detected %llu | victims aborted and retried %llu\n", ok, n,
              (double)(now_ns() - t0) / 1e6, (unsigned long long)(d1 - d0), (unsigned long long)(v1 - v0));
    if (pol == DL_ORDERED)
        sb_printf(out, "Ordered locking prevents the cycle: everyone locks the lower account number first.\n");
    if (pol == DL_TRYLOCK)
        sb_printf(out, "Try-lock prevents hold-and-wait: a transfer that cannot get its second lock lets go of the first.\n");
}

static void scenario_flood(int n, sbuf_t *out)
{
    if (n <= 0 || n > 2000)
        n = 200;
    int ok = 0, rejected = 0;
    for (int i = 0; i < n; i++) {
        request_t *r = request_new(OP_SYNTHETIC, "SYNTHETIC");
        if (!r)
            break;
        r->internal = true;
        int prio = (int)rand_below(4), burst = 1 + (int)rand_below(12);
        if (submit(r, prio, burst, 2 * 1000000LL))
            ok++;
        else {
            request_free(r);
            rejected++;
        }
    }
    sb_printf(out, "Submitted %d synthetic jobs (random burst 1-12 units, random priority 0-3); %d rejected by the "
                   "bounded queue.\n",
              ok, rejected);
    sb_printf(out, "Run 'gantt' to watch the tellers chew through them under the current policy.\n");
}

static void admin_set(char **f, int nf, sbuf_t *out, char *status, size_t scap)
{
    if (nf < 3) {
        snprintf(status, scap, "ERR\tBAD_REQUEST");
        sb_printf(out, "usage: SET <knob> <value>\n");
        return;
    }
    const char *k = f[1], *v = f[2];
    int iv = 0;
    bool ok = true;
    if (!strcmp(k, "sched")) {
        sched_policy_t p;
        if ((ok = sched_policy_parse(v, &p))) {
            pthread_mutex_lock(&E.dm);
            rq_set_policy(&E.rq, p);
            pthread_mutex_unlock(&E.dm);
            E.shm->sched_policy = p;
        }
    } else if (!strcmp(k, "quantum") && (ok = parse_int(v, &iv) && iv > 0)) {
        pthread_mutex_lock(&E.dm);
        E.rq.quantum = iv;
        pthread_mutex_unlock(&E.dm);
        E.shm->quantum = iv;
    } else if (!strcmp(k, "tick") && (ok = parse_int(v, &iv) && iv >= 0)) {
        atomic_store(&E.tick_us, iv);
        E.shm->tick_us = iv;
    } else if (!strcmp(k, "aging") && (ok = parse_int(v, &iv) && iv >= 0)) {
        pthread_mutex_lock(&E.dm);
        E.rq.aging_interval = (int64_t)iv * 1000000LL;
        pthread_mutex_unlock(&E.dm);
    } else if (!strcmp(k, "boost") && (ok = parse_int(v, &iv) && iv >= 0)) {
        pthread_mutex_lock(&E.dm);
        E.rq.boost_interval = (int64_t)iv * 1000000LL;
        pthread_mutex_unlock(&E.dm);
    } else if (!strcmp(k, "deadlock")) {
        dl_policy_t p;
        if ((ok = dl_policy_parse(v, &p))) {
            lm_set_policy(&E.bank.lm, p);
            E.shm->dl_policy = p;
        }
    } else if (!strcmp(k, "hold") && (ok = parse_int(v, &iv) && iv >= 0)) {
        pthread_mutex_lock(&E.bank.lm.m);
        E.bank.lm.hold_ns = (int64_t)iv * 1000;
        pthread_mutex_unlock(&E.bank.lm.m);
    } else if (!strcmp(k, "pager")) {
        repl_policy_t p;
        if ((ok = repl_policy_parse(v, &p))) {
            pager_set_policy(&E.bank.db.pager, p);
            E.shm->repl_policy = p;
        }
    } else if (!strcmp(k, "rwmode")) {
        rw_mode_t m;
        if ((ok = rw_mode_parse(v, &m)))
            rw_set_mode(&E.bank.idx_lock, m);
    } else if (!strcmp(k, "race")) {
        bool on = !strcmp(v, "on");
        ok = on || !strcmp(v, "off");
        if (ok) {
            atomic_store(&E.bank.race_mode, on);
            E.shm->race_mode = on;
            LOGW("race mode %s: deposits and withdrawals %s", on ? "ON" : "off",
                 on ? "skip their account lock (lost updates expected)" : "are locked again");
        }
    } else if (!strcmp(k, "racedelay") && (ok = parse_int(v, &iv) && iv >= 0)) {
        atomic_store(&E.bank.race_delay_us, iv);
    } else {
        ok = false;
    }
    if (!ok) {
        snprintf(status, scap, "ERR\tBAD_REQUEST");
        sb_printf(out, "knobs: sched fcfs|sjf|srtf|rr|prio|mlfq, quantum N, tick US, aging MS, boost MS,\n"
                       "       deadlock none|detect|ordered|trylock, hold US, pager fifo|lru|clock,\n"
                       "       rwmode reader-pref|writer-pref|fair, race on|off, racedelay US\n");
        return;
    }
    LOGI("admin: set %s = %s", k, v);
    sb_printf(out, "%s = %s\n", k, v);
}

static void admin_command(char *line, sbuf_t *out, char *status, size_t scap)
{
    char *f[PROTO_MAX_FIELDS];
    int nf = proto_split(line, f, PROTO_MAX_FIELDS);
    for (char *p = f[0]; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
    snprintf(status, scap, "OK");
    const char *c = f[0];
    if (!strcmp(c, "PING"))
        snprintf(status, scap, "OK\tPONG");
    else if (!strcmp(c, "STATS"))
        render_stats(out);
    else if (!strcmp(c, "SET"))
        admin_set(f, nf, out, status, scap);
    else if (!strcmp(c, "GANTT")) {
        int ms = 0;
        if (nf < 2 || !parse_int(f[1], &ms) || ms <= 0)
            ms = 2000;
        render_gantt(ms, out);
    } else if (!strcmp(c, "QUEUE"))
        render_queue(out);
    else if (!strcmp(c, "LOCKS") || !strcmp(c, "WAITGRAPH"))
        lm_render(&E.bank.lm, out);
    else if (!strcmp(c, "BANKER")) {
        bk_render(&E.bank.bk, out);
        sb_printf(out, "Deferred disbursements:\n");
        bank_render_pending(&E.bank, out);
    } else if (!strcmp(c, "MEM"))
        render_mem(out);
    else if (!strcmp(c, "PAGER"))
        pager_render(&E.bank.db.pager, out);
    else if (!strcmp(c, "WAL"))
        db_render(&E.bank.db, out);
    else if (!strcmp(c, "AUDIT")) {
        int64_t a, e;
        uint64_t n;
        bool ok = bank_audit(&E.bank, &a, &e, &n);
        snprintf(status, scap, "OK\t%lld\t%lld\t%llu\t%d", (long long)a, (long long)e, (unsigned long long)n, ok);
        char m1[48], m2[48];
        sb_printf(out, "%llu accounts | sum of balances %s | ledger %s | %s\n", (unsigned long long)n,
                  fmt_money(a, m1, sizeof m1), fmt_money(e, m2, sizeof m2), ok ? "CONSISTENT" : "MISMATCH");
    } else if (!strcmp(c, "RECONCILE")) {
        char m[48];
        int64_t diff = bank_reconcile(&E.bank);
        sb_printf(out, "ledger adjusted by %s to match the sum of balances\n", fmt_money(diff, m, sizeof m));
    } else if (!strcmp(c, "ACCOUNTS")) {
        int n = 0;
        if (nf < 2 || !parse_int(f[1], &n))
            n = 25;
        bank_list_accounts(&E.bank, n, out);
    } else if (!strcmp(c, "CHECKPOINT")) {
        int64_t t0 = now_ns();
        int rc = db_checkpoint(&E.bank.db);
        sb_printf(out, "checkpoint %s in %.1f ms\n", rc == 0 ? "done" : "FAILED", (double)(now_ns() - t0) / 1e6);
    } else if (!strcmp(c, "MONTHEND")) {
        request_t *r = request_new(OP_MONTH_END, "MONTH_END");
        r->internal = true;
        if (submit(r, PRIO_BATCH, OP_INFO[OP_MONTH_END].burst, 1000000000LL))
            sb_printf(out, "month-end batch job queued at batch priority\n");
        else
            request_free(r);
    } else if (!strcmp(c, "SCENARIO")) {
        const char *name = nf > 1 ? f[1] : "";
        int arg = 0;
        if (nf > 2)
            parse_int(f[2], &arg);
        if (!strcmp(name, "deadlock") || !strcmp(name, "philosophers-live"))
            scenario_deadlock(arg ? arg : 2, out);
        else if (!strcmp(name, "flood"))
            scenario_flood(arg, out);
        else if (!scenario_offline(name, f + 2, nf - 2, out)) {
            snprintf(status, scap, "ERR\tBAD_REQUEST");
            scenario_list(out);
        }
    } else if (!strcmp(c, "CRASH")) {
        LOGW("admin requested a crash: abort() without checkpoint (recovery demo)");
        abort();
    } else if (!strcmp(c, "SHUTDOWN")) {
        sb_printf(out, "asking the supervisor (pid %d) to shut down\n", getppid());
        kill(getppid(), SIGTERM);
    } else if (!strcmp(c, "HELP")) {
        sb_printf(out, "STATS | SET knob value | GANTT [ms] | QUEUE | LOCKS | BANKER | MEM | PAGER | WAL | AUDIT\n"
                       "RECONCILE | ACCOUNTS [n] | CHECKPOINT | MONTHEND | SCENARIO name [args] | CRASH | SHUTDOWN\n");
        scenario_list(out);
    } else {
        snprintf(status, scap, "ERR\tBAD_REQUEST");
        sb_printf(out, "unknown admin command (try HELP)\n");
    }
}

static void *admin_main(void *arg)
{
    int fd = (int)(intptr_t)arg;
    pthread_setname_np("admin");
    uid_t uid;
    gid_t gid;
    if (getpeereid(fd, &uid, &gid) != 0 || uid != geteuid()) {
        send_simple(fd, "ERR\tAUTH", "admin socket: permission denied");
        close(fd);
        return NULL;
    }
    conn_t *c = malloc(sizeof *c);
    conn_init(c, fd);
    char line[PROTO_MAX_LINE];
    for (;;) {
        int n = conn_read_line(c, line, sizeof line);
        if (n == -2)
            continue;
        if (n <= 0)
            break;
        if (!strcasecmp(line, "QUIT"))
            break;
        sbuf_t out;
        sb_init(&out);
        char status[160];
        admin_command(line, &out, status, sizeof status);
        bool ok = send_response(fd, &out, status);
        sb_free(&out);
        if (!ok)
            break;
    }
    free(c);
    close(fd);
    return NULL;
}

/* ------------------------------------------------------------------ timer */

static void publish_stats(void)
{
    shm_stats_t *s = E.shm;
    heap_stats_t hs;
    heap_stats(g_heap, &hs);
    s->heap_policy = hs.policy;
    s->heap_cap = hs.capacity;
    s->heap_used = hs.used_bytes;
    s->heap_free_blocks = hs.free_blocks;
    s->heap_largest_free = hs.largest_free;
    s->heap_allocs = hs.allocs;
    pager_t *p = &E.bank.db.pager;
    pthread_mutex_lock(&p->m);
    s->page_hits = p->hits;
    s->page_faults = p->misses;
    s->evictions = p->evictions;
    s->repl_policy = p->policy;
    s->frames = p->nframes;
    pthread_mutex_unlock(&p->m);
    wal_t *w = &E.bank.db.wal;
    pthread_mutex_lock(&w->m);
    s->wal_commits = w->commits;
    s->wal_flushes = w->flushes;
    pthread_mutex_unlock(&w->m);
    lm_stats_t ls;
    lm_get_stats(&E.bank.lm, &ls);
    s->deadlocks = ls.deadlocks_found;
    s->victims = ls.victims;
    s->lock_contended = ls.contended;
    s->dl_policy = ls.policy;
    s->bank_month = bank_month(&E.bank);
}

static void *timer_main(void *arg)
{
    (void)arg;
    pthread_setname_np("timer");
    int64_t last_pub = 0;
    while (!atomic_load(&E.stopping)) {
        int64_t now = now_ns();
        E.shm->heartbeat_ns = now;
        if (now - last_pub > 250 * 1000000LL) {
            publish_stats();
            last_pub = now;
        }
        int month = bank_month(&E.bank);
        if (month > E.last_month) {
            E.last_month = month;
            request_t *r = request_new(OP_MONTH_END, "MONTH_END");
            if (r) {
                r->internal = true;
                rw_rdlock(&E.bank.idx_lock);
                uint64_t naccts = E.bank.naccounts;
                rw_rdunlock(&E.bank.idx_lock);
                int burst = 20 + (int)MIN(naccts / 4, 400);
                if (!submit(r, PRIO_BATCH, burst, 1000000000LL))
                    request_free(r);
                else
                    LOGI("bank month %d began: month-end batch job queued (burst %d)", month, burst);
            }
        }
        if (E.cfg.checkpoint_secs > 0 && wall_time() - E.last_checkpoint >= E.cfg.checkpoint_secs) {
            db_checkpoint(&E.bank.db);
            E.last_checkpoint = wall_time();
        }
        sleep_us(100000);
    }
    return NULL;
}

/* ------------------------------------------------------------------ main */

static void on_term(int sig)
{
    (void)sig;
    char c = 1;
    ssize_t w = write(E.stop_pipe[1], &c, 1);
    (void)w;
}

static void spawn(void *(*fn)(void *), void *arg)
{
    pthread_t t;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &a, fn, arg) != 0)
        LOGE("pthread_create failed: %s", strerror(errno));
    pthread_attr_destroy(&a);
}

int engine_main(const server_config_t *cfg, shm_stats_t *shm)
{
    memset(&E, 0, sizeof E);
    E.cfg = *cfg;
    E.shm = shm;
    signal(SIGPIPE, SIG_IGN);
    g_heap = heap_create(cfg->heap_bytes, cfg->alloc);
    if (!g_heap)
        die("cannot create the custom heap");

    int64_t t0 = now_ns();
    if (bank_open(&E.bank, cfg->data_dir, &cfg->bank) != 0)
        die("cannot open the bank in %s: %s", cfg->data_dir, strerror(errno));
    db_recovery_t *rec = &E.bank.db.recovery;
    LOGI("storage ready in %.1f ms (%s; redo %ld txns)", (double)(now_ns() - t0) / 1e6,
         rec->fresh ? "new database" : "existing database", rec->txns);
    E.last_month = bank_month(&E.bank);
    E.last_checkpoint = wall_time();

    pthread_mutex_init(&E.dm, NULL);
    pthread_mutex_init(&E.gm, NULL);
    rq_init(&E.rq, cfg->sched, cfg->quantum);
    E.rq.aging_interval = cfg->aging_ms * 1000000LL;
    E.rq.boost_interval = cfg->boost_ms * 1000000LL;
    csem_init(&E.slots, cfg->queue_cap);
    csem_init(&E.items, 0);
    csem_init(&E.session_slots, cfg->max_sessions);
    atomic_store(&E.tick_us, cfg->tick_us);
    E.ntellers = cfg->tellers;

    shm->sched_policy = cfg->sched;
    shm->quantum = cfg->quantum;
    shm->tick_us = cfg->tick_us;
    shm->tellers = cfg->tellers;
    shm->queue_cap = cfg->queue_cap;
    shm->dl_policy = cfg->bank.deadlock;
    /* the segment outlives engine incarnations: reset the gauges a crash left behind */
    shm->busy_tellers = 0;
    shm->sessions = 0;
    shm->queue_len = 0;
    for (int i = 0; i < MAX_TELLERS; i++)
        shm->teller_op[i] = -1;
    shm->engine_pid = getpid();
    shm->engine_started = wall_time();
    publish_stats();

    for (int i = 0; i < E.ntellers; i++)
        pthread_create(&E.tellers[i], NULL, teller_main, (void *)(intptr_t)i);
    pthread_t timer;
    pthread_create(&timer, NULL, timer_main, NULL);

    int lfd = unix_listen(cfg->sock_path, 0666);
    int afd = unix_listen(cfg->admin_path, 0600);
    if (lfd < 0 || afd < 0)
        die("cannot listen on %s / %s: %s", cfg->sock_path, cfg->admin_path, strerror(errno));
    if (pipe(E.stop_pipe) != 0)
        die("pipe: %s", strerror(errno));
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    shm->engine_ready = 1;
    LOGI("engine ready: %d tellers, %s scheduling (q=%d), %s deadlock handling, %d-frame %s buffer pool, %s heap",
         cfg->tellers, sched_policy_name(cfg->sched), cfg->quantum, dl_policy_name(cfg->bank.deadlock),
         cfg->bank.frames, repl_policy_name(cfg->bank.repl), alloc_policy_name(cfg->alloc));
    LOGI("customers: %s | admin: %s", cfg->sock_path, cfg->admin_path);

    struct pollfd pf[3] = {{lfd, POLLIN, 0}, {afd, POLLIN, 0}, {E.stop_pipe[0], POLLIN, 0}};
    for (;;) {
        int n = poll(pf, 3, -1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (pf[2].revents)
            break;
        if (pf[0].revents & POLLIN) {
            int fd = accept(lfd, NULL, NULL);
            if (fd >= 0) {
                if (!csem_trywait(&E.session_slots)) {
                    send_simple(fd, "ERR\tBUSY", "Too many customers connected. Please try later.");
                    close(fd);
                } else {
                    spawn(session_main, (void *)(intptr_t)fd);
                }
            }
        }
        if (pf[1].revents & POLLIN) {
            int fd = accept(afd, NULL, NULL);
            if (fd >= 0)
                spawn(admin_main, (void *)(intptr_t)fd);
        }
    }

    LOGI("shutting down: draining the queue, then checkpointing");
    shm->engine_ready = 0;
    close(lfd);
    close(afd);
    unlink(cfg->sock_path);
    unlink(cfg->admin_path);
    /* drain: wait until every admitted job has finished */
    for (int i = 0; i < 200 && csem_value(&E.slots) < cfg->queue_cap; i++)
        sleep_us(10000);
    atomic_store(&E.stopping, true);
    for (int i = 0; i < E.ntellers; i++)
        csem_post(&E.items);
    for (int i = 0; i < E.ntellers; i++)
        pthread_join(E.tellers[i], NULL);
    pthread_join(timer, NULL);
    bank_close(&E.bank);
    LOGI("engine stopped cleanly");
    return 0;
}
