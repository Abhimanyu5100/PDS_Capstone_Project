/* wal.h - write-ahead log with group commit.
 *
 * Every transaction appends its UPDATE records (full after-images of 256-byte
 * slots) followed by a COMMIT record, contiguously, under one mutex. A
 * dedicated flusher thread writes whatever has accumulated and calls fsync()
 * once for the whole batch, then wakes every committer whose COMMIT LSN is now
 * durable. Under load, many commits share one fsync ("group commit"), which is
 * the producer-consumer pattern applied to durability.
 *
 * Each record carries a CRC-32, so a torn write at the tail (power cut halfway
 * through a write) is detected during recovery and the incomplete transaction
 * is discarded. */
#ifndef PLUTUS_WAL_H
#define PLUTUS_WAL_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define WAL_MAGIC 0x57524543u /* "WREC" */
#define WAL_SLOT_IMAGE 256

enum { WAL_UPDATE = 1, WAL_COMMIT = 2 };
typedef enum { WAL_SYNC_NONE, WAL_SYNC_FSYNC, WAL_SYNC_FULL } wal_sync_t;

typedef struct {
    uint32_t magic;
    uint32_t crc; /* over this header (with crc = 0) and the payload */
    uint64_t lsn;
    uint64_t txn;
    uint32_t type;
    uint32_t len; /* payload bytes */
    uint64_t rid;
} wal_hdr_t;

typedef struct {
    uint64_t rid;
    const uint8_t *image; /* WAL_SLOT_IMAGE bytes */
    uint64_t lsn;         /* filled in by wal_append_txn */
} wal_update_t;

typedef struct {
    int fd;
    wal_sync_t sync;
    pthread_mutex_t m;
    pthread_cond_t work_cv, durable_cv;
    uint8_t *buf;
    size_t len, cap;
    uint64_t next_lsn, durable_lsn, buffered_lsn;
    uint32_t buffered_commits;
    pthread_t flusher;
    bool stop, failed;
    /* statistics */
    uint64_t records, bytes, flushes, commits, max_group;
    int64_t fsync_ns;
} wal_t;

int wal_open(wal_t *w, const char *path, wal_sync_t sync, uint64_t next_lsn);
void wal_close(wal_t *w);
/* Appends n updates and a COMMIT atomically; returns the COMMIT's LSN. */
uint64_t wal_append_txn(wal_t *w, uint64_t txn, wal_update_t *ups, int n);
/* Blocks until every record up to lsn is on disk. Returns false if I/O failed. */
bool wal_wait_durable(wal_t *w, uint64_t lsn);
/* Discards the log; the caller must guarantee no commit is in flight. */
int wal_truncate(wal_t *w);
uint64_t wal_next_lsn(wal_t *w);
uint64_t wal_durable_lsn(wal_t *w);

/* Replays committed transactions in log order. fn is called for every UPDATE
 * of every committed transaction. Returns the number of committed transactions,
 * or -1 if the file cannot be read. *max_lsn receives the highest valid LSN and
 * *torn is set when the tail held an incomplete or corrupt record. */
typedef void (*wal_redo_fn)(void *ctx, uint64_t lsn, uint64_t rid, const uint8_t *image);
long wal_replay(const char *path, wal_redo_fn fn, void *ctx, uint64_t *max_lsn, long *updates,
                bool *torn);

const char *wal_sync_name(wal_sync_t s);
bool wal_sync_parse(const char *s, wal_sync_t *out);

#endif
