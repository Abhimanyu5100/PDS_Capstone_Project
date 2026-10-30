/* Unit tests for the common layer and the heap allocator. */
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/common/crypto.h"
#include "../src/common/proto.h"
#include "../src/common/sync.h"
#include "../src/common/util.h"
#include "../src/mem/heap.h"
#include "testlib.h"

TEST(sha256_vectors)
{
    uint8_t d[32];
    char hex[65];
    sha256("", 0, d);
    hex_encode(d, 32, hex);
    CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256("abc", 3, d);
    hex_encode(d, 32, hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256(two, strlen(two), d);
    hex_encode(d, 32, hex);
    CHECK_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(crc32_vector) { CHECK_EQ(crc32("123456789", 9), 0xCBF43926u); }

TEST(password_roundtrip)
{
    uint8_t salt[PW_SALT_LEN], h[PW_HASH_LEN];
    random_bytes(salt, sizeof salt);
    password_hash("s3cret!", salt, h);
    CHECK(password_verify("s3cret!", salt, h));
    CHECK(!password_verify("s3cret?", salt, h));
}

TEST(money_parse_format)
{
    int64_t p;
    CHECK(parse_money("1234.5", &p));
    CHECK_EQ(p, 123450);
    CHECK(parse_money("0.07", &p));
    CHECK_EQ(p, 7);
    CHECK(!parse_money("-5", &p));
    CHECK(!parse_money("12a", &p));
    char buf[64];
    CHECK_STR(fmt_money(1234567890, buf, sizeof buf), "Rs 1,23,45,678.90");
    CHECK_STR(fmt_money(99900, buf, sizeof buf), "Rs 999.00");
    CHECK_STR(fmt_money(100000, buf, sizeof buf), "Rs 1,000.00");
}

/* ------------------------------------------------------------ semaphore */

static csem_t g_sem;
static int g_inside, g_max_inside;
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;

static void *sem_worker(void *arg)
{
    for (int i = 0; i < 200; i++) {
        csem_wait(&g_sem);
        pthread_mutex_lock(&g_m);
        if (++g_inside > g_max_inside)
            g_max_inside = g_inside;
        pthread_mutex_unlock(&g_m);
        pthread_mutex_lock(&g_m);
        g_inside--;
        pthread_mutex_unlock(&g_m);
        csem_post(&g_sem);
    }
    return arg;
}

TEST(semaphore_bounds_concurrency)
{
    csem_init(&g_sem, 3);
    pthread_t t[8];
    for (int i = 0; i < 8; i++)
        pthread_create(&t[i], NULL, sem_worker, NULL);
    for (int i = 0; i < 8; i++)
        pthread_join(t[i], NULL);
    CHECK(g_max_inside <= 3);
    CHECK_EQ(csem_value(&g_sem), 3);
}

TEST(semaphore_timeout)
{
    csem_t s;
    csem_init(&s, 0);
    int64_t t0 = now_ns();
    CHECK(!csem_timedwait(&s, 20 * 1000000LL));
    CHECK(now_ns() - t0 >= 15 * 1000000LL);
    csem_post(&s);
    CHECK(csem_trywait(&s));
}

/* ------------------------------------------------------------ rwlock */

static rwlock_t g_rw;
static long g_shared;

static void *rw_writer(void *arg)
{
    for (int i = 0; i < 2000; i++) {
        rw_wrlock(&g_rw);
        long v = g_shared;
        g_shared = v + 1;
        rw_wrunlock(&g_rw);
    }
    return arg;
}

static void *rw_reader(void *arg)
{
    for (int i = 0; i < 2000; i++) {
        rw_rdlock(&g_rw);
        volatile long v = g_shared;
        (void)v;
        rw_rdunlock(&g_rw);
    }
    return arg;
}

TEST(rwlock_all_modes_exclusive)
{
    rw_mode_t modes[] = {RW_READER_PREF, RW_WRITER_PREF, RW_FAIR};
    for (size_t m = 0; m < 3; m++) {
        rw_init(&g_rw, modes[m]);
        g_shared = 0;
        pthread_t t[8];
        for (int i = 0; i < 8; i++)
            pthread_create(&t[i], NULL, i % 2 ? rw_writer : rw_reader, NULL);
        for (int i = 0; i < 8; i++)
            pthread_join(t[i], NULL);
        CHECK_EQ(g_shared, 4 * 2000);
    }
}

/* ------------------------------------------------------------ heap */

static void heap_policy_test(alloc_policy_t pol)
{
    heap_t *h = heap_create(1 << 20, pol);
    CHECK(h != NULL);
    void *ptrs[500] = {0};
    char why[128];
    srand(42);
    for (int round = 0; round < 20000; round++) {
        int i = rand() % 500;
        if (ptrs[i]) {
            /* the sentinel byte must survive neighbouring operations */
            CHECK(((unsigned char *)ptrs[i])[0] == (unsigned char)i);
            heap_free(h, ptrs[i]);
            ptrs[i] = NULL;
        } else {
            size_t n = (size_t)(rand() % 900) + 1;
            ptrs[i] = heap_alloc(h, n);
            if (ptrs[i]) {
                CHECK(((uintptr_t)ptrs[i] & 15) == 0);
                memset(ptrs[i], i & 0xff, n);
            }
        }
    }
    CHECK(heap_check(h, why, sizeof why));
    if (why[0])
        fprintf(stderr, "    %s: %s\n", alloc_policy_name(pol), why);
    for (int i = 0; i < 500; i++)
        heap_free(h, ptrs[i]);
    heap_stats_t s;
    heap_stats(h, &s);
    CHECK_EQ(s.used_bytes, 0);
    CHECK_EQ(s.free_blocks, 1); /* everything coalesced back into one block */
    CHECK_EQ(s.free_bytes, s.capacity);
    CHECK(heap_check(h, why, sizeof why));
    heap_destroy(h);
}

TEST(heap_first_fit) { heap_policy_test(ALLOC_FIRST_FIT); }
TEST(heap_next_fit) { heap_policy_test(ALLOC_NEXT_FIT); }
TEST(heap_best_fit) { heap_policy_test(ALLOC_BEST_FIT); }
TEST(heap_worst_fit) { heap_policy_test(ALLOC_WORST_FIT); }
TEST(heap_buddy) { heap_policy_test(ALLOC_BUDDY); }

TEST(heap_buddy_internal_fragmentation)
{
    heap_t *h = heap_create(1 << 16, ALLOC_BUDDY);
    void *p = heap_alloc(h, 33); /* needs 49 bytes -> 64-byte block */
    heap_stats_t s;
    heap_stats(h, &s);
    CHECK_EQ(s.used_bytes, 64);
    CHECK(s.internal_frag > 0.4);
    heap_free(h, p);
    heap_destroy(h);
}

TEST(heap_exhaustion_reports_failure)
{
    heap_t *h = heap_create(4096, ALLOC_BEST_FIT);
    CHECK(heap_alloc(h, 8192) == NULL);
    heap_stats_t s;
    heap_stats(h, &s);
    CHECK_EQ(s.failures, 1);
    heap_destroy(h);
}

/* sun_path holds about 104 bytes; deeper data directories must still work */
TEST(unix_socket_long_path)
{
    char dir[512], path[600], cwd[1024], after[1024];
    int n = snprintf(dir, sizeof dir, "/tmp/plutus-longsock-%d", (int)getpid());
    mkdir(dir, 0700);
    snprintf(dir + n, sizeof dir - (size_t)n, "/%s", "a-directory-name-long-enough-to-push-the-socket-path-past-the-sun-path-limit");
    CHECK(mkdir(dir, 0700) == 0);
    snprintf(path, sizeof path, "%s/test.sock", dir);
    CHECK(strlen(path) > 110);
    CHECK(getcwd(cwd, sizeof cwd) != NULL);

    int lfd = unix_listen(path, 0600);
    CHECK(lfd >= 0);
    int cfd = unix_connect(path);
    CHECK(cfd >= 0);
    int afd = lfd >= 0 ? accept(lfd, NULL, NULL) : -1;
    CHECK(afd >= 0);
    if (cfd >= 0 && afd >= 0) {
        conn_t c;
        conn_init(&c, afd);
        char line[64];
        CHECK(conn_write_all(cfd, "hello\n", 6));
        CHECK(conn_read_line(&c, line, sizeof line) == 5);
        CHECK_STR(line, "hello");
    }
    struct stat st;
    CHECK(stat(path, &st) == 0 && S_ISSOCK(st.st_mode) && (st.st_mode & 0777) == 0600);
    CHECK(getcwd(after, sizeof after) != NULL);
    CHECK_STR(after, cwd); /* the helpers must leave the working directory alone */

    if (afd >= 0)
        close(afd);
    if (cfd >= 0)
        close(cfd);
    if (lfd >= 0)
        close(lfd);
    unlink(path);
    rmdir(dir);
    dir[n] = '\0';
    rmdir(dir);
}

int main(void)
{
    t_begin("common");
    RUN(sha256_vectors);
    RUN(crc32_vector);
    RUN(password_roundtrip);
    RUN(money_parse_format);
    RUN(semaphore_bounds_concurrency);
    RUN(semaphore_timeout);
    RUN(rwlock_all_modes_exclusive);
    RUN(heap_first_fit);
    RUN(heap_next_fit);
    RUN(heap_best_fit);
    RUN(heap_worst_fit);
    RUN(heap_buddy);
    RUN(heap_buddy_internal_fragmentation);
    RUN(heap_exhaustion_reports_failure);
    RUN(unix_socket_long_path);
    return t_end();
}
