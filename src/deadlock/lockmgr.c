#include "lockmgr.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/sync.h"

static lock_entry_t *entry_get(lockmgr_t *lm, uint64_t rid)
{
    unsigned b = (unsigned)((rid * 0x9E3779B97F4A7C15ull) >> 54) % LM_BUCKETS;
    for (lock_entry_t *e = lm->buckets[b]; e; e = e->next)
        if (e->rid == rid)
            return e;
    lock_entry_t *e = calloc(1, sizeof *e);
    if (!e)
        die("out of memory");
    e->rid = rid;
    pthread_cond_init(&e->cv, NULL);
    e->next = lm->buckets[b];
    lm->buckets[b] = e;
    return e;
}

void lm_init(lockmgr_t *lm, dl_policy_t policy)
{
    memset(lm, 0, sizeof *lm);
    pthread_mutex_init(&lm->m, NULL);
    lm->policy = policy;
    lm->detect_interval_ns = 50 * 1000000LL;
}

static void *detector_main(void *arg)
{
    lockmgr_t *lm = arg;
    for (;;) {
        pthread_mutex_lock(&lm->m);
        bool stop = lm->stop;
        int64_t iv = lm->detect_interval_ns;
        pthread_mutex_unlock(&lm->m);
        if (stop)
            break;
        sleep_us(iv / 1000);
        pthread_mutex_lock(&lm->m);
        bool resolve = lm->policy == DL_DETECT;
        pthread_mutex_unlock(&lm->m);
        lm_detect(lm, resolve);
    }
    return NULL;
}

void lm_start_detector(lockmgr_t *lm, int64_t interval_ns)
{
    lm->detect_interval_ns = interval_ns;
    lm->detector_running = pthread_create(&lm->detector, NULL, detector_main, lm) == 0;
}

void lm_stop(lockmgr_t *lm)
{
    pthread_mutex_lock(&lm->m);
    lm->stop = true;
    pthread_mutex_unlock(&lm->m);
    if (lm->detector_running)
        pthread_join(lm->detector, NULL);
    lm->detector_running = false;
}

void lm_set_policy(lockmgr_t *lm, dl_policy_t p)
{
    pthread_mutex_lock(&lm->m);
    lm->policy = p;
    pthread_mutex_unlock(&lm->m);
}

void txn_begin(lockmgr_t *lm, txn_t *t, uint64_t id, const char *label)
{
    memset(t, 0, sizeof *t);
    t->id = id;
    t->label = label;
    t->start_ns = now_ns();
    pthread_mutex_lock(&lm->m);
    t->next = lm->active;
    if (lm->active)
        lm->active->prev = t;
    lm->active = t;
    lm->nactive++;
    pthread_mutex_unlock(&lm->m);
}

static void release_all_locked(lockmgr_t *lm, txn_t *t)
{
    for (int i = 0; i < t->nheld; i++) {
        lock_entry_t *e = entry_get(lm, t->held[i]);
        if (e->owner == t) {
            e->owner = NULL;
            if (e->waiters)
                pthread_cond_broadcast(&e->cv);
        }
    }
    t->nheld = 0;
}

void lm_release_all(lockmgr_t *lm, txn_t *t)
{
    pthread_mutex_lock(&lm->m);
    release_all_locked(lm, t);
    pthread_mutex_unlock(&lm->m);
}

void txn_end(lockmgr_t *lm, txn_t *t)
{
    pthread_mutex_lock(&lm->m);
    release_all_locked(lm, t);
    if (t->prev)
        t->prev->next = t->next;
    else
        lm->active = t->next;
    if (t->next)
        t->next->prev = t->prev;
    lm->nactive--;
    pthread_mutex_unlock(&lm->m);
}

