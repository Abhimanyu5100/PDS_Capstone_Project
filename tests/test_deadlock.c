/* Deadlock tests: Banker's algorithm (textbook example) and the lock manager
 * under every policy, using real threads that deliberately lock in opposite
 * orders. */
#include <pthread.h>

#include "../src/deadlock/banker.h"
#include "../src/deadlock/lockmgr.h"
#include "testlib.h"

TEST(banker_textbook_safe_sequence)
{
    /* Silberschatz, Operating System Concepts, section 8.6.3 */
    int64_t avail[3] = {3, 3, 2};
    int64_t max[15] = {7, 5, 3, 3, 2, 2, 9, 0, 2, 2, 2, 2, 4, 3, 3};
    int64_t alloc[15] = {0, 1, 0, 2, 0, 0, 3, 0, 2, 2, 1, 1, 0, 0, 2};
    int seq[5];
    CHECK(bk_safety(5, 3, avail, max, alloc, seq));
    int expect[5] = {1, 3, 4, 0, 2};
    for (int i = 0; i < 5; i++)
        CHECK_EQ(seq[i], expect[i]);
    /* P1 requests (1,0,2): granted, the state stays safe */
    int64_t a1[3] = {2, 3, 0};
    alloc[3] = 3, alloc[5] = 2;
    CHECK(bk_safety(5, 3, a1, max, alloc, seq));
    /* then P0 requests (0,2,0): the pretend state is unsafe */
    int64_t a2[3] = {2, 1, 0};
    alloc[1] = 3;
    CHECK(!bk_safety(5, 3, a2, max, alloc, NULL));
}

TEST(banker_api_grants_defers_and_repays)
{
    banker_t b;
    int64_t total[1] = {100};
    const char *names[1] = {"home"};
    bk_init(&b, 1, total, names);
    CHECK_EQ(bk_raise_max(&b, 1, 0, 70), BK_OK);
    CHECK_EQ(bk_raise_max(&b, 2, 0, 60), BK_OK); /* sum of claims may exceed total */
    CHECK_EQ(bk_raise_max(&b, 3, 0, 150), BK_EXCEEDS_TOTAL);
    CHECK_EQ(bk_request(&b, 1, 0, 50), BK_OK);    /* avail 50: cust1 needs 20 -> safe */
    CHECK_EQ(bk_request(&b, 2, 0, 40), BK_UNSAFE);/* avail 10: neither can finish */
    CHECK_EQ(bk_request(&b, 2, 0, 30), BK_OK);    /* avail 20 = cust1's need -> safe */
    CHECK_EQ(bk_request(&b, 1, 0, 30), BK_EXCEEDS_NEED);
    CHECK_EQ(bk_request(&b, 2, 0, 25), BK_WAIT);  /* only 20 available */
    bk_repay(&b, 1, 0, 50);
    CHECK_EQ(b.available[0], 70);
    CHECK_EQ(bk_request(&b, 2, 0, 25), BK_OK);
    uint64_t seq[8];
    int n = 8;
    CHECK(bk_is_safe(&b, seq, &n));
    CHECK_EQ(n, 2);
    bk_destroy(&b);
}

TEST(banker_refuses_unsafe_sanction)
{
    banker_t b;
    int64_t total[1] = {100};
    bk_init(&b, 1, total, NULL);
    CHECK_EQ(bk_raise_max(&b, 1, 0, 100), BK_OK);
    CHECK_EQ(bk_request(&b, 1, 0, 60), BK_OK);
    CHECK_EQ(bk_raise_max(&b, 2, 0, 50), BK_OK); /* cust1 can finish with 40 and free 60 */
    CHECK_EQ(bk_request(&b, 2, 0, 10), BK_UNSAFE);
    bk_destroy(&b);
}

/* ------------------------------------------------------------ lock manager */

static lockmgr_t LM;

typedef struct {
    uint64_t id, a, b;
    int rounds;
    int restarts;
} worker_arg;

