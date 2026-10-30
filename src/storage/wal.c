#include "wal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/crypto.h"
#include "../common/proto.h"
#include "../common/util.h"

static void *flusher_main(void *arg);

int wal_open(wal_t *w, const char *path, wal_sync_t sync, uint64_t next_lsn)
{
    memset(w, 0, sizeof *w);
    w->fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (w->fd < 0)
        return -1;
    w->sync = sync;
    w->next_lsn = next_lsn ? next_lsn : 1;
    w->durable_lsn = w->buffered_lsn = w->next_lsn - 1;
    w->cap = 1 << 16;
    w->buf = malloc(w->cap);
    if (!w->buf)
        die("out of memory");
    pthread_mutex_init(&w->m, NULL);
    pthread_cond_init(&w->work_cv, NULL);
    pthread_cond_init(&w->durable_cv, NULL);
    if (pthread_create(&w->flusher, NULL, flusher_main, w) != 0)
        return -1;
    return 0;
}

void wal_close(wal_t *w)
{
    pthread_mutex_lock(&w->m);
    w->stop = true;
    pthread_cond_signal(&w->work_cv);
    pthread_mutex_unlock(&w->m);
    pthread_join(w->flusher, NULL);
    close(w->fd);
    free(w->buf);
    w->buf = NULL;
}

static void buf_put(wal_t *w, const void *p, size_t n)
{
    while (w->len + n > w->cap) {
        w->cap *= 2;
        w->buf = realloc(w->buf, w->cap);
        if (!w->buf)
            die("out of memory");
    }
    memcpy(w->buf + w->len, p, n);
    w->len += n;
}

static void put_record(wal_t *w, uint64_t lsn, uint64_t txn, uint32_t type, uint64_t rid,
                       const uint8_t *payload, uint32_t len)
{
    wal_hdr_t h = {WAL_MAGIC, 0, lsn, txn, type, len, rid};
    uint32_t c = crc32(&h, sizeof h);
    if (len)
        c = crc32_update(c, payload, len);
    h.crc = c;
    buf_put(w, &h, sizeof h);
    if (len)
        buf_put(w, payload, len);
    w->records++;
    w->bytes += sizeof h + len;
}

uint64_t wal_append_txn(wal_t *w, uint64_t txn, wal_update_t *ups, int n)
{
    pthread_mutex_lock(&w->m);
    for (int i = 0; i < n; i++) {
        ups[i].lsn = w->next_lsn++;
        put_record(w, ups[i].lsn, txn, WAL_UPDATE, ups[i].rid, ups[i].image, WAL_SLOT_IMAGE);
    }
    uint64_t commit_lsn = w->next_lsn++;
    put_record(w, commit_lsn, txn, WAL_COMMIT, 0, NULL, 0);
    w->buffered_lsn = commit_lsn;
    w->buffered_commits++;
    w->commits++;
    pthread_cond_signal(&w->work_cv);
    pthread_mutex_unlock(&w->m);
    return commit_lsn;
}

static void *flusher_main(void *arg)
{
    wal_t *w = arg;
    uint8_t *spare = malloc(1 << 16);
    size_t spare_cap = 1 << 16;
    pthread_mutex_lock(&w->m);
    for (;;) {
        while (w->len == 0 && !w->stop)
            pthread_cond_wait(&w->work_cv, &w->m);
        if (w->len == 0 && w->stop)
            break;
        /* swap buffers so committers keep appending while we do I/O */
        uint8_t *batch = w->buf;
        size_t batch_len = w->len, batch_cap = w->cap;
        uint64_t batch_lsn = w->buffered_lsn;
        uint32_t group = w->buffered_commits;
        w->buf = spare;
        w->cap = spare_cap;
        w->len = 0;
        w->buffered_commits = 0;
        pthread_mutex_unlock(&w->m);

        bool ok = conn_write_all(w->fd, batch, batch_len);
        int64_t t0 = now_ns();
        if (ok && w->sync == WAL_SYNC_FSYNC)
            ok = fsync(w->fd) == 0;
        else if (ok && w->sync == WAL_SYNC_FULL)
            ok = fcntl(w->fd, F_FULLFSYNC) == 0; /* macOS: flush the drive cache too */
        int64_t dt = now_ns() - t0;

        pthread_mutex_lock(&w->m);
        spare = batch;
        spare_cap = batch_cap;
        if (!ok) {
            w->failed = true;
            LOGE("WAL write failed: %s", strerror(errno));
        } else {
            w->durable_lsn = batch_lsn;
        }
        w->flushes++;
        w->fsync_ns += dt;
        if (group > w->max_group)
            w->max_group = group;
        pthread_cond_broadcast(&w->durable_cv);
    }
    pthread_mutex_unlock(&w->m);
    free(spare);
    return NULL;
}

