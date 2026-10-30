/* plutus-admin - operator console for plutusd.
 *
 *   plutus-admin [-D dir] COMMAND [ARGS...]   one command
 *   plutus-admin [-D dir]                     interactive console
 *   plutus-admin [-D dir] top [seconds]       live dashboard read straight from
 *                                             the daemon's shared memory */
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "../common/proto.h"
#include "../server/server.h"

static conn_t g_conn;
static bool g_connected;
static const char *g_dir = "plutus-data";

static void ensure_conn(void)
{
    if (g_connected)
        return;
    char path[512];
    snprintf(path, sizeof path, "%s/admin.sock", g_dir);
    int fd = -1;
    for (int i = 0; i < 100 && fd < 0; i++) { /* ride out an engine restart (up to 2 s) */
        fd = unix_connect(path);
        if (fd < 0 && errno != ECONNREFUSED && errno != ENOENT)
            break;
        if (fd < 0)
            sleep_us(20000);
    }
    if (fd < 0) {
        fprintf(stderr, "plutus-admin: cannot connect to %s: %s (is plutusd running?)\n", path, strerror(errno));
        exit(2);
    }
    conn_init(&g_conn, fd);
    g_connected = true;
}

static int run(int n, const char *const f[])
{
    ensure_conn();
    sbuf_t body;
    sb_init(&body);
    char status[256];
    int rc = proto_call(&g_conn, &body, status, sizeof status, n, f);
    if (rc < 0) {
        fprintf(stderr, "plutus-admin: connection closed by the engine\n");
        g_connected = false;
        sb_free(&body);
        return 1;
    }
    fputs(body.buf, stdout);
    if (rc != 1)
        printf("%s\n", status);
    sb_free(&body);
    return rc == 1 ? 0 : 1;
}

static volatile sig_atomic_t g_stop;
static void on_int(int s) { (void)s; g_stop = 1; }

static const char *bar(double frac, char *buf, int width)
{
    int n = (int)(frac * width + 0.5);
    if (n > width)
        n = width;
    for (int i = 0; i < width; i++)
        buf[i] = i < n ? '#' : '-';
    buf[width] = '\0';
    return buf;
}

