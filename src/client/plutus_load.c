/* plutus-load - load generator: P processes x T threads of simulated customers.
 *
 *   plutus-load [-D dir] [-p procs] [-t threads] [-n ops] [-a accounts] [-m mix]
 *
 * mixes:
 *   mixed     40% balance, 25% deposit, 15% withdraw, 20% transfer (default)
 *   transfer  only transfers between random accounts (lock contention, deadlocks)
 *   hot       everyone deposits Rs 1 into ONE account (pair with `set race on`)
 *
 * The parent creates the accounts, forks the workers, and each worker sends its
 * latency samples back over a pipe. The parent merges them, prints throughput
 * and percentiles, and finally asks the admin socket for an AUDIT. */
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../common/proto.h"
#include "../common/util.h"

#define PASSWORD "loadtest1"

static char g_sock[512], g_admin[512];
static uint64_t *g_accts;
static int g_naccts, g_ops;
static const char *g_mix = "mixed";

typedef struct {
    int id;
    int64_t *lat; /* microseconds */
    int nlat;
    int errors, busy, reconnects;
} worker_t;

static int connect_sock(const char *path)
{
    for (int i = 0; i < 250; i++) { /* up to 5 s: rides out an engine restart */
        int fd = unix_connect(path);
        if (fd >= 0) {
            /* A crashed server can linger as a corpse that accepted our connection
             * but will never answer; a receive timeout turns that into an error
             * the reconnect logic can handle. */
            struct timeval tv = {8, 0}; /* > the 5 s queue-admission timeout */
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            return fd;
        }
        sleep_us(20000);
    }
    return -1;
}

static void *worker_main(void *arg)
{
    worker_t *w = arg;
    int fd = connect_sock(g_sock);
    if (fd < 0) {
        w->errors = g_ops;
        return NULL;
    }
    conn_t c;
    conn_init(&c, fd);
    char status[256], acct[32], other[32];
    uint64_t me = g_accts[(size_t)w->id % (size_t)g_naccts];
    bool hot = !strcmp(g_mix, "hot"), xfer = !strcmp(g_mix, "transfer");
    if (hot)
        me = g_accts[0];
    snprintf(acct, sizeof acct, "%llu", (unsigned long long)me);
    const char *login[] = {"LOGIN", acct, PASSWORD};
    if (proto_call(&c, NULL, status, sizeof status, 3, login) != 1) {
        w->errors = g_ops;
        close(fd);
        return NULL;
    }
    for (int i = 0; i < g_ops; i++) {
        int r = (int)rand_below(100);
        const char *f[3];
        int n;
        if (hot) {
            f[0] = "DEPOSIT", f[1] = "1", n = 2;
        } else if (xfer || r >= 80) {
            uint64_t to = g_accts[rand_below((uint32_t)g_naccts)];
            if (to == me)
                to = g_accts[((size_t)(rand_below((uint32_t)g_naccts)) + 1) % (size_t)g_naccts];
            if (to == me) {
                f[0] = "BALANCE", n = 1;
            } else {
                snprintf(other, sizeof other, "%llu", (unsigned long long)to);
                f[0] = "TRANSFER", f[1] = other, f[2] = "1.00", n = 3;
            }
        } else if (r < 40) {
            f[0] = "BALANCE", n = 1;
        } else if (r < 65) {
            f[0] = "DEPOSIT", f[1] = "10", n = 2;
        } else {
            f[0] = "WITHDRAW", f[1] = "5", n = 2;
        }
        int64_t t0 = now_ns();
        int rc = proto_call(&c, NULL, status, sizeof status, n, f);
        w->lat[w->nlat++] = (now_ns() - t0) / 1000;
        if (rc < 0) {
            /* the engine went away (crash or restart): reconnect and log in again */
            w->errors++;
            w->reconnects++;
            int lrc = -1;
            /* a dying engine may still accept a connection and then reset it, so
             * retry the whole connect + login sequence */
            for (int attempt = 0; attempt < 20 && lrc < 0; attempt++) {
                close(fd);
                if ((fd = connect_sock(g_sock)) < 0)
                    break;
                conn_init(&c, fd);
                lrc = proto_call(&c, NULL, status, sizeof status, 3, login);
            }
            if (lrc != 1) {
                w->errors += g_ops - i - 1;
                if (fd >= 0)
                    close(fd);
                return NULL;
            }
            continue;
        }
        if (rc == 0) {
            if (strstr(status, "BUSY"))
                w->busy++;
            else if (!strstr(status, "FUNDS"))
                w->errors++;
        }
    }
    const char *q[] = {"QUIT"};
    proto_call(&c, NULL, status, sizeof status, 1, q);
    close(fd);
    return NULL;
}

static int child_main(int procno, int threads, int wfd)
{
    worker_t *w = calloc((size_t)threads, sizeof *w);
    pthread_t *t = calloc((size_t)threads, sizeof *t);
    for (int i = 0; i < threads; i++) {
        w[i].id = procno * threads + i;
        w[i].lat = malloc(sizeof(int64_t) * (size_t)(g_ops + 1));
        pthread_create(&t[i], NULL, worker_main, &w[i]);
    }
    int32_t hdr[4] = {0};
    for (int i = 0; i < threads; i++) {
        pthread_join(t[i], NULL);
        hdr[0] += w[i].nlat;
        hdr[1] += w[i].errors;
        hdr[2] += w[i].busy;
        hdr[3] += w[i].reconnects;
    }
    /* report to the parent: header, then every latency sample */
    conn_write_all(wfd, hdr, sizeof hdr);
    for (int i = 0; i < threads; i++)
        conn_write_all(wfd, w[i].lat, sizeof(int64_t) * (size_t)w[i].nlat);
    close(wfd);
    return 0;
}

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return x < y ? -1 : x > y;
}

