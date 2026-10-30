/* Scheduler tests: textbook workloads (Silberschatz, ch. 5) with known answers. */
#include "../src/sched/sim.h"
#include "testlib.h"

static sim_report_t R;

static double run(const sim_proc_t *p, int n, sched_policy_t pol, int q)
{
    sim_run(p, n, pol, q, 1, NULL, &R);
    return R.avg_wait;
}

static const sim_proc_t convoy[] = {{1, 0, 24, 0}, {2, 0, 3, 0}, {3, 0, 3, 0}};

TEST(fcfs_convoy_effect)
{
    CHECK_EQ((int)(run(convoy, 3, SP_FCFS, 1) * 100 + 0.5), 1700);
    CHECK_EQ(R.res[0].waiting, 0);
    CHECK_EQ(R.res[1].waiting, 24);
    CHECK_EQ(R.res[2].waiting, 27);
    CHECK_EQ(R.makespan, 30);
}

TEST(sjf_minimises_waiting)
{
    CHECK_EQ((int)(run(convoy, 3, SP_SJF, 1) * 100 + 0.5), 300);
}

TEST(rr_quantum_4)
{
    run(convoy, 3, SP_RR, 4);
    CHECK_EQ(R.res[0].waiting, 6);
    CHECK_EQ(R.res[1].waiting, 4);
    CHECK_EQ(R.res[2].waiting, 7);
    CHECK_EQ(R.res[1].response, 4);
}

TEST(srtf_preempts_on_arrival)
{
    const sim_proc_t p[] = {{1, 0, 8, 0}, {2, 1, 4, 0}, {3, 2, 9, 0}, {4, 3, 5, 0}};
    CHECK_EQ((int)(run(p, 4, SP_SRTF, 1) * 100 + 0.5), 650);
    CHECK_EQ(R.res[0].waiting, 9);
    CHECK_EQ(R.res[2].waiting, 15);
}

TEST(priority_order)
{
    const sim_proc_t p[] = {{1, 0, 10, 3}, {2, 0, 1, 1}, {3, 0, 2, 4}, {4, 0, 1, 5}, {5, 0, 5, 2}};
    CHECK_EQ((int)(run(p, 5, SP_PRIO, 1) * 100 + 0.5), 820);
}

TEST(priority_starvation_and_aging)
{
    /* a low-priority batch job competes with a stream of VIP jobs */
    sim_proc_t p[40];
    p[0] = (sim_proc_t){1, 0, 5, 3};
    for (int i = 1; i < 40; i++)
        p[i] = (sim_proc_t){i + 1, i * 2, 2, 0};
    sim_run(p, 40, SP_PRIO, 1, 1, NULL, &R);
    int starved_wait = R.res[0].waiting;
    sim_opts_t aging = {.aging_interval = 4};
    sim_run(p, 40, SP_PRIO, 1, 1, &aging, &R);
    CHECK(R.res[0].waiting < starved_wait); /* aging rescues the batch job */
    CHECK(starved_wait >= 70);
}

TEST(mlfq_demotes_cpu_bound)
{
    const sim_proc_t p[] = {{1, 0, 30, 0}, {2, 5, 2, 0}, {3, 6, 2, 0}};
    sim_run(p, 3, SP_MLFQ, 2, 1, NULL, &R);
    /* short interactive jobs finish quickly even though P1 arrived first */
    CHECK(R.res[1].turnaround <= 4);
    CHECK(R.res[2].turnaround <= 6);
    CHECK(R.res[0].preemptions >= 1);
}

TEST(multi_cpu_rr)
{
    sim_run(convoy, 3, SP_RR, 4, 2, NULL, &R);
    CHECK_EQ(R.res[1].waiting, 0);
    CHECK(R.makespan < 30);
    CHECK(R.cpu_util > 0.5);
}

TEST(policy_switch_keeps_jobs)
{
    runq_t rq;
    rq_init(&rq, SP_MLFQ, 2);
    job_t j[5];
    for (int i = 0; i < 5; i++) {
        job_init(&j[i], (uint64_t)i, 0, 0, 5 - i);
        j[i].level = i % 3;
        rq_push(&rq, &j[i], i);
    }
    rq_set_policy(&rq, SP_SJF);
    CHECK_EQ(rq.count, 5);
    job_t *first = rq_pop(&rq, 10);
    CHECK_EQ(first->id, 4); /* shortest burst */
    int n = 1;
    while (rq_pop(&rq, 10))
        n++;
    CHECK_EQ(n, 5);
}

int main(void)
{
    t_begin("sched");
    RUN(fcfs_convoy_effect);
    RUN(sjf_minimises_waiting);
    RUN(rr_quantum_4);
    RUN(srtf_preempts_on_arrival);
    RUN(priority_order);
    RUN(priority_starvation_and_aging);
    RUN(mlfq_demotes_cpu_bound);
    RUN(multi_cpu_rr);
    RUN(policy_switch_keeps_jobs);
    return t_end();
}
