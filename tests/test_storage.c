/* Storage tests: buffer pool replacement, Belady's anomaly, WAL group commit,
 * crash recovery (simulated by forking a child that is killed mid-workload)
 * and torn-tail handling. */
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/storage/db.h"
#include "testlib.h"

static char dir[256];

static void fresh_dir(const char *tag)
{
    snprintf(dir, sizeof dir, "/tmp/plutus-test-%s-%d", tag, getpid());
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    system(cmd);
}

static void cleanup(void)
{
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    system(cmd);
}

TEST(belady_anomaly_fifo)
{
    const int refs[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    CHECK_EQ(page_sim(refs, 12, 3, PSIM_FIFO, NULL), 9);
    CHECK_EQ(page_sim(refs, 12, 4, PSIM_FIFO, NULL), 10); /* more frames, more faults */
    CHECK(page_sim(refs, 12, 4, PSIM_LRU, NULL) <= page_sim(refs, 12, 3, PSIM_LRU, NULL));
}

TEST(opt_is_optimal)
{
    const int refs[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
    CHECK_EQ(page_sim(refs, 20, 3, PSIM_OPT, NULL), 9);
    CHECK_EQ(page_sim(refs, 20, 3, PSIM_LRU, NULL), 12);
    CHECK_EQ(page_sim(refs, 20, 3, PSIM_FIFO, NULL), 15);
    for (int p = 0; p < PSIM_COUNT; p++)
        CHECK(page_sim(refs, 20, 3, PSIM_OPT, NULL) <= page_sim(refs, 20, 3, (psim_policy_t)p, NULL));
}

static record_t mkrec(uint64_t key, int64_t value)
{
    record_t r;
    memset(&r, 0, sizeof r);
    r.type = 1;
    r.key = key;
    memcpy(r.body, &value, sizeof value);
    return r;
}

static int64_t recval(const record_t *r)
{
    int64_t v;
    memcpy(&v, r->body, sizeof v);
    return v;
}

TEST(commit_read_and_reopen)
{
    fresh_dir("reopen");
    db_t db;
    CHECK_EQ(db_open(&db, dir, 4, REPL_CLOCK, WAL_SYNC_NONE), 0);
    CHECK(db.recovery.fresh);
    uint64_t rids[100];
    for (int i = 0; i < 100; i++) {
        rids[i] = db_alloc_rid(&db);
        db_write_t w = {rids[i], mkrec((uint64_t)i + 1, i * 10)};
        CHECK_EQ(db_commit(&db, (uint64_t)i + 1, &w, 1), 0);
    }
    /* 100 records over 7 pages with only 4 frames: evictions must happen */
    CHECK(db.pager.evictions > 0);
    record_t r;
    db_read(&db, rids[42], &r);
    CHECK_EQ(recval(&r), 420);
    db_close(&db);

    CHECK_EQ(db_open(&db, dir, 4, REPL_LRU, WAL_SYNC_NONE), 0);
    CHECK(!db.recovery.fresh);
    CHECK_EQ(db.recovery.txns, 0); /* clean shutdown checkpointed everything */
    db_read(&db, rids[99], &r);
    CHECK_EQ(recval(&r), 990);
    db_close(&db);
    cleanup();
}

/* The child commits forever and is SIGKILLed. Everything it reported as
 * committed (through a pipe) must survive recovery. */
TEST(crash_recovery_after_sigkill)
{
    fresh_dir("crash");
    int pfd[2];
    pipe(pfd);
    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[0]);
        db_t db;
        if (db_open(&db, dir, 3, REPL_FIFO, WAL_SYNC_FSYNC) != 0)
            _exit(2);
        uint64_t rid = db_alloc_rid(&db);
        for (int64_t i = 1;; i++) {
            db_write_t w = {rid, mkrec(7, i)};
            if (db_commit(&db, (uint64_t)i, &w, 1) != 0)
                _exit(3);
            if (write(pfd[1], &i, sizeof i) != sizeof i)
                _exit(4);
        }
    }
    close(pfd[1]);
    int64_t last = 0, v;
    int64_t t0 = now_ns();
    while (now_ns() - t0 < 300 * 1000000LL && read(pfd[0], &v, sizeof v) == sizeof v)
        last = v;
    kill(pid, SIGKILL);
    int st;
    waitpid(pid, &st, 0);
    while (read(pfd[0], &v, sizeof v) == sizeof v)
        last = v;
    close(pfd[0]);
    CHECK(last > 10);

    db_t db;
    CHECK_EQ(db_open(&db, dir, 3, REPL_FIFO, WAL_SYNC_FSYNC), 0);
    CHECK(db.recovery.txns > 0);
    bool found = false;
    for (uint64_t pid2 = 1; pid2 < db.pager.npages && !found; pid2++)
        for (int s = 0; s < SLOTS_PER_PAGE; s++) {
            record_t r;
            db_read(&db, RID(pid2, s), &r);
            if (r.type == 1 && r.key == 7) {
                CHECK(recval(&r) >= last); /* acknowledged commits are never lost */
                found = true;
            }
        }
    CHECK(found);
    db_close(&db);
    cleanup();
}

