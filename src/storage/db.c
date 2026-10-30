#include "db.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../common/crypto.h"

#define SB_MAGIC 0x504C5342u /* "PLSB" */

_Static_assert(sizeof(record_t) == SLOT_SIZE, "record must fill a slot");
_Static_assert(sizeof(page_hdr_t) == PAGE_HDR, "page header size");

static void path_join(char *out, size_t n, const char *dir, const char *file)
{
    snprintf(out, n, "%s/%s", dir, file);
}

static int sb_write(db_t *db)
{
    db->sb.magic = SB_MAGIC;
    db->sb.crc = 0;
    db->sb.crc = crc32(&db->sb, sizeof db->sb);
    uint8_t page[PAGE_SIZE] = {0};
    memcpy(page, &db->sb, sizeof db->sb);
    if (pwrite(db->pager.fd, page, PAGE_SIZE, 0) != PAGE_SIZE)
        return -1;
    return fsync(db->pager.fd);
}

static void redo_cb(void *ctx, uint64_t lsn, uint64_t rid, const uint8_t *image)
{
    db_t *db = ctx;
    if (RID_PAGE(rid) == 0)
        return;
    frame_t *f = pager_fetch(&db->pager, RID_PAGE(rid));
    pthread_mutex_lock(&f->latch);
    bool apply = page_hdr(f)->page_lsn < lsn; /* idempotent: skip if already on the page */
    if (apply) {
        memcpy(page_slot(f, RID_SLOT(rid)), image, SLOT_SIZE);
        page_hdr(f)->page_lsn = lsn;
    }
    pthread_mutex_unlock(&f->latch);
    pager_unpin(&db->pager, f, apply);
}

static void free_push(db_t *db, uint64_t rid)
{
    if (db->nfree == db->capfree) {
        db->capfree = db->capfree ? db->capfree * 2 : 256;
        db->free_rids = realloc(db->free_rids, sizeof(uint64_t) * db->capfree);
        if (!db->free_rids)
            die("out of memory");
    }
    db->free_rids[db->nfree++] = rid;
}

int db_open(db_t *db, const char *dir, int frames, repl_policy_t repl, wal_sync_t sync)
{
    memset(db, 0, sizeof *db);
    snprintf(db->dir, sizeof db->dir, "%s", dir);
    mkdir(dir, 0700);
    char dbpath[512], walpath[512];
    path_join(dbpath, sizeof dbpath, dir, "plutus.db");
    path_join(walpath, sizeof walpath, dir, "plutus.wal");
    rw_init(&db->gate, RW_WRITER_PREF);
    pthread_mutex_init(&db->space_m, NULL);
    pthread_mutex_init(&db->stat_m, NULL);
    if (pager_open(&db->pager, dbpath, frames, repl, NULL) != 0)
        return -1;

    int64_t t0 = now_ns();
    uint8_t page[PAGE_SIZE];
    ssize_t r = pread(db->pager.fd, page, PAGE_SIZE, 0);
    if (r <= 0) {
        db->recovery.fresh = true;
        db->sb = (superblock_t){.version = 1, .page_size = PAGE_SIZE, .ckpt_lsn = 0, .next_lsn = 1,
                                .created = wall_time()};
        if (sb_write(db) != 0)
            return -1;
    } else {
        memcpy(&db->sb, page, sizeof db->sb);
        uint32_t want = db->sb.crc;
        db->sb.crc = 0;
        if (db->sb.magic != SB_MAGIC || crc32(&db->sb, sizeof db->sb) != want) {
            LOGE("superblock is corrupt; refusing to open %s", dbpath);
            errno = EINVAL;
            return -1;
        }
        db->sb.crc = want;
    }

    /* REDO pass: replay every committed transaction left in the log */
    uint64_t max_lsn = 0;
    long updates = 0;
    bool torn = false;
    long txns = wal_replay(walpath, redo_cb, db, &max_lsn, &updates, &torn);
    if (txns < 0)
        return -1;
    db->recovery.txns = txns;
    db->recovery.updates = updates;
    db->recovery.torn_tail = torn;
    uint64_t next = db->sb.next_lsn > max_lsn + 1 ? db->sb.next_lsn : max_lsn + 1;
    db->recovery.ckpt_lsn = db->sb.ckpt_lsn;
    db->recovery.next_lsn = next;

    if (wal_open(&db->wal, walpath, sync, next) != 0)
        return -1;
    db->pager.wal = &db->wal;

    /* free-slot map (scans every page through the buffer pool) */
    for (uint64_t pid = 1; pid < db->pager.npages; pid++) {
        frame_t *f = pager_fetch(&db->pager, pid);
        pthread_mutex_lock(&f->latch);
        for (int s = SLOTS_PER_PAGE - 1; s >= 0; s--)
            if (((record_t *)page_slot(f, s))->type == 0)
                free_push(db, RID(pid, s));
        pthread_mutex_unlock(&f->latch);
        pager_unpin(&db->pager, f, false);
    }
    db->recovery.ns = now_ns() - t0;
    if (txns > 0 || torn)
        LOGI("recovery: replayed %ld committed transactions (%ld page updates)%s in %.1f ms", txns,
             updates, torn ? ", discarded a torn log tail" : "", (double)db->recovery.ns / 1e6);
    /* a checkpoint makes the recovered state durable and empties the log */
    return db_checkpoint(db);
}

int db_checkpoint(db_t *db)
{
    rw_wrlock(&db->gate);
    int rc = pager_flush_all(&db->pager);
    if (rc == 0) {
        uint64_t next = wal_next_lsn(&db->wal);
        db->sb.ckpt_lsn = next - 1;
        db->sb.next_lsn = next;
        db->sb.checkpoints++;
        rc = sb_write(db);
    }
    if (rc == 0)
        rc = wal_truncate(&db->wal);
    rw_wrunlock(&db->gate);
    pthread_mutex_lock(&db->stat_m);
    db->checkpoints++;
    pthread_mutex_unlock(&db->stat_m);
    if (rc != 0)
        LOGE("checkpoint failed: %s", strerror(errno));
    return rc;
}