static bool read_full(int fd, void *buf, size_t n)
{
    char *p = buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r <= 0) {
            if (r < 0 && errno == EINTR)
                continue;
            return false;
        }
        p += r;
        n -= (size_t)r;
    }
    return true;
}

static void admin_print(const char *cmd)
{
    int fd = connect_sock(g_admin);
    if (fd < 0)
        return;
    conn_t c;
    conn_init(&c, fd);
    sbuf_t body;
    sb_init(&body);
    char status[256];
    const char *f[] = {cmd};
    if (proto_call(&c, &body, status, sizeof status, 1, f) >= 0)
        fputs(body.buf, stdout);
    sb_free(&body);
    close(fd);
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN); /* a dying server must not kill the workers */
    const char *dir = "plutus-data";
    int procs = 2, threads = 4;
    g_ops = 50;
    g_naccts = 16;
    int ch;
    while ((ch = getopt(argc, argv, "D:p:t:n:a:m:h")) != -1) {
        switch (ch) {
        case 'D': dir = optarg; break;
        case 'p': procs = atoi(optarg); break;
        case 't': threads = atoi(optarg); break;
        case 'n': g_ops = atoi(optarg); break;
        case 'a': g_naccts = atoi(optarg); break;
        case 'm': g_mix = optarg; break;
        default:
            fprintf(stderr, "usage: plutus-load [-D dir] [-p procs] [-t threads] [-n ops] [-a accounts] "
                            "[-m mixed|transfer|hot]\n");
            return 2;
        }
    }
    if (procs < 1 || threads < 1 || g_ops < 1 || g_naccts < 2) {
        fprintf(stderr, "plutus-load: bad arguments\n");
        return 2;
    }
    snprintf(g_sock, sizeof g_sock, "%s/plutus.sock", dir);
    snprintf(g_admin, sizeof g_admin, "%s/admin.sock", dir);

    /* 1. create the customer accounts */
    int fd = connect_sock(g_sock);
    if (fd < 0) {
        fprintf(stderr, "plutus-load: cannot connect to %s: %s\n", g_sock, strerror(errno));
        return 2;
    }
    conn_t c;
    conn_init(&c, fd);
    g_accts = calloc((size_t)g_naccts, sizeof *g_accts);
    int64_t t0 = now_ns();
    for (int i = 0; i < g_naccts; i++) {
        char name[48], phone[16];
        snprintf(name, sizeof name, "Load Customer %c%c", 'A' + i % 26, 'A' + (i / 26) % 26);
        snprintf(phone, sizeof phone, "%lld", 7000000000LL + i);
        const char *f[] = {"REGISTER", name, "Pune", "30", phone, PASSWORD, "100000"};
        char status[256];
        int rc = proto_call(&c, NULL, status, sizeof status, 7, f);
        if (rc < 0) { /* server restarting: reconnect and retry this registration */
            close(fd);
            if ((fd = connect_sock(g_sock)) < 0)
                return 2;
            conn_init(&c, fd);
            i--;
            continue;
        }
        if (rc != 1) {
            fprintf(stderr, "plutus-load: REGISTER failed: %s\n", status);
            return 1;
        }
        g_accts[i] = strtoull(status + 3, NULL, 10);
    }
    close(fd);
    printf("created %d accounts in %.0f ms (password hashing runs on the tellers)\n", g_naccts,
           (double)(now_ns() - t0) / 1e6);

    /* 2. fork the worker processes */
    printf("running %d processes x %d threads x %d ops, mix '%s'...\n", procs, threads, g_ops, g_mix);
    fflush(stdout);
    pid_t *pids = calloc((size_t)procs, sizeof *pids);
    int *pipes = calloc((size_t)procs, sizeof *pipes);
    t0 = now_ns();
    for (int p = 0; p < procs; p++) {
        int pf[2];
        if (pipe(pf) != 0) {
            perror("pipe");
            return 1;
        }
        pids[p] = fork();
        if (pids[p] == 0) {
            close(pf[0]);
            _exit(child_main(p, threads, pf[1]));
        }
        close(pf[1]);
        pipes[p] = pf[0];
    }
    /* 3. gather results (read each pipe to EOF before reaping) */
    size_t cap = (size_t)procs * (size_t)threads * (size_t)g_ops, n = 0;
    int64_t *lat = malloc(sizeof(int64_t) * (cap + 1));
    int errors = 0, busy = 0, reconnects = 0;
    for (int p = 0; p < procs; p++) {
        int32_t hdr[4];
        if (read_full(pipes[p], hdr, sizeof hdr)) {
            errors += hdr[1];
            busy += hdr[2];
            reconnects += hdr[3];
            if (n + (size_t)hdr[0] <= cap && read_full(pipes[p], lat + n, sizeof(int64_t) * (size_t)hdr[0]))
                n += (size_t)hdr[0];
        }
        close(pipes[p]);
        waitpid(pids[p], NULL, 0);
    }
    double secs = (double)(now_ns() - t0) / 1e9;
    qsort(lat, n, sizeof *lat, cmp_i64);
    printf("\n%zu requests in %.2f s = %.0f req/s | errors %d | rejected busy %d | reconnects %d\n", n, secs,
           (double)n / secs, errors, busy, reconnects);
    if (n)
        printf("latency  p50 %.2f ms | p90 %.2f ms | p99 %.2f ms | max %.2f ms\n", (double)lat[n / 2] / 1000.0,
               (double)lat[n * 9 / 10] / 1000.0, (double)lat[n * 99 / 100] / 1000.0, (double)lat[n - 1] / 1000.0);
    printf("\naudit after the run:\n");
    admin_print("AUDIT");
    return errors ? 1 : 0;
}
