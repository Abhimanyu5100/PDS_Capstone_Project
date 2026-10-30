/* plutusd - the Plutus Fincorp bank daemon (supervisor process).
 *
 * Responsibilities:
 *   - parse configuration, optionally daemonise (double fork + setsid)
 *   - single-instance guarantee with an flock()ed pid file
 *   - create the shared-memory stats segment and the log pipe
 *   - fork the logger, engine and auditor; reap them on SIGCHLD; restart the
 *     engine after a crash with exponential back-off (crash-loop protection)
 *   - orderly shutdown on SIGTERM/SIGINT: engine first (it drains and
 *     checkpoints), then the auditor, then the logger once the pipe is closed
 * Signals are turned into bytes on a self-pipe, so all real work happens in
 * the main loop and never inside a signal handler. */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "server.h"

static int g_sigpipe[2];
static void on_signal(int sig)
{
    unsigned char c = (unsigned char)sig;
    int saved = errno;
    ssize_t w = write(g_sigpipe[1], &c, 1);
    (void)w;
    errno = saved;
}

/* ------------------------------------------------------------------ logger */

static int logger_main(int rfd, const char *dir, bool tee)
{
    signal(SIGINT, SIG_IGN); /* exit only when every writer has closed the pipe */
    signal(SIGTERM, SIG_IGN);
    char path[512], old[520];
    snprintf(path, sizeof path, "%s/plutus.log", dir);
    snprintf(old, sizeof old, "%s/plutus.log.1", dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return 1;
    char buf[8192];
    off_t size = lseek(fd, 0, SEEK_END);
    for (;;) {
        ssize_t n = read(rfd, buf, sizeof buf);
        if (n == 0)
            break;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        ssize_t w = write(fd, buf, (size_t)n);
        (void)w;
        if (tee) {
            w = write(1, buf, (size_t)n);
            (void)w;
        }
        size += n;
        if (size > 4 * 1024 * 1024) { /* rotate */
            close(fd);
            rename(path, old);
            fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
            size = 0;
            if (fd < 0)
                return 1;
        }
    }
    close(fd);
    return 0;
}

/* ------------------------------------------------------------------ config */

static void usage(void)
{
    fprintf(stderr,
            "usage: plutusd [options]\n"
            "  -D, --data DIR         data directory (default ./plutus-data)\n"
            "  -t, --tellers N        teller threads = CPUs (default 4)\n"
            "  -s, --sched POLICY     fcfs|sjf|srtf|rr|prio|mlfq (default mlfq)\n"
            "  -q, --quantum N        RR / MLFQ base quantum in units (default 3)\n"
            "  -T, --tick US          length of one work unit in microseconds (default 2000)\n"
            "      --aging MS         PRIO aging interval (default 50)\n"
            "      --boost MS         MLFQ priority boost interval (default 500)\n"
            "  -c, --queue N          bounded ready-queue capacity (default 256)\n"
            "  -k, --deadlock POLICY  none|detect|ordered|trylock (default detect)\n"
            "  -f, --frames N         buffer-pool frames (default 16)\n"
            "  -r, --replace POLICY   fifo|lru|clock (default clock)\n"
            "  -a, --alloc POLICY     first-fit|next-fit|best-fit|worst-fit|buddy (default best-fit)\n"
            "      --heap MB          custom heap size (default 64)\n"
            "      --sync MODE        none|fsync|full (default fsync)\n"
            "      --month SECS       seconds per simulated bank month (default 120)\n"
            "      --pools H,P,S,B    loan fund pools in lakhs (default 1000,300,300,800)\n"
            "      --checkpoint SECS  checkpoint interval (default 30)\n"
            "      --audit SECS       auditor period (default 5)\n"
            "      --sessions N       maximum concurrent customers (default 128)\n"
            "  -d, --daemon           run in the background\n"
            "  -v, --verbose          debug logging\n");
    exit(2);
}

static void parse_pools(const char *s, int64_t pools[LOAN_TYPES])
{
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", s);
    char *save = NULL, *tok = strtok_r(tmp, ",", &save);
    for (int i = 0; i < LOAN_TYPES && tok; i++, tok = strtok_r(NULL, ",", &save)) {
        int64_t v;
        if (!parse_i64(tok, &v) || v <= 0)
            usage();
        pools[i] = v * LAKH;
    }
}

static void parse_config(int argc, char **argv, server_config_t *c)
{
    memset(c, 0, sizeof *c);
    snprintf(c->data_dir, sizeof c->data_dir, "plutus-data");
    c->tellers = 4;
    c->queue_cap = 256;
    c->sched = SP_MLFQ;
    c->quantum = 3;
    c->tick_us = 2000;
    c->aging_ms = 50;
    c->boost_ms = 500;
    c->alloc = ALLOC_BEST_FIT;
    c->heap_bytes = 64u << 20;
    c->max_sessions = 128;
    c->checkpoint_secs = 30;
    c->audit_secs = 5;
    c->foreground = true;
    c->log_level = L_INFO;
    c->bank.frames = 16;
    c->bank.repl = REPL_CLOCK;
    c->bank.sync = WAL_SYNC_FSYNC;
    c->bank.deadlock = DL_DETECT;
    c->bank.month_secs = 120;
    c->bank.pools[LOAN_HOME] = 1000 * LAKH;
    c->bank.pools[LOAN_PERSONAL] = 300 * LAKH;
    c->bank.pools[LOAN_STUDENT] = 300 * LAKH;
    c->bank.pools[LOAN_BUSINESS] = 800 * LAKH;

    enum { OPT_AGING = 1000, OPT_BOOST, OPT_HEAP, OPT_SYNC, OPT_MONTH, OPT_POOLS, OPT_CKPT, OPT_AUDIT, OPT_SESS };
    static const struct option opts[] = {
        {"data", required_argument, 0, 'D'},     {"tellers", required_argument, 0, 't'},
        {"sched", required_argument, 0, 's'},    {"quantum", required_argument, 0, 'q'},
        {"tick", required_argument, 0, 'T'},     {"aging", required_argument, 0, OPT_AGING},
        {"boost", required_argument, 0, OPT_BOOST}, {"queue", required_argument, 0, 'c'},
        {"deadlock", required_argument, 0, 'k'}, {"frames", required_argument, 0, 'f'},
        {"replace", required_argument, 0, 'r'},  {"alloc", required_argument, 0, 'a'},
        {"heap", required_argument, 0, OPT_HEAP},  {"sync", required_argument, 0, OPT_SYNC},
        {"month", required_argument, 0, OPT_MONTH}, {"pools", required_argument, 0, OPT_POOLS},
        {"checkpoint", required_argument, 0, OPT_CKPT}, {"audit", required_argument, 0, OPT_AUDIT},
        {"sessions", required_argument, 0, OPT_SESS}, {"daemon", no_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};
    int ch, iv;
    while ((ch = getopt_long(argc, argv, "D:t:s:q:T:c:k:f:r:a:dvh", opts, NULL)) != -1) {
        switch (ch) {
        case 'D': snprintf(c->data_dir, sizeof c->data_dir, "%s", optarg); break;
        case 't':
            if (!parse_int(optarg, &c->tellers) || c->tellers < 1 || c->tellers > MAX_TELLERS)
                usage();
            break;
        case 's': if (!sched_policy_parse(optarg, &c->sched)) usage(); break;
        case 'q': if (!parse_int(optarg, &c->quantum) || c->quantum < 1) usage(); break;
        case 'T': if (!parse_int(optarg, &iv) || iv < 0) usage(); c->tick_us = iv; break;
        case OPT_AGING: if (!parse_int(optarg, &iv) || iv < 0) usage(); c->aging_ms = iv; break;
        case OPT_BOOST: if (!parse_int(optarg, &iv) || iv < 0) usage(); c->boost_ms = iv; break;
        case 'c': if (!parse_int(optarg, &c->queue_cap) || c->queue_cap < 1) usage(); break;
        case 'k': if (!dl_policy_parse(optarg, &c->bank.deadlock)) usage(); break;
        case 'f': if (!parse_int(optarg, &c->bank.frames) || c->bank.frames < 2) usage(); break;
        case 'r': if (!repl_policy_parse(optarg, &c->bank.repl)) usage(); break;
        case 'a': if (!alloc_policy_parse(optarg, &c->alloc)) usage(); break;
        case OPT_HEAP: if (!parse_int(optarg, &iv) || iv < 1) usage(); c->heap_bytes = (size_t)iv << 20; break;
        case OPT_SYNC: if (!wal_sync_parse(optarg, &c->bank.sync)) usage(); break;
        case OPT_MONTH: if (!parse_int(optarg, &iv) || iv < 1) usage(); c->bank.month_secs = iv; break;
        case OPT_POOLS: parse_pools(optarg, c->bank.pools); break;
        case OPT_CKPT: if (!parse_int(optarg, &iv) || iv < 0) usage(); c->checkpoint_secs = iv; break;
        case OPT_AUDIT: if (!parse_int(optarg, &iv) || iv < 0) usage(); c->audit_secs = iv; break;
        case OPT_SESS: if (!parse_int(optarg, &c->max_sessions) || c->max_sessions < 1) usage(); break;
        case 'd': c->foreground = false; break;
        case 'v': c->log_level = L_DEBUG; break;
        default: usage();
        }
    }
    mkdir(c->data_dir, 0700);
    char real[PATH_MAX];
    if (realpath(c->data_dir, real))
        snprintf(c->data_dir, sizeof c->data_dir, "%s", real);
    snprintf(c->sock_path, sizeof c->sock_path, "%s/plutus.sock", c->data_dir);
    snprintf(c->admin_path, sizeof c->admin_path, "%s/admin.sock", c->data_dir);
}

/* ------------------------------------------------------------------ children */

typedef enum { CH_LOGGER, CH_ENGINE, CH_AUDITOR, CH_COUNT } child_t;
static const char *child_names[] = {"logger", "engine", "auditor"};
static pid_t g_pid[CH_COUNT];
static int g_log_pipe[2];
static server_config_t g_cfg;
static shm_stats_t *g_shm;
static int g_pidfd = -1;

static void child_setup(const char *name)
{
    /* restore default dispositions; the child installs its own handlers */
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    close(g_sigpipe[0]);
    close(g_sigpipe[1]);
    if (g_pidfd >= 0)
        close(g_pidfd); /* only the supervisor holds the instance lock */
    char title[32];
    snprintf(title, sizeof title, "plutus-%s", name);
    log_init(title + 7, g_log_pipe[1], g_cfg.log_level);
}

static pid_t start_child(child_t which)
{
    pid_t pid = fork();
    if (pid < 0) {
        LOGE("fork %s: %s", child_names[which], strerror(errno));
        return -1;
    }
    if (pid > 0) {
        g_pid[which] = pid;
        if (which == CH_LOGGER)
            g_shm->logger_pid = pid;
        return pid;
    }
    child_setup(child_names[which]);
    int rc;
    switch (which) {
    case CH_LOGGER:
        close(g_log_pipe[1]);
        rc = logger_main(g_log_pipe[0], g_cfg.data_dir, g_cfg.foreground);
        break;
    case CH_ENGINE:
        close(g_log_pipe[0]);
        rc = engine_main(&g_cfg, g_shm);
        break;
    default:
        close(g_log_pipe[0]);
        rc = auditor_main(&g_cfg, g_shm);
        break;
    }
    _exit(rc);
}

static void describe_status(int st, char *out, size_t n)
{
    if (WIFEXITED(st))
        snprintf(out, n, "exited with status %d", WEXITSTATUS(st));
    else if (WIFSIGNALED(st))
        snprintf(out, n, "killed by signal %d (%s)%s", WTERMSIG(st), strsignal(WTERMSIG(st)),
                 WCOREDUMP(st) ? ", core dumped" : "");
    else
        snprintf(out, n, "status 0x%x", st);
}

static int lock_pidfile(const char *dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/plutusd.pid", dir);
    int fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        char buf[32] = {0};
        ssize_t r = read(fd, buf, sizeof buf - 1);
        (void)r;
        fprintf(stderr, "plutusd: already running for %s (pid %s)\n", dir, buf[0] ? buf : "?");
        exit(1);
    }
    if (ftruncate(fd, 0) == 0) {
        char buf[32];
        int n = snprintf(buf, sizeof buf, "%d\n", getpid());
        ssize_t w = pwrite(fd, buf, (size_t)n, 0);
        (void)w;
    }
    return fd; /* stays open (and locked) for the life of the process */
}

static void daemonise(void)
{
    pid_t p = fork();
    if (p < 0)
        die("fork: %s", strerror(errno));
    if (p > 0)
        _exit(0);
    setsid(); /* new session, no controlling terminal */
    p = fork();
    if (p < 0)
        die("fork: %s", strerror(errno));
    if (p > 0)
        _exit(0); /* the grandchild can never reacquire a terminal */
    int fd = open("/dev/null", O_RDWR);
    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    if (fd > 2)
        close(fd);
}

int main(int argc, char **argv)
{
    parse_config(argc, argv, &g_cfg);
    if (!g_cfg.foreground) {
        printf("plutusd: starting in the background; logs in %s/plutus.log\n", g_cfg.data_dir);
        fflush(stdout);
        daemonise();
    }
    int pidfd = g_pidfd = lock_pidfile(g_cfg.data_dir);
    if (pidfd < 0)
        die("cannot open the pid file in %s", g_cfg.data_dir);

    g_shm = shm_create(g_cfg.data_dir);
    if (!g_shm)
        die("cannot create shared memory: %s", strerror(errno));
    g_shm->supervisor_pid = getpid();

    if (pipe(g_log_pipe) != 0 || pipe(g_sigpipe) != 0)
        die("pipe: %s", strerror(errno));
    fcntl(g_sigpipe[1], F_SETFL, O_NONBLOCK);
    fcntl(g_sigpipe[0], F_SETFD, FD_CLOEXEC);
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    log_init("super", g_log_pipe[1], g_cfg.log_level);
    start_child(CH_LOGGER);
    LOGI("Plutus Fincorp OS edition - supervisor pid %d, data in %s", getpid(), g_cfg.data_dir);
    start_child(CH_ENGINE);
    start_child(CH_AUDITOR);

    bool stopping = false;
    int64_t crash_times[8] = {0};
    int ncrash = 0;
    int64_t restart_at = 0, backoff_ms = 200;
    int64_t stop_deadline = 0;

    for (;;) {
        struct pollfd pf = {g_sigpipe[0], POLLIN, 0};
        int timeout = restart_at ? 50 : stopping ? 100 : 1000;
        int n = poll(&pf, 1, timeout);
        if (n > 0) {
            unsigned char sigs[64];
            ssize_t r = read(g_sigpipe[0], sigs, sizeof sigs);
            for (ssize_t i = 0; i < r; i++) {
                if ((sigs[i] == SIGTERM || sigs[i] == SIGINT) && !stopping) {
                    stopping = true;
                    stop_deadline = now_ns() + 15 * 1000000000LL;
                    LOGI("supervisor: shutdown requested (signal %d)", sigs[i]);
                    restart_at = 0;
                    if (g_pid[CH_ENGINE] > 0)
                        kill(g_pid[CH_ENGINE], SIGTERM);
                    if (g_pid[CH_AUDITOR] > 0)
                        kill(g_pid[CH_AUDITOR], SIGTERM);
                } else if (sigs[i] == SIGHUP) {
                    LOGI("supervisor: SIGHUP received (configuration is fixed at start-up; ignoring)");
                }
            }
        }
        /* reap every child that has changed state */
        int st;
        pid_t p;
        while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
            child_t which = CH_COUNT;
            for (int k = 0; k < CH_COUNT; k++)
                if (g_pid[k] == p)
                    which = (child_t)k;
            if (which == CH_COUNT)
                continue;
            g_pid[which] = 0;
            char how[96];
            describe_status(st, how, sizeof how);
            if (which == CH_LOGGER) {
                if (!stopping)
                    start_child(CH_LOGGER);
                continue;
            }
            if (stopping) {
                LOGI("%s %s", child_names[which], how);
                continue;
            }
            if (which == CH_AUDITOR) {
                LOGW("auditor %s; restarting it", how);
                start_child(CH_AUDITOR);
                continue;
            }
            /* the engine died unexpectedly */
            g_shm->engine_ready = 0;
            atomic_fetch_add(&g_shm->engine_restarts, 1);
            int64_t now = now_ns();
            crash_times[ncrash++ % 8] = now;
            int recent = 0;
            for (int k = 0; k < 8; k++)
                recent += crash_times[k] && now - crash_times[k] < 30 * 1000000000LL;
            if (recent >= 5) {
                LOGE("engine %s - 5 crashes within 30 s, giving up (crash loop)", how);
                stopping = true;
                stop_deadline = now_ns();
                if (g_pid[CH_AUDITOR] > 0)
                    kill(g_pid[CH_AUDITOR], SIGTERM);
                continue;
            }
            LOGE("engine %s; restarting in %lld ms (WAL recovery will replay committed work)", how,
                 (long long)backoff_ms);
            restart_at = now + backoff_ms * 1000000LL;
            backoff_ms = backoff_ms * 2 > 5000 ? 5000 : backoff_ms * 2;
        }
        if (restart_at && now_ns() >= restart_at) {
            restart_at = 0;
            start_child(CH_ENGINE);
        }
        if (!stopping && g_shm->engine_ready && wall_time() - g_shm->engine_started > 30)
            backoff_ms = 200; /* stable again: reset the back-off */
        if (stopping) {
            if (now_ns() > stop_deadline) {
                if (g_pid[CH_ENGINE] > 0) {
                    LOGE("engine did not stop in time; SIGKILL");
                    kill(g_pid[CH_ENGINE], SIGKILL);
                }
                if (g_pid[CH_AUDITOR] > 0)
                    kill(g_pid[CH_AUDITOR], SIGKILL);
            }
            if (g_pid[CH_ENGINE] <= 0 && g_pid[CH_AUDITOR] <= 0)
                break;
        }
    }
    LOGI("supervisor: all workers stopped; goodbye");
    close(g_log_pipe[1]); /* EOF lets the logger drain and exit */
    close(g_log_pipe[0]);
    if (g_pid[CH_LOGGER] > 0)
        waitpid(g_pid[CH_LOGGER], NULL, 0);
    shm_unlink_for(g_cfg.data_dir);
    char path[512];
    snprintf(path, sizeof path, "%s/plutusd.pid", g_cfg.data_dir);
    unlink(path);
    close(pidfd);
    return 0;
}
