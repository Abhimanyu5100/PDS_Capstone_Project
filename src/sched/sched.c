#include "sched.h"

#include <limits.h>
#include <string.h>

void rq_init(runq_t *rq, sched_policy_t policy, int quantum)
{
    memset(rq, 0, sizeof *rq);
    rq->policy = policy;
    rq->quantum = quantum > 0 ? quantum : 1;
}

void job_init(job_t *j, uint64_t id, int kind, int priority, int burst)
{
    memset(j, 0, sizeof *j);
    j->id = id;
    j->kind = kind;
    j->priority = priority;
    j->burst = j->remaining = burst > 0 ? burst : 1;
    j->first_run = -1;
    j->arrival = -1;
}

static void list_append(runq_t *rq, int lvl, job_t *j)
{
    j->next = NULL;
    if (rq->tail[lvl])
        rq->tail[lvl]->next = j;
    else
        rq->head[lvl] = j;
    rq->tail[lvl] = j;
}

static void list_unlink(runq_t *rq, int lvl, job_t *prev, job_t *j)
{
    if (prev)
        prev->next = j->next;
    else
        rq->head[lvl] = j->next;
    if (rq->tail[lvl] == j)
        rq->tail[lvl] = prev;
    j->next = NULL;
}

void rq_push(runq_t *rq, job_t *j, int64_t now)
{
    if (j->arrival < 0)
        j->arrival = now;
    j->enqueued = now;
    int lvl = rq->policy == SP_MLFQ ? j->level : 0;
    if (lvl < 0 || lvl >= MLFQ_LEVELS)
        lvl = 0;
    list_append(rq, lvl, j);
    rq->count++;
}

int rq_effective_priority(const runq_t *rq, const job_t *j, int64_t now)
{
    int p = j->priority;
    if (rq->aging_interval > 0 && now > j->enqueued)
        p -= (int)((now - j->enqueued) / rq->aging_interval);
    return p < 0 ? 0 : p;
}

int rq_level_quantum(const runq_t *rq, int level) { return rq->quantum << level; }

static void mlfq_boost(runq_t *rq, int64_t now)
{
    if (rq->boost_interval <= 0 || now - rq->last_boost < rq->boost_interval)
        return;
    rq->last_boost = now;
    for (int l = 1; l < MLFQ_LEVELS; l++) {
        job_t *j;
        while ((j = rq->head[l])) {
            list_unlink(rq, l, NULL, j);
            j->level = 0;
            list_append(rq, 0, j);
        }
    }
    rq->boosts++;
}

/* Returns the best job on level 0 under a "smaller key wins" rule, FIFO on ties. */
static job_t *pick_min(runq_t *rq, int64_t now, job_t **prev_out)
{
    job_t *best = NULL, *best_prev = NULL, *prev = NULL;
    long best_key = LONG_MAX;
    for (job_t *j = rq->head[0]; j; prev = j, j = j->next) {
        long key;
        if (rq->policy == SP_PRIO)
            key = rq_effective_priority(rq, j, now);
        else
            key = j->remaining;
        if (key < best_key) {
            best_key = key;
            best = j;
            best_prev = prev;
        }
    }
    *prev_out = best_prev;
    return best;
}

job_t *rq_pop(runq_t *rq, int64_t now)
{
    if (rq->count == 0)
        return NULL;
    job_t *j = NULL, *prev = NULL;
    int lvl = 0;
    switch (rq->policy) {
    case SP_FCFS:
    case SP_RR:
        j = rq->head[0];
        break;
    case SP_SJF:
    case SP_SRTF:
    case SP_PRIO:
        j = pick_min(rq, now, &prev);
        break;
    case SP_MLFQ:
        mlfq_boost(rq, now);
        for (lvl = 0; lvl < MLFQ_LEVELS && !rq->head[lvl]; lvl++)
            ;
        j = rq->head[lvl];
        break;
    default:
        j = rq->head[0];
    }
    list_unlink(rq, lvl, prev, j);
    rq->count--;
    rq->dispatches++;
    j->waited += now - j->enqueued;
    if (j->first_run < 0)
        j->first_run = now;
    return j;
}

bool rq_should_preempt(runq_t *rq, job_t *running, int ran, int64_t now)
{
    if (running->remaining <= 0)
        return false;
    bool preempt = false;
    switch (rq->policy) {
    case SP_FCFS:
    case SP_SJF:
        break;
    case SP_SRTF:
        for (job_t *j = rq->head[0]; j; j = j->next)
            if (j->remaining < running->remaining) {
                preempt = true;
                break;
            }
        break;
    case SP_PRIO:
        for (job_t *j = rq->head[0]; j; j = j->next)
            if (rq_effective_priority(rq, j, now) < running->priority) {
                preempt = true;
                break;
            }
        break;
    case SP_RR:
        preempt = ran >= rq->quantum && rq->count > 0;
        break;
    case SP_MLFQ: {
        for (int l = 0; l < running->level; l++)
            if (rq->head[l]) {
                preempt = true; /* a higher queue has work: yield, keep level */
                break;
            }
        if (!preempt && ran >= rq_level_quantum(rq, running->level)) {
            /* used its whole allotment: it looks CPU-bound, demote it */
            if (running->level < MLFQ_LEVELS - 1)
                running->level++;
            preempt = rq->count > 0;
        }
        break;
    }
    default:
        break;
    }
    if (preempt) {
        running->preemptions++;
        rq->preemptions++;
    }
    return preempt;
}

void rq_set_policy(runq_t *rq, sched_policy_t p)
{
    /* collapse every level into one FIFO list (ordered by enqueue time), then
     * redistribute under the new policy */
    job_t *all = NULL, **tailp = &all;
    for (int l = 0; l < MLFQ_LEVELS; l++) {
        if (rq->head[l]) {
            *tailp = rq->head[l];
            tailp = &rq->tail[l]->next;
        }
        rq->head[l] = rq->tail[l] = NULL;
    }
    rq->policy = p;
    int n = rq->count;
    rq->count = 0;
    while (all) {
        job_t *j = all;
        all = j->next;
        if (p != SP_MLFQ)
            j->level = 0;
        int64_t enq = j->enqueued;
        rq_push(rq, j, enq);
    }
    rq->count = n;
}

const char *sched_policy_name(sched_policy_t p)
{
    static const char *n[] = {"fcfs", "sjf", "srtf", "rr", "prio", "mlfq"};
    return p < SP_POLICY_COUNT ? n[p] : "?";
}

bool sched_policy_parse(const char *s, sched_policy_t *out)
{
    for (int i = 0; i < SP_POLICY_COUNT; i++)
        if (!strcmp(s, sched_policy_name((sched_policy_t)i))) {
            *out = (sched_policy_t)i;
            return true;
        }
    return false;
}
