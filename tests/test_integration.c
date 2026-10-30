/* End-to-end test: boots bin/plutusd in a scratch directory and drives it over
 * its sockets like real clients would. Run from the project root (make itest). */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/common/proto.h"
#include "testlib.h"

static char dir[256], sock[300], admin[300];
static pid_t daemon_pid;
static const char *g_exe = "bin/plutusd";

static int connect_retry(const char *path, int ms)
{
    for (int i = 0; i < ms / 20; i++) {
        int fd = unix_connect(path);
        if (fd >= 0)
            return fd;
        sleep_us(20000);
    }
    return -1;
}

typedef struct {
    conn_t c;
    sbuf_t body;
    char status[512];
} client_t;

static bool client_open(client_t *cl, const char *path)
{
    int fd = connect_retry(path, 8000);
    if (fd < 0)
        return false;
    conn_init(&cl->c, fd);
    sb_init(&cl->body);
    return true;
}

static void client_close(client_t *cl)
{
    close(cl->c.fd);
    sb_free(&cl->body);
}

/* call(cl, "CMD", "arg", ..., NULL) */
static int call(client_t *cl, ...)
{
    const char *f[PROTO_MAX_FIELDS];
    int n = 0;
    va_list ap;
    va_start(ap, cl);
    for (const char *s; (s = va_arg(ap, const char *)) && n < PROTO_MAX_FIELDS;)
        f[n++] = s;
    va_end(ap);
    return proto_call(&cl->c, &cl->body, cl->status, sizeof cl->status, n, f);
}

static int admin_call(sbuf_t *out, char *status, size_t cap, const char *a, const char *b, const char *c)
{
    client_t cl;
    if (!client_open(&cl, admin))
        return -1;
    const char *f[3] = {a, b, c};
    int n = c ? 3 : b ? 2 : 1;
    int rc = proto_call(&cl.c, &cl.body, status, cap, n, f);
    if (out)
        sb_printf(out, "%s", cl.body.buf);
    client_close(&cl);
    return rc;
}

static bool audit_ok(void)
{
    char st[256];
    admin_call(NULL, st, sizeof st, "AUDIT", NULL, NULL);
    char *f[8];
    int n = proto_split(st, f, 8);
    return n >= 5 && f[4][0] == '1';
}

static void start_daemon(const char *extra1, const char *extra2)
{
    daemon_pid = fork();
    if (daemon_pid == 0) {
        int null = open("/dev/null", O_WRONLY);
        dup2(null, 1);
        dup2(null, 2);
        execl(g_exe, "plutusd", "-D", dir, "--tick", "500", "--audit", "1", "--month", "1000", "-t", "4",
              extra1, extra2, (char *)NULL);
        _exit(127);
    }
    int fd = connect_retry(sock, 8000);
    if (fd >= 0)
        close(fd);
}

