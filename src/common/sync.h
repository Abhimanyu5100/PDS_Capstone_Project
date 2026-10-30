/* sync.h - synchronisation primitives built from a mutex and condition variables.
 *
 * macOS does not implement unnamed POSIX semaphores (sem_init returns ENOSYS),
 * so the counting semaphore is built from first principles. The readers-writers
 * lock is hand-written too, so the starvation behaviour of each policy can be
 * switched at run time and demonstrated. */
#ifndef PLUTUS_SYNC_H
#define PLUTUS_SYNC_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* ---- counting semaphore ---- */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t c;
    int value;
    int waiters;
} csem_t;

void csem_init(csem_t *s, int value);
void csem_destroy(csem_t *s);
void csem_wait(csem_t *s);                       /* P() */
bool csem_timedwait(csem_t *s, int64_t timeout_ns); /* false on timeout */
bool csem_trywait(csem_t *s);
void csem_post(csem_t *s);                       /* V() */
int csem_value(csem_t *s);

/* ---- readers-writers lock ----
 * RW_READER_PREF: readers enter while no writer holds the lock (writers can starve)
 * RW_WRITER_PREF: a waiting writer blocks new readers (readers can starve)
 * RW_FAIR:        FIFO ticket order, so neither side starves */
typedef enum { RW_READER_PREF, RW_WRITER_PREF, RW_FAIR } rw_mode_t;

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t cv;
    rw_mode_t mode;
    int readers;          /* active readers */
    bool writer;          /* active writer */
    int waiting_writers;
    int waiting_readers;
    uint64_t next_ticket; /* RW_FAIR */
    uint64_t serving;     /* RW_FAIR: lowest ticket not yet admitted */
    /* statistics */
    uint64_t reads, writes;
    int64_t max_read_wait_ns, max_write_wait_ns;
} rwlock_t;

void rw_init(rwlock_t *l, rw_mode_t mode);
void rw_set_mode(rwlock_t *l, rw_mode_t mode);
void rw_rdlock(rwlock_t *l);
void rw_rdunlock(rwlock_t *l);
void rw_wrlock(rwlock_t *l);
void rw_wrunlock(rwlock_t *l);
/* consistent copy of the mode and statistics (taken under the lock) */
void rw_get_stats(rwlock_t *l, rw_mode_t *mode, uint64_t *reads, uint64_t *writes, int64_t *max_read_wait_ns,
                  int64_t *max_write_wait_ns);
const char *rw_mode_name(rw_mode_t m);
bool rw_mode_parse(const char *s, rw_mode_t *out);

/* absolute CLOCK_REALTIME deadline for pthread_cond_timedwait */
void deadline_after(struct timespec *ts, int64_t ns);

#endif
