/* db.h - crash-safe record store on top of the pager and the WAL.
 *
 * Records are fixed 256-byte slots addressed by a RID (page << 4 | slot).
 * Commit protocol (redo-only logging, no-steal):
 *   1. append after-images + COMMIT to the WAL
 *   2. wait until the COMMIT is durable (group commit)
 *   3. apply the after-images to the buffer-pool pages and stamp page_lsn
 * Uncommitted data never reaches a page, so recovery needs no undo: it replays
 * committed after-images whose LSN is newer than the page's page_lsn.
 *
 * Checkpoint: flush every dirty page, record the checkpoint LSN in the
 * superblock, then truncate the WAL. A readers-writers lock (writer preference,
 * so checkpoints cannot starve) lets commits run concurrently while keeping
 * them out of a checkpoint. */
#ifndef PLUTUS_DB_H
#define PLUTUS_DB_H

#include <stdbool.h>
#include <stdint.h>

#include "../common/sync.h"
#include "pager.h"
#include "wal.h"

#define RID(page, slot) (((uint64_t)(page) << 4) | (uint64_t)(slot))
#define RID_PAGE(r) ((r) >> 4)
#define RID_SLOT(r) ((int)((r)&15))

typedef struct {
    uint8_t type; /* 0 = free slot; the bank layer defines the others */
    uint8_t flags;
    uint16_t version;
    uint32_t pad;
    uint64_t key;   /* account / FD / loan / policy number */
    uint64_t owner; /* owning account number */
    uint8_t body[SLOT_SIZE - 24];
} record_t;

typedef struct {
    uint32_t magic, crc;
    uint32_t version, page_size;
    uint64_t ckpt_lsn, next_lsn;
    int64_t created;
    uint64_t checkpoints;
} superblock_t;

typedef struct {
    uint64_t rid;
    record_t rec;
} db_write_t;

typedef struct {
    long txns, updates;
    bool torn_tail;
    bool fresh;
    int64_t ns;
    uint64_t ckpt_lsn, next_lsn;
} db_recovery_t;

typedef struct {
    char dir[400];
    pager_t pager;
    wal_t wal;
    rwlock_t gate;
    superblock_t sb;
    pthread_mutex_t space_m;
    uint64_t *free_rids;
    size_t nfree, capfree;
    db_recovery_t recovery;
    uint64_t commits, checkpoints;
    pthread_mutex_t stat_m;
} db_t;

int db_open(db_t *db, const char *dir, int frames, repl_policy_t repl, wal_sync_t sync);
void db_close(db_t *db);
int db_read(db_t *db, uint64_t rid, record_t *out);
uint64_t db_alloc_rid(db_t *db);
void db_release_rid(db_t *db, uint64_t rid); /* give back an unused reservation */
int db_commit(db_t *db, uint64_t txn, const db_write_t *w, int n);
int db_checkpoint(db_t *db);
typedef void (*db_scan_fn)(void *ctx, uint64_t rid, const record_t *rec);
void db_scan(db_t *db, db_scan_fn fn, void *ctx);
void db_render(db_t *db, sbuf_t *out);

#endif