static void stop_daemon(void)
{
    kill(daemon_pid, SIGTERM);
    int st = 0;
    waitpid(daemon_pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

static char A[32], B[32], C[32];

static void reg(client_t *cl, const char *name, const char *phone, const char *dep, char *out)
{
    CHECK_EQ(call(cl, "REGISTER", name, "Pune", "30", phone, "pass1234", dep, NULL), 1);
    snprintf(out, 32, "%s", cl->status + 3);
}

TEST(protocol_and_banking)
{
    client_t cl;
    CHECK(client_open(&cl, sock));
    CHECK_EQ(call(&cl, "PING", NULL), 1);
    reg(&cl, "Alice Rao", "9000000001", "10000", A);
    reg(&cl, "Bob Das", "9000000002", "5000", B);
    reg(&cl, "Chitra Sen", "9000000003", "20000", C);
    CHECK_EQ(call(&cl, "BALANCE", NULL), 0); /* not logged in */
    CHECK(strstr(cl.status, "AUTH") != NULL);
    CHECK_EQ(call(&cl, "REGISTER", "Kid", "Pune", "12", "9000000009", "pass1234", "10", NULL), 0);
    CHECK_EQ(call(&cl, "LOGIN", A, "wrong", NULL), 0);
    CHECK_EQ(call(&cl, "LOGIN", A, "pass1234", NULL), 1);
    CHECK_EQ(call(&cl, "DEPOSIT", "2500.50", NULL), 1);
    CHECK_EQ(call(&cl, "TRANSFER", B, "1000", NULL), 1);
    CHECK_EQ(call(&cl, "WITHDRAW", "999999", NULL), 0);
    CHECK(strstr(cl.status, "FUNDS") != NULL);
    CHECK_EQ(call(&cl, "BALANCE", NULL), 1);
    CHECK(strstr(cl.body.buf, "Rs 11,500.50") != NULL);
    CHECK_EQ(call(&cl, "FD_OPEN", "5000", "12", "0", NULL), 1);
    CHECK(strstr(cl.body.buf, "6.50% p.a.") != NULL);
    CHECK_EQ(call(&cl, "INS_BUY", "life", "Alice Rao", "Bob Das", "1", "0", "0", NULL), 1);
    CHECK_EQ(call(&cl, "STATEMENT", "10", NULL), 1);
    CHECK(strstr(cl.body.buf, "TRANSFER-OUT") != NULL);
    CHECK_EQ(call(&cl, "BOGUS", NULL), 0);
    client_close(&cl);
    CHECK(audit_ok());
}

typedef struct {
    const char *me, *other;
    int ok;
} xfer_arg;

static void *xfer_worker(void *p)
{
    xfer_arg *a = p;
    client_t cl;
    if (!client_open(&cl, sock))
        return NULL;
    if (call(&cl, "LOGIN", a->me, "pass1234", NULL) == 1)
        for (int i = 0; i < 25; i++)
            a->ok += call(&cl, "TRANSFER", a->other, "1.00", NULL) == 1;
    client_close(&cl);
    return NULL;
}

TEST(concurrent_opposing_transfers)
{
    char st[256];
    admin_call(NULL, st, sizeof st, "SET", "hold", "1500"); /* make deadlocks likely */
    xfer_arg args[6] = {{A, B, 0}, {B, A, 0}, {B, C, 0}, {C, B, 0}, {C, A, 0}, {A, C, 0}};
    pthread_t t[6];
    for (int i = 0; i < 6; i++)
        pthread_create(&t[i], NULL, xfer_worker, &args[i]);
    int ok = 0;
    for (int i = 0; i < 6; i++) {
        pthread_join(t[i], NULL);
        ok += args[i].ok;
    }
    admin_call(NULL, st, sizeof st, "SET", "hold", "0");
    CHECK_EQ(ok, 150); /* every transfer completes despite the deadlocks */
    CHECK(audit_ok());
}

static int engine_pid(void)
{
    sbuf_t out;
    sb_init(&out);
    char st[256];
    admin_call(&out, st, sizeof st, "STATS", NULL, NULL);
    int pid = 0;
    sscanf(out.buf, "Plutus engine pid %d", &pid);
    sb_free(&out);
    return pid;
}

TEST(sigkill_engine_recovers_everything)
{
    client_t cl;
    CHECK(client_open(&cl, sock));
    call(&cl, "LOGIN", A, "pass1234", NULL);
    call(&cl, "BALANCE", NULL);
    char before[512];
    snprintf(before, sizeof before, "%s", strstr(cl.body.buf, "Available"));
    client_close(&cl);
    int pid = engine_pid();
    CHECK(pid > 0);
    kill(pid, SIGKILL);
    sleep_us(300000);
    int pid2 = 0;
    for (int i = 0; i < 50 && (pid2 = engine_pid()) <= 0; i++)
        sleep_us(100000);
    CHECK(pid2 > 0 && pid2 != pid); /* the supervisor restarted it */
    CHECK(client_open(&cl, sock));
    CHECK_EQ(call(&cl, "LOGIN", A, "pass1234", NULL), 1);
    call(&cl, "BALANCE", NULL);
    CHECK(strstr(cl.body.buf, before) != NULL);
    client_close(&cl);
    CHECK(audit_ok());
}

typedef struct {
    const char *acct;
} dep_arg;

static void *deposit_worker(void *p)
{
    client_t cl;
    if (!client_open(&cl, sock))
        return NULL;
    if (call(&cl, "LOGIN", ((dep_arg *)p)->acct, "pass1234", NULL) == 1)
        for (int i = 0; i < 15; i++)
            call(&cl, "DEPOSIT", "1", NULL);
    client_close(&cl);
    return NULL;
}

TEST(race_mode_loses_updates_and_audit_catches_it)
{
    char st[256];
    admin_call(NULL, st, sizeof st, "SET", "race", "on");
    dep_arg a = {C};
    pthread_t t[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&t[i], NULL, deposit_worker, &a);
    for (int i = 0; i < 4; i++)
        pthread_join(t[i], NULL);
    CHECK(!audit_ok()); /* lost updates */
    admin_call(NULL, st, sizeof st, "SET", "race", "off");
    admin_call(NULL, st, sizeof st, "RECONCILE", NULL, NULL);
    CHECK(audit_ok());
    for (int i = 0; i < 4; i++)
        pthread_create(&t[i], NULL, deposit_worker, &a);
    for (int i = 0; i < 4; i++)
        pthread_join(t[i], NULL);
    CHECK(audit_ok()); /* locked again: nothing lost */
}

TEST(admin_scenarios_all_run)
{
    const char *names[] = {"sched", "starvation", "paging", "alloc", "banker", "rw", "prodcons", "race", "philosophers",
                           "deadlock", "flood"};
    for (size_t i = 0; i < ARRAY_LEN(names); i++) {
        char st[256];
        sbuf_t out;
        sb_init(&out);
        int rc = admin_call(&out, st, sizeof st, "SCENARIO", names[i], NULL);
        CHECK_EQ(rc, 1);
        CHECK(out.len > 100);
        sb_free(&out);
    }
    const char *views[] = {"STATS", "GANTT", "QUEUE", "LOCKS", "BANKER", "MEM", "PAGER", "WAL", "CHECKPOINT"};
    for (size_t i = 0; i < ARRAY_LEN(views); i++) {
        char st[256];
        CHECK_EQ(admin_call(NULL, st, sizeof st, views[i], NULL, NULL), 1);
    }
    char st[256];
    const char *pols[] = {"fcfs", "sjf", "srtf", "rr", "prio", "mlfq"};
    for (size_t i = 0; i < ARRAY_LEN(pols); i++)
        CHECK_EQ(admin_call(NULL, st, sizeof st, "SET", "sched", pols[i]), 1);
}

TEST(banker_defers_then_grants)
{
    client_t x, y;
    CHECK(client_open(&x, sock));
    CHECK(client_open(&y, sock));
    char X[32], Y[32];
    reg(&x, "Kabir Rao", "9100000001", "50000", X);
    reg(&y, "Meera Iyer", "9100000002", "50000", Y);
    call(&x, "LOGIN", X, "pass1234", NULL);
    call(&y, "LOGIN", Y, "pass1234", NULL);
    CHECK_EQ(call(&x, "LOAN_APPLY", "720", "home", "8", "120", "200000", NULL), 1);
    CHECK_EQ(call(&y, "LOAN_APPLY", "720", "home", "8", "120", "200000", NULL), 1);
    char lx[16], ly[16];
    call(&x, "LOAN_LIST", NULL);
    sscanf(strstr(x.body.buf, "300000"), "%15s", lx);
    call(&y, "LOAN_LIST", NULL);
    sscanf(strstr(y.body.buf, "300000"), "%15s", ly);
    CHECK_EQ(call(&x, "LOAN_DRAW", lx, "500000", NULL), 1);
    CHECK_EQ(call(&y, "LOAN_DRAW", ly, "400000", NULL), 1);
    CHECK(strstr(y.status, "PENDING") != NULL); /* unsafe: deferred */
    CHECK_EQ(call(&x, "LOAN_REPAY", lx, "200000", NULL), 1);
    sleep_us(100000);
    call(&y, "LOAN_LIST", NULL);
    CHECK(strstr(y.body.buf, "drawn Rs 4,00,000.00") != NULL); /* granted automatically */
    client_close(&x);
    client_close(&y);
    CHECK(audit_ok());
}

int main(void)
{
    t_begin("integration");
    if (getenv("PLUTUSD"))
        g_exe = getenv("PLUTUSD");
    if (access(g_exe, X_OK) != 0) {
        fprintf(stderr, "run from the project root after `make`\n");
        return 1;
    }
    snprintf(dir, sizeof dir, "/tmp/plutus-itest-%d", getpid());
    snprintf(sock, sizeof sock, "%s/plutus.sock", dir);
    snprintf(admin, sizeof admin, "%s/admin.sock", dir);
    mkdir(dir, 0700);

    start_daemon("--pools", "10,10,10,10");
    RUN(protocol_and_banking);
    RUN(concurrent_opposing_transfers);
    RUN(sigkill_engine_recovers_everything);
    RUN(race_mode_loses_updates_and_audit_catches_it);
    RUN(banker_defers_then_grants);
    RUN(admin_scenarios_all_run);
    stop_daemon();

    /* restart on the same data: everything must still be there */
    start_daemon("--pools", "10,10,10,10");
    client_t cl;
    CHECK(client_open(&cl, sock));
    CHECK_EQ(call(&cl, "LOGIN", B, "pass1234", NULL), 1);
    client_close(&cl);
    CHECK(audit_ok());
    stop_daemon();
    printf("  %-40s %s\n", "restart_preserves_data", t_failures ? "check above" : "ok");

    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    system(cmd);
    return t_end();
}