static int top(int seconds)
{
    shm_stats_t *s = shm_attach(g_dir, false);
    if (!s) {
        fprintf(stderr, "plutus-admin: no shared-memory segment for %s (%s)\n", g_dir, strerror(errno));
        return 2;
    }
    signal(SIGINT, on_int);
    static const char *sched[] = {"fcfs", "sjf", "srtf", "rr", "prio", "mlfq"};
    static const char *dl[] = {"none", "detect", "ordered", "trylock"};
    static const char *repl[] = {"fifo", "lru", "clock"};
    static const char *heap[] = {"first-fit", "next-fit", "best-fit", "worst-fit", "buddy"};
    static const char *ops[] = {"REGISTER", "LOGIN", "BALANCE", "DEPOSIT", "WITHDRAW", "TRANSFER", "STATEMENT",
                                "FD_QUOTE", "FD_OPEN", "FD_LIST", "FD_CLOSE", "LOAN_QUOTE", "LOAN_APPLY",
                                "LOAN_LIST", "LOAN_DRAW", "LOAN_REPAY", "INS_QUOTE", "INS_BUY", "INS_LIST",
                                "MONTH_END", "SYNTHETIC"};
    uint64_t last_done = s->jobs_done;
    int64_t t0 = now_ns(), last_t = t0;
    char b[64];
    while (!g_stop && (seconds <= 0 || now_ns() - t0 < (int64_t)seconds * 1000000000LL)) {
        int64_t now = now_ns();
        double dt = (double)(now - last_t) / 1e9;
        uint64_t done = s->jobs_done;
        double tput = dt > 0 ? (double)(done - last_done) / dt : 0;
        last_done = done;
        last_t = now;
        printf("\x1b[H\x1b[2J");
        printf("\x1b[1;33mPLUTUS FINCORP - live from shared memory\x1b[0m  (supervisor %lld, engine %lld, logger "
               "%lld, auditor %lld)\n",
               (long long)s->supervisor_pid, (long long)s->engine_pid, (long long)s->logger_pid,
               (long long)s->auditor_pid);
        double hb = s->heartbeat_ns ? (double)(now - s->heartbeat_ns) / 1e6 : -1;
        printf("engine %s | heartbeat %.0f ms ago | restarts %llu | bank month %d | sessions %d\n\n",
               s->engine_ready ? "\x1b[32mUP\x1b[0m" : "\x1b[31mDOWN\x1b[0m", hb,
               (unsigned long long)s->engine_restarts, (int)s->bank_month, (int)s->sessions);
        int nt = s->tellers, busy = s->busy_tellers;
        printf("\x1b[1mCPU scheduling\x1b[0m  %s q=%d tick=%lldus   throughput %.0f jobs/s\n",
               sched[s->sched_policy % 6], (int)s->quantum, (long long)s->tick_us, tput);
        printf("  tellers busy [%s] %d/%d\n", bar(nt ? (double)busy / nt : 0, b, 30), busy, nt);
        printf("  ready queue  [%s] %d/%d\n", bar(s->queue_cap ? (double)s->queue_len / s->queue_cap : 0, b, 30),
               (int)s->queue_len, (int)s->queue_cap);
        for (int t = 0; t < nt && t < MAX_TELLERS; t++) {
            int op = s->teller_op[t];
            printf("    teller %-2d %s\n", t, op >= 0 && op < (int)(sizeof ops / sizeof *ops) ? ops[op] : ".");
        }
        uint64_t d = s->jobs_done;
        printf("  done %llu | rejected %llu | preemptions %llu | avg wait %.2f ms | avg turnaround %.2f ms\n\n",
               (unsigned long long)d, (unsigned long long)s->jobs_rejected, (unsigned long long)s->preemptions,
               d ? (double)s->wait_ns / (double)d / 1e6 : 0, d ? (double)s->turnaround_ns / (double)d / 1e6 : 0);
        printf("\x1b[1mDeadlock\x1b[0m  policy %s | cycles found %llu | victims %llu | contended %llu\n",
               dl[s->dl_policy % 4], (unsigned long long)s->deadlocks, (unsigned long long)s->victims,
               (unsigned long long)s->lock_contended);
        uint64_t refs = s->page_hits + s->page_faults;
        printf("\x1b[1mPaging\x1b[0m    %d frames %s | hit ratio [%s] %.1f%% | faults %llu | evictions %llu\n",
               (int)s->frames, repl[s->repl_policy % 3], bar(refs ? (double)s->page_hits / (double)refs : 0, b, 20),
               refs ? 100.0 * (double)s->page_hits / (double)refs : 0, (unsigned long long)s->page_faults,
               (unsigned long long)s->evictions);
        printf("\x1b[1mHeap\x1b[0m      %s | used [%s] %llu KiB of %llu KiB | %llu free blocks, largest %llu B\n",
               heap[s->heap_policy % 5], bar(s->heap_cap ? (double)s->heap_used / (double)s->heap_cap : 0, b, 20),
               (unsigned long long)(s->heap_used / 1024), (unsigned long long)(s->heap_cap / 1024),
               (unsigned long long)s->heap_free_blocks, (unsigned long long)s->heap_largest_free);
        printf("\x1b[1mWAL\x1b[0m       commits %llu | fsync batches %llu | %.2f commits per fsync\n",
               (unsigned long long)s->wal_commits, (unsigned long long)s->wal_flushes,
               s->wal_flushes ? (double)s->wal_commits / (double)s->wal_flushes : 0);
        char m1[48], m2[48];
        printf("\x1b[1mAudit\x1b[0m     %s | %llu accounts | ledger %s | actual %s | audits %llu, violations %llu%s\n",
               s->audits ? (s->audit_ok ? "\x1b[32mCONSISTENT\x1b[0m" : "\x1b[31mMISMATCH\x1b[0m") : "pending",
               (unsigned long long)s->accounts, fmt_money(s->ledger_expected, m1, sizeof m1),
               fmt_money(s->ledger_actual, m2, sizeof m2), (unsigned long long)s->audits,
               (unsigned long long)s->audit_violations, s->race_mode ? "  \x1b[31m[RACE MODE ON]\x1b[0m" : "");
        printf("\n(Ctrl-C to quit)\n");
        fflush(stdout);
        sleep_us(500000);
    }
    return 0;
}

static void repl(void)
{
    char line[1024];
    printf("Plutus admin console. HELP lists commands, 'top' shows the live dashboard, QUIT exits.\n");
    for (;;) {
        printf("plutus-admin> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin))
            break;
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0])
            continue;
        if (!strcasecmp(line, "quit") || !strcasecmp(line, "exit"))
            break;
        char *f[PROTO_MAX_FIELDS];
        int n = 0;
        for (char *save = NULL, *t = strtok_r(line, " \t", &save); t && n < PROTO_MAX_FIELDS;
             t = strtok_r(NULL, " \t", &save))
            f[n++] = t;
        if (!strcasecmp(f[0], "top")) {
            g_stop = 0;
            top(n > 1 ? atoi(f[1]) : 0);
            continue;
        }
        run(n, (const char *const *)f);
    }
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    int ch;
    while ((ch = getopt(argc, argv, "+D:h")) != -1) {
        if (ch == 'D')
            g_dir = optarg;
        else {
            fprintf(stderr, "usage: plutus-admin [-D datadir] [COMMAND ARGS... | top [secs]]\n");
            return 2;
        }
    }
    if (optind >= argc) {
        repl();
        return 0;
    }
    if (!strcasecmp(argv[optind], "top"))
        return top(optind + 1 < argc ? atoi(argv[optind + 1]) : 0);
    return run(argc - optind, (const char *const *)argv + optind);
}