void db_close(db_t *db)
{
    db_checkpoint(db);
    wal_close(&db->wal);
    pager_close(&db->pager);
    free(db->free_rids);
    db->free_rids = NULL;
}

int db_read(db_t *db, uint64_t rid, record_t *out)
{
    if (RID_PAGE(rid) == 0 || RID_SLOT(rid) >= SLOTS_PER_PAGE)
        return -1;
    frame_t *f = pager_fetch(&db->pager, RID_PAGE(rid));
    pthread_mutex_lock(&f->latch);
    memcpy(out, page_slot(f, RID_SLOT(rid)), SLOT_SIZE);
    pthread_mutex_unlock(&f->latch);
    pager_unpin(&db->pager, f, false);
    return 0;
}

uint64_t db_alloc_rid(db_t *db)
{
    pthread_mutex_lock(&db->space_m);
    if (db->nfree == 0) {
        uint64_t pid = pager_new_page(&db->pager);
        for (int s = SLOTS_PER_PAGE - 1; s >= 0; s--)
            free_push(db, RID(pid, s));
    }
    uint64_t rid = db->free_rids[--db->nfree];
    pthread_mutex_unlock(&db->space_m);
    return rid;
}

void db_release_rid(db_t *db, uint64_t rid)
{
    pthread_mutex_lock(&db->space_m);
    free_push(db, rid);
    pthread_mutex_unlock(&db->space_m);
}

int db_commit(db_t *db, uint64_t txn, const db_write_t *w, int n)
{
    enum { MAXW = 16 };
    if (n > MAXW)
        return -1;
    wal_update_t ups[MAXW];
    for (int i = 0; i < n; i++)
        ups[i] = (wal_update_t){w[i].rid, (const uint8_t *)&w[i].rec, 0};
    rw_rdlock(&db->gate);
    uint64_t lsn = wal_append_txn(&db->wal, txn, ups, n);
    if (!wal_wait_durable(&db->wal, lsn)) {
        rw_rdunlock(&db->gate);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        frame_t *f = pager_fetch(&db->pager, RID_PAGE(w[i].rid));
        pthread_mutex_lock(&f->latch);
        memcpy(page_slot(f, RID_SLOT(w[i].rid)), &w[i].rec, SLOT_SIZE);
        if (ups[i].lsn > page_hdr(f)->page_lsn)
            page_hdr(f)->page_lsn = ups[i].lsn;
        pthread_mutex_unlock(&f->latch);
        pager_unpin(&db->pager, f, true);
    }
    rw_rdunlock(&db->gate);
    pthread_mutex_lock(&db->stat_m);
    db->commits++;
    pthread_mutex_unlock(&db->stat_m);
    return 0;
}

void db_scan(db_t *db, db_scan_fn fn, void *ctx)
{
    pthread_mutex_lock(&db->pager.m);
    uint64_t npages = db->pager.npages;
    pthread_mutex_unlock(&db->pager.m);
    record_t rec;
    for (uint64_t pid = 1; pid < npages; pid++) {
        frame_t *f = pager_fetch(&db->pager, pid);
        for (int s = 0; s < SLOTS_PER_PAGE; s++) {
            pthread_mutex_lock(&f->latch);
            memcpy(&rec, page_slot(f, s), SLOT_SIZE);
            pthread_mutex_unlock(&f->latch);
            if (rec.type != 0)
                fn(ctx, RID(pid, s), &rec);
        }
        pager_unpin(&db->pager, f, false);
    }
}

void db_render(db_t *db, sbuf_t *out)
{
    wal_t *w = &db->wal;
    pthread_mutex_lock(&w->m);
    uint64_t recs = w->records, bytes = w->bytes, flushes = w->flushes, commits = w->commits;
    uint64_t maxg = w->max_group, next = w->next_lsn, durable = w->durable_lsn;
    int64_t fs = w->fsync_ns;
    wal_sync_t sync = w->sync;
    pthread_mutex_unlock(&w->m);
    pthread_mutex_lock(&db->stat_m);
    uint64_t ck = db->checkpoints;
    pthread_mutex_unlock(&db->stat_m);
    sb_printf(out, "Write-ahead log (sync=%s): next LSN %llu, durable LSN %llu\n", wal_sync_name(sync),
              (unsigned long long)next, (unsigned long long)durable);
    sb_printf(out,
              "  records %llu | bytes %llu | commits %llu | flushes %llu | avg commits per fsync "
              "%.2f (max %llu) | avg fsync %.2f ms\n",
              (unsigned long long)recs, (unsigned long long)bytes, (unsigned long long)commits,
              (unsigned long long)flushes, flushes ? (double)commits / (double)flushes : 0.0,
              (unsigned long long)maxg, flushes ? (double)fs / (double)flushes / 1e6 : 0.0);
    rw_rdlock(&db->gate); /* the superblock changes only under the write side */
    uint64_t ckpt = db->sb.ckpt_lsn;
    rw_rdunlock(&db->gate);
    sb_printf(out, "  checkpoints %llu | last checkpoint LSN %llu\n", (unsigned long long)ck, (unsigned long long)ckpt);
    sb_printf(out, "  last recovery: %s, %ld txns / %ld updates replayed%s, %.1f ms\n",
              db->recovery.fresh ? "fresh database" : "existing database", db->recovery.txns,
              db->recovery.updates, db->recovery.torn_tail ? ", torn tail discarded" : "",
              (double)db->recovery.ns / 1e6);
}
