/* lockmgr.h - per-account exclusive locks with deadlock handling.
 *
 * A transfer has to hold two account locks at once. Two tellers running
 * A->B and B->A, each holding its first lock and waiting for the second, is a
 * deadlock (all four Coffman conditions hold). A ring of transfers
 * A->B->C->...->A is the dining-philosophers problem.
 *
 * Policies (switchable while the bank runs):
 *   none     lock in request order, never resolve        -> can hang (on purpose)
 *   detect   lock in request order; a detector thread builds the wait-for graph,
 *            finds cycles with DFS and aborts the youngest transaction (victim),
 *            which rolls back and retries
 *   ordered  PREVENTION: acquire locks in ascending account order, so circular
 *            wait is impossible
 *   trylock  PREVENTION: break hold-and-wait; if a later lock is busy, release
 *            everything and back off for a random, exponentially growing time
 *
 * The detector runs under every policy. Under `none` it reports cycles but
 * leaves them alone, so `plutus-admin waitgraph` can show a live deadlock. */
#ifndef PLUTUS_LOCKMGR_H
#define PLUTUS_LOCKMGR_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "../common/util.h"

typedef enum { DL_NONE, DL_DETECT, DL_ORDERED, DL_TRYLOCK, DL_POLICY_COUNT } dl_policy_t;

#define TXN_MAX_LOCKS 8
#define LM_BUCKETS 1024

enum { LM_OK = 0, LM_DEADLOCK = -1 };

typedef struct txn {
    uint64_t id;
    int64_t start_ns;
    const char *label;
    uint64_t held[TXN_MAX_LOCKS];
    int nheld;
    uint64_t waiting_for; /* resource id, 0 = not waiting */
    bool victim;          /* chosen by the detector */
    int restarts;
    struct txn *prev, *next; /* active-transaction registry */
} txn_t;

typedef struct lock_entry {
    uint64_t rid;
    txn_t *owner;
    int waiters;
    pthread_cond_t cv;
    struct lock_entry *next;
} lock_entry_t;

typedef struct {
    pthread_mutex_t m;
    lock_entry_t *buckets[LM_BUCKETS];
    txn_t *active;
    int nactive;
    dl_policy_t policy;
    int64_t hold_ns; /* think time between successive acquisitions (demo knob) */
    /* detector thread */
    pthread_t detector;
    bool detector_running, stop;
    int64_t detect_interval_ns;
    /* statistics */
    uint64_t acquisitions, contended, deadlocks_found, victims, backoffs, detector_passes;
    char last_cycle[256];
} lockmgr_t;

void lm_init(lockmgr_t *lm, dl_policy_t policy);
void lm_start_detector(lockmgr_t *lm, int64_t interval_ns);
void lm_stop(lockmgr_t *lm);
void lm_set_policy(lockmgr_t *lm, dl_policy_t p);

void txn_begin(lockmgr_t *lm, txn_t *t, uint64_t id, const char *label);
void txn_end(lockmgr_t *lm, txn_t *t); /* releases every lock and unregisters */

/* Acquires all of rids[0..n) according to the current policy. On LM_DEADLOCK
 * the transaction was chosen as a victim: it holds no locks and should retry. */
int lm_lock_set(lockmgr_t *lm, txn_t *t, const uint64_t *rids, int n);
void lm_release_all(lockmgr_t *lm, txn_t *t);

/* One detection pass. Returns the number of cycles found. Resolves them
 * (aborts victims) only when `resolve` is true. */
int lm_detect(lockmgr_t *lm, bool resolve);
void lm_render(lockmgr_t *lm, sbuf_t *out);

typedef struct {
    dl_policy_t policy;
    uint64_t acquisitions, contended, deadlocks_found, victims, backoffs;
} lm_stats_t;
void lm_get_stats(lockmgr_t *lm, lm_stats_t *out);

const char *dl_policy_name(dl_policy_t p);
bool dl_policy_parse(const char *s, dl_policy_t *out);

#endif
