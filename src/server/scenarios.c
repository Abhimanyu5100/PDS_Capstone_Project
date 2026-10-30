/* scenarios.c - self-contained OS demonstrations for the admin console.
 *
 * Each one runs real code from the subsystems (the same run queue, pager
 * simulator, heap, Banker's algorithm, rwlock, semaphore and lock manager the
 * bank uses) on a private workload, and prints a report suitable for a viva. */
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/sync.h"
#include "../deadlock/banker.h"
#include "../deadlock/lockmgr.h"
#include "../mem/heap.h"
#include "../sched/sim.h"
#include "../storage/pager.h"
#include "engine.h"

void scenario_list(sbuf_t *out)
{
    sb_printf(out,
              "scenarios:\n"
              "  sched [quantum]       compare FCFS/SJF/SRTF/RR/PRIO/MLFQ on a bank workload (Gantt + metrics)\n"
              "  starvation            priority scheduling starves a batch job; aging fixes it\n"
              "  paging                FIFO/LRU/CLOCK/OPT page faults and Belady's anomaly\n"
              "  alloc                 first/next/best/worst-fit vs buddy: fragmentation under bank traffic\n"
              "  banker                Banker's algorithm step by step (textbook example)\n"
              "  rw                    readers-writers: who starves under each lock preference\n"
              "  prodcons              bounded buffer with counting semaphores\n"
              "  race [threads]        lost updates without a lock, fixed with a mutex and an atomic\n"
              "  philosophers [n]      dining philosophers under every deadlock policy (threads)\n"
              "  deadlock [n]          LIVE: n transfers in a ring through the real tellers\n"
              "  flood [n]             LIVE: n synthetic jobs into the real scheduler\n");
}

/* ------------------------------------------------------------------ scheduling */

static const sim_proc_t bank_workload[] = {
    /* pid arrival burst priority : what it represents */
    {1, 0, 12, 3}, /* month-end interest batch (low priority, long) */
    {2, 1, 2, 2},  /* regular customer: deposit */
    {3, 2, 4, 1},  /* premium customer: transfer */
    {4, 3, 1, 0},  /* VIP: balance check */
    {5, 4, 8, 2},  /* regular: loan application */
    {6, 6, 4, 2},  /* regular: login (password hashing) */
    {7, 8, 2, 0},  /* VIP: withdrawal */
    {8, 9, 5, 1},  /* premium: FD opening */
};

static void scen_sched(char **args, int nargs, sbuf_t *out)
{
    int q = 3;
    if (nargs > 0)
        parse_int(args[0], &q);
    if (q < 1)
        q = 3;
    int n = (int)ARRAY_LEN(bank_workload);
    sb_printf(out, "Workload (1 tick = 1 teller time unit; priority 0 = VIP ... 3 = batch):\n");
    sb_printf(out, "  P1 interest batch | P2 deposit | P3 transfer | P4 VIP balance | P5 loan | P6 login | "
                   "P7 VIP withdraw | P8 FD\n\n");
    static sim_report_t r;
    double best = 1e9;
    sched_policy_t best_p = SP_FCFS;
    double avg[SP_POLICY_COUNT], resp[SP_POLICY_COUNT];
    int cs[SP_POLICY_COUNT];
    for (int p = 0; p < SP_POLICY_COUNT; p++) {
        sim_opts_t o = {.aging_interval = p == SP_PRIO ? 6 : 0, .boost_interval = p == SP_MLFQ ? 20 : 0};
        sim_run(bank_workload, n, (sched_policy_t)p, q, 1, &o, &r);
        sb_printf(out, "=== %s ===\n", sched_policy_name((sched_policy_t)p));
        sim_render_gantt(&r, out);
        sim_render_table(&r, out);
        sb_printf(out, "\n");
        avg[p] = r.avg_wait;
        resp[p] = r.avg_response;
        cs[p] = r.context_switches;
        if (r.avg_wait < best) {
            best = r.avg_wait;
            best_p = (sched_policy_t)p;
        }
    }
    sb_printf(out, "Summary (quantum %d):\n  %-6s %12s %13s %16s\n", q, "policy", "avg waiting", "avg response",
              "context switches");
    for (int p = 0; p < SP_POLICY_COUNT; p++)
        sb_printf(out, "  %-6s %12.2f %13.2f %16d%s\n", sched_policy_name((sched_policy_t)p), avg[p], resp[p], cs[p],
                  (sched_policy_t)p == best_p ? "   <- lowest waiting time" : "");
    sb_printf(out, "SJF/SRTF minimise waiting time but need burst estimates; RR and MLFQ give the best response "
                   "for short\ninteractive jobs; PRIO serves VIPs first and relies on aging to keep the batch job "
                   "from starving.\n");
}

