/* pager.h - buffer pool: demand paging of the database file.
 *
 * The database file is split into 4 KiB pages. Only `nframes` of them are kept
 * in memory. Touching a page that is not resident is a page fault: a victim
 * frame is chosen by the replacement policy, written back if dirty (after the
 * WAL rule is checked), and the wanted page is read in. Pinned frames are never
 * evicted; if every frame is pinned, fetchers block until one is unpinned.
 *
 * Replacement policies, switchable at run time: FIFO, LRU and CLOCK (second
 * chance). Each page carries a CRC-32 that is checked on every read, so torn
 * or corrupted pages are detected. */
#ifndef PLUTUS_PAGER_H
#define PLUTUS_PAGER_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "../common/util.h"
#include "wal.h"

#define PAGE_SIZE 4096
#define PAGE_HDR 64
#define SLOT_SIZE 256
#define SLOTS_PER_PAGE ((PAGE_SIZE - PAGE_HDR) / SLOT_SIZE) /* 15 */
#define PAGE_MAGIC 0x504C5047u /* "PLPG" */

typedef enum { REPL_FIFO, REPL_LRU, REPL_CLOCK, REPL_POLICY_COUNT } repl_policy_t;

typedef struct {
    uint32_t magic;
    uint32_t crc;
    uint64_t page_id;
    uint64_t page_lsn; /* LSN of the latest update applied to this page */
    uint8_t reserved[PAGE_HDR - 24];
} page_hdr_t;

typedef struct {
    uint64_t page_id;
    bool valid, dirty, ref;
    int pin;
    uint64_t loaded_at, last_used; /* logical clock values */
    pthread_mutex_t latch;          /* protects data */
    uint8_t *data;
} frame_t;

typedef struct {
    int fd;
    pthread_mutex_t m;
    pthread_cond_t unpinned_cv;
    frame_t *frames;
    int nframes;
    repl_policy_t policy;
    int clock_hand;
    uint64_t tick;
    uint64_t npages; /* includes page 0 (superblock) */
    wal_t *wal;
    /* statistics */
    uint64_t hits, misses, evictions, writebacks, crc_errors, pin_waits;
} pager_t;

int pager_open(pager_t *p, const char *path, int nframes, repl_policy_t policy, wal_t *wal);
void pager_close(pager_t *p);
frame_t *pager_fetch(pager_t *p, uint64_t page_id); /* returns a pinned frame */
void pager_unpin(pager_t *p, frame_t *f, bool dirtied);
uint64_t pager_new_page(pager_t *p);
int pager_flush_all(pager_t *p);
void pager_set_policy(pager_t *p, repl_policy_t pol);
void pager_render(pager_t *p, sbuf_t *out);

static inline page_hdr_t *page_hdr(frame_t *f) { return (page_hdr_t *)f->data; }
static inline uint8_t *page_slot(frame_t *f, int slot) { return f->data + PAGE_HDR + slot * SLOT_SIZE; }

const char *repl_policy_name(repl_policy_t p);
bool repl_policy_parse(const char *s, repl_policy_t *out);

/* Offline page-replacement simulator (includes OPT, which needs the future). */
typedef enum { PSIM_FIFO, PSIM_LRU, PSIM_CLOCK, PSIM_OPT, PSIM_COUNT } psim_policy_t;
int page_sim(const int *refs, int n, int frames, psim_policy_t pol, sbuf_t *trace);
const char *psim_name(psim_policy_t p);

#endif