bool wal_wait_durable(wal_t *w, uint64_t lsn)
{
    pthread_mutex_lock(&w->m);
    while (w->durable_lsn < lsn && !w->failed)
        pthread_cond_wait(&w->durable_cv, &w->m);
    bool ok = !w->failed;
    pthread_mutex_unlock(&w->m);
    return ok;
}

int wal_truncate(wal_t *w)
{
    pthread_mutex_lock(&w->m);
    while (w->len > 0 || w->durable_lsn < w->buffered_lsn) {
        pthread_cond_signal(&w->work_cv);
        pthread_cond_wait(&w->durable_cv, &w->m);
    }
    int rc = ftruncate(w->fd, 0);
    if (rc == 0 && w->sync != WAL_SYNC_NONE)
        rc = fsync(w->fd);
    pthread_mutex_unlock(&w->m);
    return rc;
}

uint64_t wal_durable_lsn(wal_t *w)
{
    pthread_mutex_lock(&w->m);
    uint64_t v = w->durable_lsn;
    pthread_mutex_unlock(&w->m);
    return v;
}

uint64_t wal_next_lsn(wal_t *w)
{
    pthread_mutex_lock(&w->m);
    uint64_t v = w->next_lsn;
    pthread_mutex_unlock(&w->m);
    return v;
}

long wal_replay(const char *path, wal_redo_fn fn, void *ctx, uint64_t *max_lsn, long *updates,
                bool *torn)
{
    *max_lsn = 0;
    *torn = false;
    if (updates)
        *updates = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return errno == ENOENT ? 0 : -1;
    long committed = 0;
    /* a transaction's records are contiguous, so one pending buffer suffices */
    enum { MAXP = 64 };
    struct {
        uint64_t lsn, rid;
        uint8_t img[WAL_SLOT_IMAGE];
    } *pend = malloc(sizeof(*pend) * MAXP);
    int npend = 0;
    uint64_t cur_txn = 0;
    for (;;) {
        wal_hdr_t h;
        size_t r = fread(&h, 1, sizeof h, f);
        if (r == 0)
            break;
        if (r != sizeof h || h.magic != WAL_MAGIC || h.len > WAL_SLOT_IMAGE) {
            *torn = true;
            break;
        }
        uint8_t payload[WAL_SLOT_IMAGE];
        if (h.len && fread(payload, 1, h.len, f) != h.len) {
            *torn = true;
            break;
        }
        uint32_t want = h.crc;
        h.crc = 0;
        uint32_t c = crc32(&h, sizeof h);
        if (h.len)
            c = crc32_update(c, payload, h.len);
        if (c != want) {
            *torn = true;
            break;
        }
        if (h.lsn > *max_lsn)
            *max_lsn = h.lsn;
        if (h.txn != cur_txn) {
            npend = 0; /* previous transaction never committed */
            cur_txn = h.txn;
        }
        if (h.type == WAL_UPDATE && h.len == WAL_SLOT_IMAGE && npend < MAXP) {
            pend[npend].lsn = h.lsn;
            pend[npend].rid = h.rid;
            memcpy(pend[npend].img, payload, WAL_SLOT_IMAGE);
            npend++;
        } else if (h.type == WAL_COMMIT) {
            for (int i = 0; i < npend; i++)
                fn(ctx, pend[i].lsn, pend[i].rid, pend[i].img);
            if (updates)
                *updates += npend;
            committed++;
            npend = 0;
            cur_txn = 0;
        }
    }
    free(pend);
    fclose(f);
    return committed;
}

const char *wal_sync_name(wal_sync_t s)
{
    static const char *n[] = {"none", "fsync", "full"};
    return s <= WAL_SYNC_FULL ? n[s] : "?";
}

bool wal_sync_parse(const char *s, wal_sync_t *out)
{
    for (int i = 0; i <= WAL_SYNC_FULL; i++)
        if (!strcmp(s, wal_sync_name((wal_sync_t)i))) {
            *out = (wal_sync_t)i;
            return true;
        }
    return false;
}