static void scen_starvation(sbuf_t *out)
{
    enum { N = 60 };
    sim_proc_t p[N];
    p[0] = (sim_proc_t){1, 0, 6, 3};
    for (int i = 1; i < N; i++)
        p[i] = (sim_proc_t){i + 1, i * 2 - 1, 2, 0};
    static sim_report_t r;
    sb_printf(out, "One batch job (P1, priority 3, burst 6) and a stream of %d VIP jobs (priority 0)\n"
                   "arriving every 2 ticks, each needing 2 ticks: the VIPs alone keep the teller 100%% busy.\n\n",
              N - 1);
    sim_run(p, N, SP_PRIO, 1, 1, NULL, &r);
    int starved = r.res[0].finish;
    sb_printf(out, "Without aging: P1 runs 1 tick, then starves until the last VIP leaves; it finishes at t=%d\n"
                   "(turnaround %d ticks). An endless VIP stream would starve it forever.\n",
              starved, r.res[0].turnaround);
    sim_render_gantt(&r, out);
    sim_opts_t o = {.aging_interval = 3};
    sim_run(p, N, SP_PRIO, 1, 1, &o, &r);
    sb_printf(out, "\nWith aging (one priority step per 3 ticks waited): P1 is regularly promoted, takes a tick,\n"
                   "and is preempted again. It finishes at t=%d (turnaround %d ticks, %d ticks sooner).\n",
              r.res[0].finish, r.res[0].turnaround, starved - r.res[0].finish);
    sim_render_gantt(&r, out);
}

/* ------------------------------------------------------------------ paging */

