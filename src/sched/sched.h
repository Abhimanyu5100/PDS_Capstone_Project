/* sched.h - CPU scheduling policies for bank transactions.
 *
 * Every customer request becomes a job with a CPU burst measured in work units
 * (a unit is one "tick" of teller time). Teller threads are the CPUs. The run
 * queue decides which job a free teller dispatches next, and after every unit it
 * decides whether the running job must be preempted.
 *
 * The same runq_t drives both the live teller pool (time in nanoseconds) and the
 * offline simulator in sim.c (time in ticks). Policies:
 *
 *   FCFS  first come, first served                      non-preemptive
 *   SJF   shortest job first (by remaining burst)       non-preemptive
 *   SRTF  shortest remaining time first                 preemptive
 *   RR    round robin with a fixed quantum              preemptive
 *   PRIO  priority (customer tier) with aging           preemptive
 *   MLFQ  multi-level feedback queue, 3 levels,         preemptive
 *         quantum doubles per level, periodic boost */
#ifndef PLUTUS_SCHED_H
#define PLUTUS_SCHED_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SP_FCFS,
    SP_SJF,
    SP_SRTF,
    SP_RR,
    SP_PRIO,
    SP_MLFQ,
    SP_POLICY_COUNT
} sched_policy_t;

#define MLFQ_LEVELS 3

typedef struct job job_t;
struct job {
    uint64_t id;
    int kind;         /* opaque to the scheduler: request type */
    int priority;     /* 0 = most urgent */
    int burst;        /* total units of work */
    int remaining;    /* units left */
    int level;        /* MLFQ queue level */
    int preemptions;
    int64_t arrival;  /* time first queued */
    int64_t first_run;/* time first dispatched, -1 before that */
    int64_t finish;
    int64_t enqueued; /* time of the latest enqueue (for aging and waiting time) */
    int64_t waited;   /* total time spent in the ready queue */
    job_t *next;
    void *ctx;        /* owner data (the live engine stores its request here) */
};

typedef struct {
    sched_policy_t policy;
    int quantum;            /* RR and MLFQ level-0 quantum, in units */
    int64_t aging_interval; /* PRIO: one priority step gained per interval waited; 0 = off */
    int64_t boost_interval; /* MLFQ: everything returns to level 0 each interval; 0 = off */
    int64_t last_boost;
    job_t *head[MLFQ_LEVELS], *tail[MLFQ_LEVELS];
    int count;
    uint64_t dispatches, preemptions, boosts;
} runq_t;

void rq_init(runq_t *rq, sched_policy_t policy, int quantum);
void job_init(job_t *j, uint64_t id, int kind, int priority, int burst);
void rq_push(runq_t *rq, job_t *j, int64_t now);
job_t *rq_pop(runq_t *rq, int64_t now);
/* Called after the running job completes a unit. `ran` is the number of units
 * it has run since it was dispatched. May demote the job (MLFQ). Returns true
 * when the job must go back to the ready queue. */
bool rq_should_preempt(runq_t *rq, job_t *running, int ran, int64_t now);
void rq_set_policy(runq_t *rq, sched_policy_t p);
int rq_effective_priority(const runq_t *rq, const job_t *j, int64_t now);
int rq_level_quantum(const runq_t *rq, int level);

const char *sched_policy_name(sched_policy_t p);
bool sched_policy_parse(const char *s, sched_policy_t *out);

#endif
