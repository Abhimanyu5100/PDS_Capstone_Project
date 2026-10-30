/* sim.h - deterministic, virtual-time CPU scheduling simulator.
 *
 * Runs a workload through the exact runq_t code the live tellers use, on
 * `ncpu` virtual CPUs, one tick at a time. Produces a Gantt chart and the
 * classic metrics (waiting, turnaround and response time). Used by the
 * `scenario sched` admin command and the unit tests. */
#ifndef PLUTUS_SIM_H
#define PLUTUS_SIM_H

#include "../common/util.h"
#include "sched.h"

#define SIM_MAX_PROCS 64
#define SIM_MAX_SLICES 1024
#define SIM_MAX_CPUS 8

typedef struct {
    int pid;
    int arrival;
    int burst;
    int priority;
} sim_proc_t;

typedef struct {
    int cpu, pid, start, end; /* [start, end) */
} sim_slice_t;

typedef struct {
    int pid, arrival, burst, priority;
    int first_run, finish;
    int waiting, turnaround, response;
    int preemptions;
} sim_result_t;

typedef struct {
    sched_policy_t policy;
    int quantum, ncpu;
    sim_slice_t slices[SIM_MAX_SLICES];
    int nslices;
    sim_result_t res[SIM_MAX_PROCS];
    int nres;
    int makespan, context_switches;
    double avg_wait, avg_turnaround, avg_response, cpu_util;
} sim_report_t;

typedef struct {
    int64_t aging_interval; /* ticks; 0 = off */
    int64_t boost_interval; /* ticks; 0 = off */
} sim_opts_t;

void sim_run(const sim_proc_t *procs, int n, sched_policy_t policy, int quantum, int ncpu,
             const sim_opts_t *opts, sim_report_t *out);
void sim_render_gantt(const sim_report_t *r, sbuf_t *out);
void sim_render_table(const sim_report_t *r, sbuf_t *out);

#endif