TEST(torn_wal_tail_is_discarded)
{
    fresh_dir("torn");
    pid_t pid = fork();
    if (pid == 0) {
        /* child: checkpoint 111, then commit 222 and 333 and "crash" (exit
         * without a checkpoint, so the page on disk still says 111) */
        db_t db;
        db_open(&db, dir, 4, REPL_LRU, WAL_SYNC_FSYNC);
        uint64_t rid = db_alloc_rid(&db);
        db_write_t w = {rid, mkrec(9, 111)};
        db_commit(&db, 1, &w, 1);
        db_checkpoint(&db);
        w.rec = mkrec(9, 222);
        db_commit(&db, 2, &w, 1);
        w.rec = mkrec(9, 333);
        db_commit(&db, 3, &w, 1);
        _exit(0);
    }
    int st;
    waitpid(pid, &st, 0);
    /* WAL now holds [UPDATE 296B][COMMIT 40B] x 2. Cutting 100 bytes removes
     * txn 3's COMMIT and part of its UPDATE: a torn write. */
    char walpath[300];
    snprintf(walpath, sizeof walpath, "%s/plutus.wal", dir);
    int fd = open(walpath, O_RDWR);
    off_t size = lseek(fd, 0, SEEK_END);
    CHECK_EQ(size, 2 * (296 + 40));
    ftruncate(fd, size - 100);
    close(fd);

    db_t db;
    CHECK_EQ(db_open(&db, dir, 4, REPL_LRU, WAL_SYNC_NONE), 0);
    CHECK(db.recovery.torn_tail);
    CHECK_EQ(db.recovery.txns, 1);
    record_t r;
    bool found = false;
    for (int s = 0; s < SLOTS_PER_PAGE; s++) {
        db_read(&db, RID(1, s), &r);
        if (r.type == 1 && r.key == 9) {
            CHECK_EQ(recval(&r), 222); /* complete txn redone, torn txn discarded */
            found = true;
        }
    }
    CHECK(found);
    db_close(&db);
    cleanup();
}

static db_t G;
static void *committer(void *arg)
{
    uint64_t base = (uint64_t)(uintptr_t)arg;
    for (int i = 0; i < 50; i++) {
        uint64_t rid = db_alloc_rid(&G);
        db_write_t w = {rid, mkrec(base * 1000 + (uint64_t)i, i)};
        db_commit(&G, base * 1000 + (uint64_t)i, &w, 1);
    }
    return NULL;
}

TEST(group_commit_batches_fsyncs)
{
    fresh_dir("group");
    CHECK_EQ(db_open(&G, dir, 16, REPL_LRU, WAL_SYNC_FSYNC), 0);
    pthread_t t[8];
    for (int i = 0; i < 8; i++)
        pthread_create(&t[i], NULL, committer, (void *)(uintptr_t)(i + 1));
    for (int i = 0; i < 8; i++)
        pthread_join(t[i], NULL);
    CHECK_EQ(G.wal.commits, 400);
    CHECK(G.wal.flushes < G.wal.commits); /* several commits shared one fsync */
    int count = 0;
    for (uint64_t pid = 1; pid < G.pager.npages; pid++)
        for (int s = 0; s < SLOTS_PER_PAGE; s++) {
            record_t r;
            db_read(&G, RID(pid, s), &r);
            count += r.type == 1;
        }
    CHECK_EQ(count, 400);
    db_close(&G);
    cleanup();
}

int main(void)
{
    t_begin("storage");
    RUN(belady_anomaly_fifo);
    RUN(opt_is_optimal);
    RUN(commit_read_and_reopen);
    RUN(crash_recovery_after_sigkill);
    RUN(torn_wal_tail_is_discarded);
    RUN(group_commit_batches_fsyncs);
    return t_end();
}