static void *transfer_worker(void *p)
{
    worker_arg *w = p;
    for (int i = 0; i < w->rounds; i++) {
        txn_t t;
        txn_begin(&LM, &t, w->id * 1000 + (uint64_t)i, "xfer");
        uint64_t rids[2] = {w->a, w->b};
        while (lm_lock_set(&LM, &t, rids, 2) == LM_DEADLOCK)
            w->restarts++;
        sleep_us(100);
        txn_end(&LM, &t);
    }
    return NULL;
}

static void run_pair(dl_policy_t pol, int rounds, int *restarts)
{
    lm_init(&LM, pol);
    LM.hold_ns = 2 * 1000000LL; /* widen the window between first and second lock */
    lm_start_detector(&LM, 10 * 1000000LL);
    worker_arg a = {1, 100, 200, rounds, 0}, b = {2, 200, 100, rounds, 0};
    pthread_t ta, tb;
    pthread_create(&ta, NULL, transfer_worker, &a);
    pthread_create(&tb, NULL, transfer_worker, &b);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    lm_stop(&LM);
    *restarts = a.restarts + b.restarts;
}

TEST(detect_breaks_ab_ba_deadlock)
{
    int restarts;
    run_pair(DL_DETECT, 10, &restarts);
    CHECK(LM.deadlocks_found >= 1);
    CHECK(LM.victims >= 1);
    CHECK_EQ(restarts, (int)LM.victims);
}

TEST(ordered_prevents_deadlock)
{
    int restarts;
    run_pair(DL_ORDERED, 10, &restarts);
    CHECK_EQ(LM.deadlocks_found, 0);
    CHECK_EQ(restarts, 0);
}

TEST(trylock_prevents_deadlock)
{
    int restarts;
    run_pair(DL_TRYLOCK, 10, &restarts);
    CHECK_EQ(LM.deadlocks_found, 0);
    CHECK_EQ(restarts, 0);
}

static uint64_t lm_read(const uint64_t *field)
{
    pthread_mutex_lock(&LM.m);
    uint64_t v = *field;
    pthread_mutex_unlock(&LM.m);
    return v;
}

TEST(none_really_deadlocks_until_rescued)
{
    lm_init(&LM, DL_NONE);
    LM.hold_ns = 5 * 1000000LL;
    lm_start_detector(&LM, 10 * 1000000LL);
    worker_arg a = {1, 100, 200, 1, 0}, b = {2, 200, 100, 1, 0};
    pthread_t ta, tb;
    pthread_create(&ta, NULL, transfer_worker, &a);
    pthread_create(&tb, NULL, transfer_worker, &b);
    for (int i = 0; i < 200 && lm_read(&LM.deadlocks_found) == 0; i++)
        sleep_us(10000);
    sleep_us(50000); /* several detector passes later, still stuck */
    CHECK(lm_read(&LM.deadlocks_found) >= 1);
    CHECK_EQ(lm_read(&LM.victims), 0);
    lm_set_policy(&LM, DL_DETECT); /* rescue the stuck threads */
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    lm_stop(&LM);
    CHECK(LM.victims >= 1);
}

/* dining philosophers: N transactions in a ring, each locking i then i+1 */
TEST(dining_philosophers_ring)
{
    enum { N = 5 };
    lm_init(&LM, DL_DETECT);
    LM.hold_ns = 2 * 1000000LL;
    lm_start_detector(&LM, 10 * 1000000LL);
    worker_arg w[N];
    pthread_t t[N];
    for (int i = 0; i < N; i++) {
        w[i] = (worker_arg){(uint64_t)i + 1, 10 + (uint64_t)i, 10 + (uint64_t)((i + 1) % N), 5, 0};
        pthread_create(&t[i], NULL, transfer_worker, &w[i]);
    }
    for (int i = 0; i < N; i++)
        pthread_join(t[i], NULL);
    lm_stop(&LM);
    CHECK_EQ(LM.nactive, 0);
}

int main(void)
{
    t_begin("deadlock");
    RUN(banker_textbook_safe_sequence);
    RUN(banker_api_grants_defers_and_repays);
    RUN(banker_refuses_unsafe_sanction);
    RUN(detect_breaks_ab_ba_deadlock);
    RUN(ordered_prevents_deadlock);
    RUN(trylock_prevents_deadlock);
    RUN(none_really_deadlocks_until_rescued);
    RUN(dining_philosophers_ring);
    return t_end();
}