/* returns LM_OK, LM_DEADLOCK (victim), or 1 (busy, only when !block) */
static int lock_one(lockmgr_t *lm, txn_t *t, uint64_t rid, bool block)
{
    pthread_mutex_lock(&lm->m);
    lock_entry_t *e = entry_get(lm, rid);
    if (e->owner && e->owner != t) {
        lm->contended++;
        if (!block) {
            pthread_mutex_unlock(&lm->m);
            return 1;
        }
        t->waiting_for = rid;
        e->waiters++;
        while (e->owner && e->owner != t && !t->victim) {
            struct timespec dl;
            deadline_after(&dl, 100 * 1000000LL);
            pthread_cond_timedwait(&e->cv, &lm->m, &dl);
        }
        e->waiters--;
        t->waiting_for = 0;
        if (t->victim) {
            pthread_mutex_unlock(&lm->m);
            return LM_DEADLOCK;
        }
    }
    if (e->owner != t) {
        e->owner = t;
        if (t->nheld < TXN_MAX_LOCKS)
            t->held[t->nheld++] = rid;
        lm->acquisitions++;
    }
    pthread_mutex_unlock(&lm->m);
    return LM_OK;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static void abort_victim(lockmgr_t *lm, txn_t *t)
{
    pthread_mutex_lock(&lm->m);
    release_all_locked(lm, t);
    t->victim = false;
    t->restarts++;
    int shift = t->restarts < 6 ? t->restarts : 6;
    pthread_mutex_unlock(&lm->m);
    /* Back off before the retry. Without this the victim re-grabs the lock it
     * just released before the woken waiter runs, and the same cycle forms
     * again forever (livelock). */
    sleep_us(1000 + rand_below(1000u << shift));
}

int lm_lock_set(lockmgr_t *lm, txn_t *t, const uint64_t *rids_in, int n)
{
    if (n > TXN_MAX_LOCKS)
        n = TXN_MAX_LOCKS;
    uint64_t rids[TXN_MAX_LOCKS];
    memcpy(rids, rids_in, sizeof(uint64_t) * (size_t)n);

    pthread_mutex_lock(&lm->m);
    dl_policy_t pol = lm->policy;
    int64_t hold = lm->hold_ns;
    pthread_mutex_unlock(&lm->m);

    if (pol == DL_ORDERED)
        qsort(rids, (size_t)n, sizeof rids[0], cmp_u64);

    if (pol != DL_TRYLOCK) {
        for (int i = 0; i < n; i++) {
            if (lock_one(lm, t, rids[i], true) == LM_DEADLOCK) {
                abort_victim(lm, t);
                return LM_DEADLOCK;
            }
            if (i + 1 < n && hold > 0)
                sleep_us(hold / 1000); /* e.g. a fraud check while holding the first lock */
        }
        return LM_OK;
    }

    /* trylock: never hold a lock while blocking on another */
    for (int attempt = 0;; attempt++) {
        if (lock_one(lm, t, rids[0], true) == LM_DEADLOCK) {
            abort_victim(lm, t);
            return LM_DEADLOCK;
        }
        bool all = true;
        for (int i = 1; i < n && all; i++) {
            if (hold > 0)
                sleep_us(hold / 1000);
            if (lock_one(lm, t, rids[i], false) != LM_OK)
                all = false;
        }
        if (all)
            return LM_OK;
        lm_release_all(lm, t);
        pthread_mutex_lock(&lm->m);
        lm->backoffs++;
        pthread_mutex_unlock(&lm->m);
        int shift = attempt < 6 ? attempt : 6;
        sleep_us(200 + rand_below(500u << shift));
    }
}

/* ------------------------------------------------------------------ detection */

#define MAX_NODES 512

int lm_detect(lockmgr_t *lm, bool resolve)
{
    pthread_mutex_lock(&lm->m);
    lm->detector_passes++;
    txn_t *nodes[MAX_NODES];
    int n = 0;
    for (txn_t *t = lm->active; t && n < MAX_NODES; t = t->next)
        nodes[n++] = t;
    /* wait-for graph: each waiting transaction has one edge, to the owner of
     * the lock it waits for */
    int succ[MAX_NODES];
    for (int i = 0; i < n; i++) {
        succ[i] = -1;
        if (!nodes[i]->waiting_for || nodes[i]->victim)
            continue;
        lock_entry_t *e = entry_get(lm, nodes[i]->waiting_for);
        for (int k = 0; k < n; k++)
            if (nodes[k] == e->owner) {
                succ[i] = k;
                break;
            }
    }
    /* iterative DFS with white/grey/black colouring; a grey successor closes a cycle */
    enum { WHITE, GREY, BLACK };
    int color[MAX_NODES] = {0};
    int found = 0;
    for (int s = 0; s < n; s++) {
        if (color[s] != WHITE)
            continue;
        int path[MAX_NODES], depth = 0, v = s;
        while (v != -1 && color[v] == WHITE) {
            color[v] = GREY;
            path[depth++] = v;
            v = succ[v];
        }
        if (v != -1 && color[v] == GREY) {
            /* cycle = path from v to the end */
            int start = 0;
            while (path[start] != v)
                start++;
            found++;
            txn_t *victim = NULL;
            char buf[256];
            int off = 0;
            for (int i = start; i < depth; i++) {
                txn_t *t = nodes[path[i]];
                off += snprintf(buf + off, sizeof buf - (size_t)off, "T%llu(waits acct %llu) -> ",
                                (unsigned long long)t->id, (unsigned long long)t->waiting_for);
                if (off >= (int)sizeof buf)
                    off = (int)sizeof buf - 1;
                if (!victim || t->start_ns > victim->start_ns)
                    victim = t; /* youngest has done the least work */
            }
            snprintf(buf + off, sizeof buf - (size_t)off, "T%llu",
                     (unsigned long long)nodes[v]->id);
            /* an unresolved cycle is seen again on every pass: count it once */
            if (resolve || strcmp(buf, lm->last_cycle) != 0)
                lm->deadlocks_found++;
            snprintf(lm->last_cycle, sizeof lm->last_cycle, "%s", buf);
            if (resolve && victim) {
                victim->victim = true;
                lm->victims++;
                lock_entry_t *e = entry_get(lm, victim->waiting_for);
                pthread_cond_broadcast(&e->cv);
                LOGW("deadlock: %s ; aborting victim T%llu", buf, (unsigned long long)victim->id);
            }
        }
        for (int i = 0; i < depth; i++)
            color[path[i]] = BLACK;
    }
    pthread_mutex_unlock(&lm->m);
    return found;
}

void lm_render(lockmgr_t *lm, sbuf_t *out)
{
    pthread_mutex_lock(&lm->m);
    sb_printf(out, "Deadlock policy : %s   (lock hold delay %lld us)\n", dl_policy_name(lm->policy),
              (long long)(lm->hold_ns / 1000));
    sb_printf(out,
              "Acquisitions %llu | contended %llu | deadlocks found %llu | victims aborted %llu | "
              "trylock back-offs %llu | detector passes %llu\n",
              (unsigned long long)lm->acquisitions, (unsigned long long)lm->contended,
              (unsigned long long)lm->deadlocks_found, (unsigned long long)lm->victims,
              (unsigned long long)lm->backoffs, (unsigned long long)lm->detector_passes);
    sb_printf(out, "Active transactions: %d\n", lm->nactive);
    int shown = 0;
    for (txn_t *t = lm->active; t && shown < 40; t = t->next, shown++) {
        sb_printf(out, "  T%-6llu %-10s holds [", (unsigned long long)t->id, t->label ? t->label : "");
        for (int i = 0; i < t->nheld; i++)
            sb_printf(out, "%s%llu", i ? " " : "", (unsigned long long)t->held[i]);
        sb_printf(out, "]");
        if (t->waiting_for) {
            lock_entry_t *e = entry_get(lm, t->waiting_for);
            sb_printf(out, "  waits for %llu (held by T%llu)", (unsigned long long)t->waiting_for,
                      e->owner ? (unsigned long long)e->owner->id : 0ull);
        }
        sb_printf(out, "%s\n", t->restarts ? "  (restarted)" : "");
    }
    if (lm->last_cycle[0])
        sb_printf(out, "Last cycle: %s\n", lm->last_cycle);
    pthread_mutex_unlock(&lm->m);
}

const char *dl_policy_name(dl_policy_t p)
{
    static const char *n[] = {"none", "detect", "ordered", "trylock"};
    return p < DL_POLICY_COUNT ? n[p] : "?";
}

bool dl_policy_parse(const char *s, dl_policy_t *out)
{
    for (int i = 0; i < DL_POLICY_COUNT; i++)
        if (!strcmp(s, dl_policy_name((dl_policy_t)i))) {
            *out = (dl_policy_t)i;
            return true;
        }
    return false;
}

void lm_get_stats(lockmgr_t *lm, lm_stats_t *out)
{
    pthread_mutex_lock(&lm->m);
    *out = (lm_stats_t){lm->policy, lm->acquisitions, lm->contended, lm->deadlocks_found, lm->victims,
                        lm->backoffs};
    pthread_mutex_unlock(&lm->m);
}
