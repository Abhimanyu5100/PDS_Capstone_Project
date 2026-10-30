/* auditor.c - the plutus-auditor process.
 *
 * An independent process that
 *   - watches the engine's heartbeat in shared memory; if the engine stops
 *     beating for 5 s it is considered hung and is SIGKILLed, and the
 *     supervisor restarts it (WAL recovery brings the data back);
 *   - every audit period, connects to the admin socket and asks for an AUDIT,
 *     verifying that the sum of all balances equals the ledger. In race mode
 *     this is how lost updates are caught.
 * Results are published back into shared memory for `plutus-admin top`. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/proto.h"
#include "server.h"

static volatile sig_atomic_t g_stop;
static void on_term(int s) { (void)s; g_stop = 1; }

static void run_audit(const server_config_t *cfg, shm_stats_t *shm)
{
    int fd = unix_connect(cfg->admin_path);
    if (fd < 0)
        return;
    conn_t c;
    conn_init(&c, fd);
    char status[256];
    const char *req[] = {"AUDIT"};
    int rc = proto_call(&c, NULL, status, sizeof status, 1, req);
    close(fd);
    if (rc != 1)
        return;
    char *f[8];
    int nf = proto_split(status, f, 8);
    int64_t actual, expected, n;
    if (nf < 5 || !parse_i64(f[1], &actual) || !parse_i64(f[2], &expected) || !parse_i64(f[3], &n))
        return;
    bool ok = f[4][0] == '1';
    shm->ledger_actual = actual;
    shm->ledger_expected = expected;
    shm->accounts = (uint64_t)n;
    shm->audit_ok = ok;
    shm->last_audit = wall_time();
    atomic_fetch_add(&shm->audits, 1);
    if (!ok) {
        atomic_fetch_add(&shm->audit_violations, 1);
        char m1[48], m2[48], m3[48];
        LOGE("AUDIT FAILED: balances sum to %s but the ledger says %s (difference %s)%s",
             fmt_money(actual, m1, sizeof m1), fmt_money(expected, m2, sizeof m2),
             fmt_money(actual - expected, m3, sizeof m3), shm->race_mode ? " - race mode is ON" : "");
    }
}

int auditor_main(const server_config_t *cfg, shm_stats_t *shm)
{
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGPIPE, SIG_IGN);
    shm->auditor_pid = getpid();
    LOGI("auditor started: watchdog 5 s, audit every %lld s", (long long)cfg->audit_secs);
    int64_t last_audit = 0;
    while (!g_stop) {
        sleep_us(250000);
        if (getppid() == 1)
            break; /* the supervisor is gone */
        if (!shm->engine_ready)
            continue;
        int64_t hb = shm->heartbeat_ns, now = now_ns();
        pid_t epid = (pid_t)shm->engine_pid;
        if (hb && now - hb > 5 * 1000000000LL && epid > 0) {
            LOGE("watchdog: engine pid %d missed its heartbeat for %.1f s - killing it", epid, (double)(now - hb) / 1e9);
            shm->engine_ready = 0;
            kill(epid, SIGKILL);
            continue;
        }
        if (cfg->audit_secs > 0 && wall_time() - last_audit >= cfg->audit_secs) {
            run_audit(cfg, shm);
            last_audit = wall_time();
        }
    }
    LOGI("auditor stopped");
    return 0;
}