static void scen_paging(sbuf_t *out)
{
    const int belady[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    const int text[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
    sb_printf(out, "Reference string A (Silberschatz): 7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1\n");
    sb_printf(out, "  %-6s %8s %8s %8s\n", "policy", "3 frames", "4 frames", "5 frames");
    for (int p = 0; p < PSIM_COUNT; p++)
        sb_printf(out, "  %-6s %8d %8d %8d\n", psim_name((psim_policy_t)p), page_sim(text, 20, 3, (psim_policy_t)p, NULL),
                  page_sim(text, 20, 4, (psim_policy_t)p, NULL), page_sim(text, 20, 5, (psim_policy_t)p, NULL));
    sb_printf(out, "\nLRU trace with 3 frames (PF = page fault):\n");
    page_sim(text, 20, 3, PSIM_LRU, out);
    sb_printf(out, "\nBelady's anomaly, reference string B: 1 2 3 4 1 2 5 1 2 3 4 5\n");
    for (int p = 0; p < PSIM_COUNT; p++) {
        int f3 = page_sim(belady, 12, 3, (psim_policy_t)p, NULL), f4 = page_sim(belady, 12, 4, (psim_policy_t)p, NULL);
        sb_printf(out, "  %-6s 3 frames: %2d faults, 4 frames: %2d faults%s\n", psim_name((psim_policy_t)p), f3, f4,
                  f4 > f3 ? "   <- MORE frames, MORE faults (anomaly)" : "");
    }
    sb_printf(out, "LRU and OPT are stack algorithms, so they can never show the anomaly; FIFO (and CLOCK,\n"
                   "which degrades to FIFO when every reference bit is set) can.\n"
                   "The live buffer pool ('pager' command) uses the same FIFO/LRU/CLOCK policies on real pages.\n");
}

/* ------------------------------------------------------------------ allocation */

static void scen_alloc(sbuf_t *out)
{
    /* object sizes the engine really allocates: requests, sessions, index
     * entries, child arrays, pending draws */
    static const size_t sizes[] = {sizeof(void *) * 4, 72, 120, 32, 64, 4456, 2200, 96, 520, 48};
    sb_printf(out, "Same random workload (40k operations, bank-object sizes 32 B to 4.4 KB) on a 512 KiB heap:\n\n");
    sb_printf(out, "  %-9s %7s %8s %9s %9s %10s %8s %11s\n", "policy", "fails", "blocks", "ext frag", "int frag",
              "largest", "coalesce", "search/op");
    for (int p = 0; p < ALLOC_POLICY_COUNT; p++) {
        heap_t *h = heap_create(512 * 1024, (alloc_policy_t)p);
        void *live[600] = {0};
        unsigned seed = 7;
        for (int i = 0; i < 40000; i++) {
            seed = seed * 1103515245u + 12345u;
            int slot = (int)((seed >> 8) % 600);
            if (live[slot]) {
                heap_free(h, live[slot]);
                live[slot] = NULL;
            } else {
                size_t n = sizes[(seed >> 20) % ARRAY_LEN(sizes)];
                live[slot] = heap_alloc(h, n);
            }
        }
        heap_stats_t s;
        heap_stats(h, &s);
        char map[65];
        heap_map(h, map, 64);
        sb_printf(out, "  %-9s %7llu %8llu %8.1f%% %8.1f%% %9zuB %8llu %11.2f\n", alloc_policy_name((alloc_policy_t)p),
                  (unsigned long long)s.failures, (unsigned long long)s.free_blocks, s.external_frag * 100,
                  s.internal_frag * 100, s.largest_free, (unsigned long long)s.coalesces,
                  (double)s.search_steps / (double)(s.allocs + s.failures));
        sb_printf(out, "            |%s|\n", map);
        heap_destroy(h);
    }
    sb_printf(out, "\nBuddy trades internal fragmentation (power-of-two rounding) for O(log n) splitting and\n"
                   "merging; best-fit keeps big holes intact but searches more; next-fit spreads allocations.\n");
}

/* ------------------------------------------------------------------ banker */

static void print_matrix(sbuf_t *out, const char *title, const int64_t *m, int n, int k)
{
    sb_printf(out, "  %-10s", title);
    for (int i = 0; i < n; i++) {
        sb_printf(out, " P%d(", i);
        for (int r = 0; r < k; r++)
            sb_printf(out, "%s%lld", r ? " " : "", (long long)m[i * k + r]);
        sb_printf(out, ")");
    }
    sb_printf(out, "\n");
}

static void scen_banker(sbuf_t *out)
{
    int64_t avail[3] = {3, 3, 2};
    int64_t max[15] = {7, 5, 3, 3, 2, 2, 9, 0, 2, 2, 2, 2, 4, 3, 3};
    int64_t alloc[15] = {0, 1, 0, 2, 0, 0, 3, 0, 2, 2, 1, 1, 0, 0, 2};
    int64_t need[15];
    for (int i = 0; i < 15; i++)
        need[i] = max[i] - alloc[i];
    sb_printf(out, "Five customers, three fund pools A B C. Available = (3 3 2)\n");
    print_matrix(out, "Max", max, 5, 3);
    print_matrix(out, "Allocation", alloc, 5, 3);
    print_matrix(out, "Need", need, 5, 3);
    int seq[5];
    bool safe = bk_safety(5, 3, avail, max, alloc, seq);
    sb_printf(out, "Safety algorithm: %s", safe ? "SAFE, sequence <" : "UNSAFE");
    for (int i = 0; safe && i < 5; i++)
        sb_printf(out, "%sP%d", i ? ", " : "", seq[i]);
    sb_printf(out, "%s\n\n", safe ? ">" : "");

    sb_printf(out, "Step 1: P1 requests (1 0 2). Request <= Need and <= Available, so pretend to grant it...\n");
    avail[0] -= 1, avail[2] -= 2;
    alloc[3] += 1, alloc[5] += 2;
    safe = bk_safety(5, 3, avail, max, alloc, seq);
    sb_printf(out, "  new Available (2 3 0) -> %s", safe ? "SAFE <" : "UNSAFE");
    for (int i = 0; safe && i < 5; i++)
        sb_printf(out, "%sP%d", i ? ", " : "", seq[i]);
    sb_printf(out, "%s: GRANTED\n", safe ? ">" : "");

    sb_printf(out, "Step 2: P4 requests (3 3 0). Available is (2 3 0), so P4 must WAIT.\n");
    sb_printf(out, "Step 3: P0 requests (0 2 0). It fits in Available; pretend to grant it...\n");
    avail[1] -= 2;
    alloc[1] += 2;
    safe = bk_safety(5, 3, avail, max, alloc, NULL);
    sb_printf(out, "  new Available (2 1 0) -> %s: %s\n\n", safe ? "SAFE" : "UNSAFE", safe ? "GRANTED" : "DEFERRED (rolled back)");
    sb_printf(out, "In Plutus the resources are the loan fund pools: 'banker' shows the live matrices, and\n"
                   "LOAN_DRAW requests that would make the state unsafe are deferred until repayments arrive.\n");
}

/* ------------------------------------------------------------------ readers-writers */

typedef struct {
    rwlock_t *l;
    _Atomic bool *stop;
    _Atomic uint64_t *count;
    int hold_us;
} rw_arg;

static void *rw_reader(void *p)
{
    rw_arg *a = p;
    while (!atomic_load(a->stop)) {
        rw_rdlock(a->l);
        sleep_us(a->hold_us);
        rw_rdunlock(a->l);
        atomic_fetch_add(a->count, 1);
    }
    return NULL;
}

static void *rw_writer(void *p)
{
    rw_arg *a = p;
    while (!atomic_load(a->stop)) {
        rw_wrlock(a->l);
        sleep_us(a->hold_us);
        rw_wrunlock(a->l);
        atomic_fetch_add(a->count, 1);
        sleep_us(200);
    }
    return NULL;
}

static void scen_rw(sbuf_t *out)
{
    sb_printf(out, "8 reader threads (overlapping 2 ms reads) and 2 writer threads (1 ms writes), 400 ms per mode:\n\n");
    sb_printf(out, "  %-12s %9s %9s %16s %16s\n", "mode", "reads", "writes", "max reader wait", "max writer wait");
    rw_mode_t modes[] = {RW_READER_PREF, RW_WRITER_PREF, RW_FAIR};
    for (int m = 0; m < 3; m++) {
        rwlock_t l;
        rw_init(&l, modes[m]);
        _Atomic bool stop = false;
        _Atomic uint64_t reads = 0, writes = 0;
        rw_arg ra = {&l, &stop, &reads, 2000}, wa = {&l, &stop, &writes, 1000};
        pthread_t t[10];
        for (int i = 0; i < 8; i++)
            pthread_create(&t[i], NULL, rw_reader, &ra);
        for (int i = 8; i < 10; i++)
            pthread_create(&t[i], NULL, rw_writer, &wa);
        sleep_us(400000);
        atomic_store(&stop, true);
        /* a writer stuck behind endless readers needs them to stop first */
        for (int i = 0; i < 10; i++)
            pthread_join(t[i], NULL);
        sb_printf(out, "  %-12s %9llu %9llu %13.1f ms %13.1f ms\n", rw_mode_name(modes[m]),
                  (unsigned long long)reads, (unsigned long long)writes, (double)l.max_read_wait_ns / 1e6,
                  (double)l.max_write_wait_ns / 1e6);
    }
    sb_printf(out, "\nReader preference: readers keep overlapping, so writers starve (huge writer wait).\n"
                   "Writer preference: a waiting writer blocks new readers.  Fair: FIFO tickets bound both waits.\n"
                   "The bank's account index uses this lock ('set rwmode ...'); the checkpoint gate uses writer-pref.\n");
}

/* ------------------------------------------------------------------ producer-consumer */

enum { PC_CAP = 8 };
static struct {
    csem_t empty, full;
    pthread_mutex_t m;
    int buf[PC_CAP], in, out, count, max_seen;
    _Atomic int produced, consumed, producer_blocks;
} PC;

static void *pc_producer(void *arg)
{
    for (int i = 0; i < 60; i++) {
        if (!csem_trywait(&PC.empty)) {
            atomic_fetch_add(&PC.producer_blocks, 1);
            csem_wait(&PC.empty); /* buffer full: block */
        }
        pthread_mutex_lock(&PC.m);
        PC.buf[PC.in] = i;
        PC.in = (PC.in + 1) % PC_CAP;
        if (++PC.count > PC.max_seen)
            PC.max_seen = PC.count;
        pthread_mutex_unlock(&PC.m);
        csem_post(&PC.full);
        atomic_fetch_add(&PC.produced, 1);
        sleep_us(300);
    }
    return arg;
}

static void *pc_consumer(void *arg)
{
    for (int i = 0; i < 60; i++) {
        csem_wait(&PC.full);
        pthread_mutex_lock(&PC.m);
        PC.out = (PC.out + 1) % PC_CAP;
        PC.count--;
        pthread_mutex_unlock(&PC.m);
        csem_post(&PC.empty);
        atomic_fetch_add(&PC.consumed, 1);
        sleep_us(1500); /* consumers are slower than producers */
    }
    return arg;
}

static void scen_prodcons(sbuf_t *out)
{
    memset(&PC, 0, sizeof PC);
    csem_init(&PC.empty, PC_CAP);
    csem_init(&PC.full, 0);
    pthread_mutex_init(&PC.m, NULL);
    pthread_t t[6];
    for (int i = 0; i < 3; i++)
        pthread_create(&t[i], NULL, pc_producer, NULL);
    for (int i = 3; i < 6; i++)
        pthread_create(&t[i], NULL, pc_consumer, NULL);
    sb_printf(out, "3 producers (every 0.3 ms) and 3 consumers (every 1.5 ms) share an %d-slot buffer.\n", PC_CAP);
    sb_printf(out, "Occupancy samples: ");
    for (int s = 0; s < 24; s++) {
        sleep_us(4000);
        pthread_mutex_lock(&PC.m);
        sb_printf(out, "%d ", PC.count);
        pthread_mutex_unlock(&PC.m);
    }
    for (int i = 0; i < 6; i++)
        pthread_join(t[i], NULL);
    sb_printf(out, "\nproduced %d, consumed %d, max occupancy %d/%d, producers blocked on a full buffer %d times\n",
              (int)PC.produced, (int)PC.consumed, PC.max_seen, PC_CAP, (int)PC.producer_blocks);
    sb_printf(out, "'empty' counts free slots and 'full' counts filled ones; the mutex guards in/out.\n"
                   "The engine's job queue is the same pattern: sessions produce jobs, tellers consume them.\n");
}

/* ------------------------------------------------------------------ race condition */

static long race_counter;
static _Atomic long race_atomic;
static pthread_mutex_t race_m = PTHREAD_MUTEX_INITIALIZER;
static int race_mode_sel, race_iters;

static void *race_worker(void *arg)
{
    for (int i = 0; i < race_iters; i++) {
        if (race_mode_sel == 0) {
            long v = race_counter; /* read */
            if ((i & 63) == 0)
                sched_yield();     /* widen the window */
            race_counter = v + 1;  /* write: may overwrite another thread's update */
        } else if (race_mode_sel == 1) {
            pthread_mutex_lock(&race_m);
            race_counter++;
            pthread_mutex_unlock(&race_m);
        } else {
            atomic_fetch_add(&race_atomic, 1);
        }
    }
    return arg;
}

static void scen_race(char **args, int nargs, sbuf_t *out)
{
    int threads = 8;
    if (nargs > 0)
        parse_int(args[0], &threads);
    if (threads < 2 || threads > 64)
        threads = 8;
    race_iters = 200000;
    long expected = (long)threads * race_iters;
    const char *names[] = {"no lock (read-modify-write)", "pthread mutex", "C11 atomic fetch_add"};
    sb_printf(out, "%d threads each add 1 to a shared balance %d times (expected %ld):\n", threads, race_iters, expected);
    for (int m = 0; m < 3; m++) {
        race_counter = 0;
        race_atomic = 0;
        race_mode_sel = m;
        pthread_t t[64];
        int64_t t0 = now_ns();
        for (int i = 0; i < threads; i++)
            pthread_create(&t[i], NULL, race_worker, NULL);
        for (int i = 0; i < threads; i++)
            pthread_join(t[i], NULL);
        long got = m == 2 ? race_atomic : race_counter;
        sb_printf(out, "  %-28s result %9ld  lost updates %8ld  (%.1f ms)\n", names[m], got, expected - got,
                  (double)(now_ns() - t0) / 1e6);
    }
    sb_printf(out, "Live version: 'set race on', run plutus-load against one account, then 'audit'.\n");
}

/* ------------------------------------------------------------------ philosophers */

static lockmgr_t PH;
typedef struct {
    int id, n, meals;
} phil_arg;

static void *philosopher(void *p)
{
    phil_arg *a = p;
    for (int m = 0; m < a->meals; m++) {
        txn_t t;
        txn_begin(&PH, &t, (uint64_t)(a->id + 1) * 100 + (uint64_t)m, "philosopher");
        uint64_t forks[2] = {(uint64_t)a->id + 1, (uint64_t)((a->id + 1) % a->n) + 1};
        while (lm_lock_set(&PH, &t, forks, 2) == LM_DEADLOCK)
            ;
        sleep_us(1000); /* eat */
        txn_end(&PH, &t);
        sleep_us(500); /* think */
    }
    return NULL;
}

static void scen_philosophers(char **args, int nargs, sbuf_t *out)
{
    int n = 5;
    if (nargs > 0)
        parse_int(args[0], &n);
    if (n < 2 || n > 16)
        n = 5;
    sb_printf(out, "%d philosophers, %d forks, 5 meals each; each grabs the left fork, waits 3 ms, then the right.\n\n",
              n, n);
    sb_printf(out, "  %-8s %10s %10s %9s %10s\n", "policy", "time (ms)", "deadlocks", "victims", "back-offs");
    dl_policy_t pols[] = {DL_DETECT, DL_ORDERED, DL_TRYLOCK};
    for (int k = 0; k < 3; k++) {
        lm_init(&PH, pols[k]);
        PH.hold_ns = 3 * 1000000LL;
        lm_start_detector(&PH, 10 * 1000000LL);
        phil_arg a[16];
        pthread_t t[16];
        int64_t t0 = now_ns();
        for (int i = 0; i < n; i++) {
            a[i] = (phil_arg){i, n, 5};
            pthread_create(&t[i], NULL, philosopher, &a[i]);
        }
        for (int i = 0; i < n; i++)
            pthread_join(t[i], NULL);
        lm_stop(&PH);
        sb_printf(out, "  %-8s %10.1f %10llu %9llu %10llu\n", dl_policy_name(pols[k]), (double)(now_ns() - t0) / 1e6,
                  (unsigned long long)PH.deadlocks_found, (unsigned long long)PH.victims,
                  (unsigned long long)PH.backoffs);
    }
    sb_printf(out, "\n(policy 'none' would hang forever here; see the LIVE 'scenario deadlock' for that.)\n"
                   "detect: let it happen, find the cycle in the wait-for graph, abort the youngest.\n"
                   "ordered: break circular wait (always lock the lower-numbered fork first).\n"
                   "trylock: break hold-and-wait (put the left fork down if the right one is taken).\n");
}

bool scenario_offline(const char *name, char **args, int nargs, sbuf_t *out)
{
    if (!strcmp(name, "sched"))
        scen_sched(args, nargs, out);
    else if (!strcmp(name, "starvation"))
        scen_starvation(out);
    else if (!strcmp(name, "paging"))
        scen_paging(out);
    else if (!strcmp(name, "alloc"))
        scen_alloc(out);
    else if (!strcmp(name, "banker"))
        scen_banker(out);
    else if (!strcmp(name, "rw"))
        scen_rw(out);
    else if (!strcmp(name, "prodcons"))
        scen_prodcons(out);
    else if (!strcmp(name, "race"))
        scen_race(args, nargs, out);
    else if (!strcmp(name, "philosophers"))
        scen_philosophers(args, nargs, out);
    else
        return false;
    return true;
}
