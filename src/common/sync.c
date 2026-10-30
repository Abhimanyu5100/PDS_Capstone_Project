#include "sync.h"

#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include "util.h"

void deadline_after(struct timespec *ts, int64_t ns)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t t = (int64_t)tv.tv_sec * 1000000000LL + (int64_t)tv.tv_usec * 1000 + ns;
    ts->tv_sec = t / 1000000000LL;
    ts->tv_nsec = t % 1000000000LL;
}

/* ------------------------------------------------------------------ semaphore */

void csem_init(csem_t *s, int value)
{
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->c, NULL);
    s->value = value;
    s->waiters = 0;
}

void csem_destroy(csem_t *s)
{
    pthread_mutex_destroy(&s->m);
    pthread_cond_destroy(&s->c);
}

void csem_wait(csem_t *s)
{
    pthread_mutex_lock(&s->m);
    s->waiters++;
    while (s->value == 0) /* loop: guards against spurious wake-ups */
        pthread_cond_wait(&s->c, &s->m);
    s->waiters--;
    s->value--;
    pthread_mutex_unlock(&s->m);
}

bool csem_timedwait(csem_t *s, int64_t timeout_ns)
{
    struct timespec dl;
    deadline_after(&dl, timeout_ns);
    pthread_mutex_lock(&s->m);
    s->waiters++;
    while (s->value == 0) {
        if (pthread_cond_timedwait(&s->c, &s->m, &dl) == ETIMEDOUT && s->value == 0) {
            s->waiters--;
            pthread_mutex_unlock(&s->m);
            return false;
        }
    }
    s->waiters--;
    s->value--;
    pthread_mutex_unlock(&s->m);
    return true;
}

bool csem_trywait(csem_t *s)
{
    bool ok = false;
    pthread_mutex_lock(&s->m);
    if (s->value > 0) {
        s->value--;
        ok = true;
    }
    pthread_mutex_unlock(&s->m);
    return ok;
}

void csem_post(csem_t *s)
{
    pthread_mutex_lock(&s->m);
    s->value++;
    if (s->waiters)
        pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
}

int csem_value(csem_t *s)
{
    pthread_mutex_lock(&s->m);
    int v = s->value;
    pthread_mutex_unlock(&s->m);
    return v;
}

/* ------------------------------------------------------------------ rwlock */

void rw_init(rwlock_t *l, rw_mode_t mode)
{
    memset(l, 0, sizeof *l);
    pthread_mutex_init(&l->m, NULL);
    pthread_cond_init(&l->cv, NULL);
    l->mode = mode;
}

void rw_set_mode(rwlock_t *l, rw_mode_t mode)
{
    pthread_mutex_lock(&l->m);
    l->mode = mode;
    pthread_cond_broadcast(&l->cv);
    pthread_mutex_unlock(&l->m);
}

static bool reader_may_enter(rwlock_t *l, uint64_t ticket)
{
    if (l->writer)
        return false;
    switch (l->mode) {
    case RW_READER_PREF: return true;
    case RW_WRITER_PREF: return l->waiting_writers == 0;
    case RW_FAIR: return ticket == l->serving;
    }
    return true;
}

static bool writer_may_enter(rwlock_t *l, uint64_t ticket)
{
    if (l->writer || l->readers)
        return false;
    if (l->mode == RW_FAIR)
        return ticket == l->serving;
    if (l->mode == RW_READER_PREF)
        return l->waiting_readers == 0;
    return true;
}

void rw_rdlock(rwlock_t *l)
{
    int64_t t0 = now_ns();
    pthread_mutex_lock(&l->m);
    uint64_t ticket = l->next_ticket++;
    l->waiting_readers++;
    while (!reader_may_enter(l, ticket))
        pthread_cond_wait(&l->cv, &l->m);
    l->waiting_readers--;
    l->readers++;
    l->reads++;
    if (l->mode == RW_FAIR) {
        l->serving++;
        pthread_cond_broadcast(&l->cv); /* let the next ticket (maybe a reader) in */
    }
    int64_t w = now_ns() - t0;
    if (w > l->max_read_wait_ns)
        l->max_read_wait_ns = w;
    pthread_mutex_unlock(&l->m);
}

void rw_rdunlock(rwlock_t *l)
{
    pthread_mutex_lock(&l->m);
    if (--l->readers == 0)
        pthread_cond_broadcast(&l->cv);
    pthread_mutex_unlock(&l->m);
}

void rw_wrlock(rwlock_t *l)
{
    int64_t t0 = now_ns();
    pthread_mutex_lock(&l->m);
    uint64_t ticket = l->next_ticket++;
    l->waiting_writers++;
    while (!writer_may_enter(l, ticket))
        pthread_cond_wait(&l->cv, &l->m);
    l->waiting_writers--;
    l->writer = true;
    l->writes++;
    if (l->mode == RW_FAIR)
        l->serving++;
    int64_t w = now_ns() - t0;
    if (w > l->max_write_wait_ns)
        l->max_write_wait_ns = w;
    pthread_mutex_unlock(&l->m);
}

void rw_wrunlock(rwlock_t *l)
{
    pthread_mutex_lock(&l->m);
    l->writer = false;
    pthread_cond_broadcast(&l->cv);
    pthread_mutex_unlock(&l->m);
}

const char *rw_mode_name(rw_mode_t m)
{
    switch (m) {
    case RW_READER_PREF: return "reader-pref";
    case RW_WRITER_PREF: return "writer-pref";
    case RW_FAIR: return "fair";
    }
    return "?";
}

bool rw_mode_parse(const char *s, rw_mode_t *out)
{
    if (!strcmp(s, "reader-pref") || !strcmp(s, "reader"))
        *out = RW_READER_PREF;
    else if (!strcmp(s, "writer-pref") || !strcmp(s, "writer"))
        *out = RW_WRITER_PREF;
    else if (!strcmp(s, "fair"))
        *out = RW_FAIR;
    else
        return false;
    return true;
}

void rw_get_stats(rwlock_t *l, rw_mode_t *mode, uint64_t *reads, uint64_t *writes, int64_t *max_read_wait_ns,
                  int64_t *max_write_wait_ns)
{
    pthread_mutex_lock(&l->m);
    *mode = l->mode;
    *reads = l->reads;
    *writes = l->writes;
    *max_read_wait_ns = l->max_read_wait_ns;
    *max_write_wait_ns = l->max_write_wait_ns;
    pthread_mutex_unlock(&l->m);
}
