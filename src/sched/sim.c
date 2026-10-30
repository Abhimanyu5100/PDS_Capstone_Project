#include "sim.h"

#include <stdio.h>
#include <string.h>

static void add_slice(sim_report_t *r, int cpu, int pid, int t)
{
    /* extend the CPU's latest slice when the same process keeps running */
    for (int i = r->nslices - 1; i >= 0; i--) {
        sim_slice_t *s = &r->slices[i];
        if (s->cpu != cpu)
            continue;
        if (s->pid == pid && s->end == t) {
            s->end = t + 1;
            return;
        }
        break;
    }
    if (r->nslices < SIM_MAX_SLICES)
        r->slices[r->nslices++] = (sim_slice_t){cpu, pid, t, t + 1};
}

void sim_run(const sim_proc_t *procs, int n, sched_policy_t policy, int quantum, int ncpu,
             const sim_opts_t *opts, sim_report_t *out)
{
    memset(out, 0, sizeof *out);
    if (n > SIM_MAX_PROCS)
        n = SIM_MAX_PROCS;
    if (ncpu < 1)
        ncpu = 1;
    if (ncpu > SIM_MAX_CPUS)
        ncpu = SIM_MAX_CPUS;
    out->policy = policy;
    out->quantum = quantum;
    out->ncpu = ncpu;

    job_t jobs[SIM_MAX_PROCS];
    for (int i = 0; i < n; i++)
        job_init(&jobs[i], (uint64_t)procs[i].pid, 0, procs[i].priority, procs[i].burst);

    runq_t rq;
    rq_init(&rq, policy, quantum);
    if (opts) {
        rq.aging_interval = opts->aging_interval;
        rq.boost_interval = opts->boost_interval;
    }

    job_t *running[SIM_MAX_CPUS] = {0};
    int ran[SIM_MAX_CPUS] = {0};
    int last_pid[SIM_MAX_CPUS];
    for (int c = 0; c < ncpu; c++)
        last_pid[c] = -1;
    int done = 0, busy_ticks = 0;
    bool arrived[SIM_MAX_PROCS] = {0};

    for (int t = 0; done < n && t < 1000000; t++) {
        /* 1. arrivals (in input order, so ties are deterministic) */
        for (int i = 0; i < n; i++)
            if (!arrived[i] && procs[i].arrival <= t) {
                arrived[i] = true;
                rq_push(&rq, &jobs[i], t);
            }
        /* 2. completions and preemption checks at the tick boundary */
        for (int c = 0; c < ncpu; c++) {
            job_t *j = running[c];
            if (!j)
                continue;
            if (j->remaining == 0) {
                j->finish = t;
                running[c] = NULL;
                done++;
            } else if (rq_should_preempt(&rq, j, ran[c], t)) {
                rq_push(&rq, j, t);
                running[c] = NULL;
            }
        }
        /* 3. idle CPUs dispatch */
        for (int c = 0; c < ncpu; c++) {
            if (running[c])
                continue;
            job_t *j = rq_pop(&rq, t);
            if (!j)
                break;
            running[c] = j;
            ran[c] = 0;
            if (last_pid[c] != (int)j->id) {
                if (last_pid[c] != -1)
                    out->context_switches++;
                last_pid[c] = (int)j->id;
            }
        }
        /* 4. execute one tick */
        bool any = false;
        for (int c = 0; c < ncpu; c++) {
            job_t *j = running[c];
            if (!j)
                continue;
            any = true;
            j->remaining--;
            ran[c]++;
            busy_ticks++;
            add_slice(out, c, (int)j->id, t);
        }
        (void)any;
    }

    double sw = 0, st = 0, sr = 0;
    for (int i = 0; i < n; i++) {
        sim_result_t *r = &out->res[i];
        r->pid = procs[i].pid;
        r->arrival = procs[i].arrival;
        r->burst = procs[i].burst;
        r->priority = procs[i].priority;
        r->first_run = (int)jobs[i].first_run;
        r->finish = (int)jobs[i].finish;
        r->turnaround = r->finish - r->arrival;
        r->waiting = r->turnaround - r->burst;
        r->response = r->first_run - r->arrival;
        r->preemptions = jobs[i].preemptions;
        if (r->finish > out->makespan)
            out->makespan = r->finish;
        sw += r->waiting;
        st += r->turnaround;
        sr += r->response;
    }
    out->nres = n;
    if (n) {
        out->avg_wait = sw / n;
        out->avg_turnaround = st / n;
        out->avg_response = sr / n;
    }
    out->cpu_util = out->makespan ? (double)busy_ticks / ((double)out->makespan * ncpu) : 0;
}

static char pid_glyph(int pid)
{
    static const char g[] = "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    return pid >= 1 && pid <= (int)sizeof g - 1 ? g[pid - 1] : '?';
}

void sim_render_gantt(const sim_report_t *r, sbuf_t *out)
{
    int width = r->makespan;
    int scale = 1;
    while (width / scale > 96)
        scale++;
    sb_printf(out, "Gantt chart (%s, q=%d, %d CPU%s, 1 col = %d tick%s)\n",
              sched_policy_name(r->policy), r->quantum, r->ncpu, r->ncpu > 1 ? "s" : "", scale,
              scale > 1 ? "s" : "");
    for (int c = 0; c < r->ncpu; c++) {
        sb_printf(out, "  CPU%d |", c);
        for (int col = 0; col * scale < width; col++) {
            int t = col * scale;
            char ch = '.';
            for (int i = 0; i < r->nslices; i++) {
                const sim_slice_t *s = &r->slices[i];
                if (s->cpu == c && t >= s->start && t < s->end) {
                    ch = pid_glyph(s->pid);
                    break;
                }
            }
            sb_putc(out, ch);
        }
        sb_printf(out, "|\n");
    }
    sb_printf(out, "       ");
    for (int col = 0; col * scale <= width; col++)
        sb_putc(out, (col % 10) == 0 ? '|' : ' ');
    sb_printf(out, "\n       ");
    for (int col = 0; col * scale <= width; col += 10) {
        char lbl[16];
        int len = snprintf(lbl, sizeof lbl, "%d", col * scale);
        sb_printf(out, "%-10s", lbl);
        (void)len;
    }
    sb_printf(out, "\n  segments:");
    for (int i = 0; i < r->nslices && i < 40; i++)
        sb_printf(out, " [P%d %d-%d]", r->slices[i].pid, r->slices[i].start, r->slices[i].end);
    if (r->nslices > 40)
        sb_printf(out, " ... (%d more)", r->nslices - 40);
    sb_printf(out, "\n");
}

void sim_render_table(const sim_report_t *r, sbuf_t *out)
{
    sb_printf(out, "  %-4s %7s %5s %4s %6s %6s %7s %10s %8s\n", "PID", "arrive", "burst", "pri",
              "start", "finish", "waiting", "turnaround", "response");
    for (int i = 0; i < r->nres; i++) {
        const sim_result_t *x = &r->res[i];
        sb_printf(out, "  P%-3d %7d %5d %4d %6d %6d %7d %10d %8d\n", x->pid, x->arrival, x->burst,
                  x->priority, x->first_run, x->finish, x->waiting, x->turnaround, x->response);
    }
    sb_printf(out,
              "  avg waiting %.2f | avg turnaround %.2f | avg response %.2f | makespan %d | "
              "CPU util %.0f%% | context switches %d\n",
              r->avg_wait, r->avg_turnaround, r->avg_response, r->makespan, r->cpu_util * 100.0,
              r->context_switches);
}
