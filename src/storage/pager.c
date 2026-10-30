#include "pager.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../common/crypto.h"

static uint32_t page_crc(const uint8_t *data)
{
    page_hdr_t h;
    memcpy(&h, data, sizeof h);
    h.crc = 0;
    uint32_t c = crc32(&h, sizeof h);
    return crc32_update(c, data + PAGE_HDR, PAGE_SIZE - PAGE_HDR);
}

int pager_open(pager_t *p, const char *path, int nframes, repl_policy_t policy, wal_t *wal)
{
    memset(p, 0, sizeof *p);
    p->fd = open(path, O_RDWR | O_CREAT, 0600);
    if (p->fd < 0)
        return -1;
    struct stat st;
    if (fstat(p->fd, &st) < 0)
        return -1;
    p->npages = (uint64_t)st.st_size / PAGE_SIZE;
    if (p->npages < 1)
        p->npages = 1;
    p->nframes = nframes < 2 ? 2 : nframes;
    p->policy = policy;
    p->wal = wal;
    p->frames = calloc((size_t)p->nframes, sizeof(frame_t));
    uint8_t *mem = NULL;
    if (posix_memalign((void **)&mem, PAGE_SIZE, (size_t)p->nframes * PAGE_SIZE) != 0 || !p->frames)
        die("out of memory");
    for (int i = 0; i < p->nframes; i++) {
        p->frames[i].data = mem + (size_t)i * PAGE_SIZE;
        pthread_mutex_init(&p->frames[i].latch, NULL);
    }
    pthread_mutex_init(&p->m, NULL);
    pthread_cond_init(&p->unpinned_cv, NULL);
    return 0;
}

static int write_frame(pager_t *p, frame_t *f)
{
    pthread_mutex_lock(&f->latch);
    page_hdr_t *h = page_hdr(f);
    /* WAL rule: a page may reach disk only after the log records describing
     * its changes are durable. Updates are applied after commit, so this holds
     * by construction; the check guards it. */
    if (p->wal && h->page_lsn > wal_durable_lsn(p->wal))
        LOGE("WAL rule violated: page %llu lsn %llu", (unsigned long long)f->page_id,
             (unsigned long long)h->page_lsn);
    h->magic = PAGE_MAGIC;
    h->page_id = f->page_id;
    h->crc = page_crc(f->data);
    ssize_t w = pwrite(p->fd, f->data, PAGE_SIZE, (off_t)(f->page_id * PAGE_SIZE));
    pthread_mutex_unlock(&f->latch);
    if (w != PAGE_SIZE) {
        LOGE("page %llu write failed: %s", (unsigned long long)f->page_id, strerror(errno));
        return -1;
    }
    f->dirty = false;
    p->writebacks++;
    return 0;
}

static void read_frame(pager_t *p, frame_t *f, uint64_t page_id)
{
    ssize_t r = pread(p->fd, f->data, PAGE_SIZE, (off_t)(page_id * PAGE_SIZE));
    page_hdr_t *h = page_hdr(f);
    if (r != PAGE_SIZE || h->magic != PAGE_MAGIC) {
        /* beyond EOF or never written: a fresh, empty page */
        memset(f->data, 0, PAGE_SIZE);
        h->magic = PAGE_MAGIC;
        h->page_id = page_id;
        return;
    }
    if (h->crc != page_crc(f->data)) {
        p->crc_errors++;
        LOGE("page %llu failed its checksum (torn or corrupt write); WAL redo will repair "
             "updates since the last checkpoint",
             (unsigned long long)page_id);
    }
}

static int choose_victim(pager_t *p)
{
    int best = -1;
    for (int i = 0; i < p->nframes; i++)
        if (!p->frames[i].valid)
            return i;
    switch (p->policy) {
    case REPL_FIFO:
    case REPL_LRU:
        for (int i = 0; i < p->nframes; i++) {
            frame_t *f = &p->frames[i];
            if (f->pin)
                continue;
            uint64_t key = p->policy == REPL_FIFO ? f->loaded_at : f->last_used;
            uint64_t bkey = best < 0 ? 0
                            : p->policy == REPL_FIFO ? p->frames[best].loaded_at
                                                     : p->frames[best].last_used;
            if (best < 0 || key < bkey)
                best = i;
        }
        return best;
    case REPL_CLOCK:
        for (int step = 0; step < 2 * p->nframes; step++) {
            frame_t *f = &p->frames[p->clock_hand];
            int idx = p->clock_hand;
            p->clock_hand = (p->clock_hand + 1) % p->nframes;
            if (f->pin)
                continue;
            if (f->ref) {
                f->ref = false; /* second chance */
                continue;
            }
            return idx;
        }
        return -1;
    default:
        return -1;
    }
}

frame_t *pager_fetch(pager_t *p, uint64_t page_id)
{
    pthread_mutex_lock(&p->m);
    for (;;) {
        for (int i = 0; i < p->nframes; i++) {
            frame_t *f = &p->frames[i];
            if (f->valid && f->page_id == page_id) {
                f->pin++;
                f->ref = true;
                f->last_used = ++p->tick;
                p->hits++;
                pthread_mutex_unlock(&p->m);
                return f;
            }
        }
        int v = choose_victim(p);
        if (v >= 0) {
            frame_t *f = &p->frames[v];
            if (f->valid) {
                p->evictions++;
                if (f->dirty)
                    write_frame(p, f);
            }
            /* I/O is done under the pool mutex: simple and correct, at the
             * cost of serialising page faults */
            read_frame(p, f, page_id);
            f->page_id = page_id;
            f->valid = true;
            f->dirty = false;
            f->ref = true;
            f->pin = 1;
            f->loaded_at = f->last_used = ++p->tick;
            if (page_id >= p->npages)
                p->npages = page_id + 1;
            p->misses++;
            pthread_mutex_unlock(&p->m);
            return f;
        }
        p->pin_waits++; /* every frame pinned: wait for an unpin */
        pthread_cond_wait(&p->unpinned_cv, &p->m);
    }
}

void pager_unpin(pager_t *p, frame_t *f, bool dirtied)
{
    pthread_mutex_lock(&p->m);
    if (dirtied)
        f->dirty = true;
    if (--f->pin == 0)
        pthread_cond_signal(&p->unpinned_cv);
    pthread_mutex_unlock(&p->m);
}

uint64_t pager_new_page(pager_t *p)
{
    pthread_mutex_lock(&p->m);
    uint64_t id = p->npages++;
    pthread_mutex_unlock(&p->m);
    return id;
}

int pager_flush_all(pager_t *p)
{
    int rc = 0;
    pthread_mutex_lock(&p->m);
    for (int i = 0; i < p->nframes; i++) {
        frame_t *f = &p->frames[i];
        if (f->valid && f->dirty && write_frame(p, f) != 0)
            rc = -1;
    }
    pthread_mutex_unlock(&p->m);
    if (fsync(p->fd) != 0)
        rc = -1;
    return rc;
}

void pager_close(pager_t *p)
{
    pager_flush_all(p);
    close(p->fd);
    free(p->frames[0].data);
    free(p->frames);
    p->frames = NULL;
}

void pager_set_policy(pager_t *p, repl_policy_t pol)
{
    pthread_mutex_lock(&p->m);
    p->policy = pol;
    pthread_mutex_unlock(&p->m);
}

void pager_render(pager_t *p, sbuf_t *out)
{
    pthread_mutex_lock(&p->m);
    uint64_t refs = p->hits + p->misses;
    sb_printf(out, "Buffer pool: %d frames x %d B, policy %s, database %llu pages\n", p->nframes,
              PAGE_SIZE, repl_policy_name(p->policy), (unsigned long long)p->npages);
    sb_printf(out,
              "  hits %llu | page faults %llu | hit ratio %.1f%% | evictions %llu | write-backs %llu "
              "| checksum errors %llu | waits (all pinned) %llu\n",
              (unsigned long long)p->hits, (unsigned long long)p->misses,
              refs ? 100.0 * (double)p->hits / (double)refs : 0.0, (unsigned long long)p->evictions,
              (unsigned long long)p->writebacks, (unsigned long long)p->crc_errors,
              (unsigned long long)p->pin_waits);
    sb_printf(out, "  %-5s %-6s %-5s %-5s %-4s %-9s %-9s\n", "frame", "page", "dirty", "ref", "pin",
              "loaded@", "used@");
    for (int i = 0; i < p->nframes && i < 64; i++) {
        frame_t *f = &p->frames[i];
        if (!f->valid) {
            sb_printf(out, "  %-5d %-6s\n", i, "-");
            continue;
        }
        sb_printf(out, "  %-5d %-6llu %-5s %-5s %-4d %-9llu %-9llu%s\n", i,
                  (unsigned long long)f->page_id, f->dirty ? "yes" : "no", f->ref ? "1" : "0", f->pin,
                  (unsigned long long)f->loaded_at, (unsigned long long)f->last_used,
                  p->policy == REPL_CLOCK && i == p->clock_hand ? "  <- clock hand" : "");
    }
    pthread_mutex_unlock(&p->m);
}

const char *repl_policy_name(repl_policy_t p)
{
    static const char *n[] = {"fifo", "lru", "clock"};
    return p < REPL_POLICY_COUNT ? n[p] : "?";
}

bool repl_policy_parse(const char *s, repl_policy_t *out)
{
    for (int i = 0; i < REPL_POLICY_COUNT; i++)
        if (!strcmp(s, repl_policy_name((repl_policy_t)i))) {
            *out = (repl_policy_t)i;
            return true;
        }
    return false;
}

/* ------------------------------------------------------------------ simulator */

const char *psim_name(psim_policy_t p)
{
    static const char *n[] = {"FIFO", "LRU", "CLOCK", "OPT"};
    return p < PSIM_COUNT ? n[p] : "?";
}

int page_sim(const int *refs, int n, int frames, psim_policy_t pol, sbuf_t *trace)
{
    enum { MAXF = 32 };
    if (frames > MAXF)
        frames = MAXF;
    int page[MAXF], loaded[MAXF], used[MAXF];
    bool refbit[MAXF];
    int nf = 0, hand = 0, faults = 0;
    for (int t = 0; t < n; t++) {
        int r = refs[t], hit = -1;
        for (int i = 0; i < nf; i++)
            if (page[i] == r)
                hit = i;
        if (hit >= 0) {
            used[hit] = t;
            refbit[hit] = true;
        } else {
            faults++;
            int v;
            if (nf < frames) {
                v = nf++;
            } else if (pol == PSIM_FIFO) {
                v = 0;
                for (int i = 1; i < nf; i++)
                    if (loaded[i] < loaded[v])
                        v = i;
            } else if (pol == PSIM_LRU) {
                v = 0;
                for (int i = 1; i < nf; i++)
                    if (used[i] < used[v])
                        v = i;
            } else if (pol == PSIM_CLOCK) {
                while (refbit[hand]) {
                    refbit[hand] = false;
                    hand = (hand + 1) % nf;
                }
                v = hand;
                hand = (hand + 1) % nf;
            } else { /* OPT: evict the page used furthest in the future */
                v = 0;
                int far = -1;
                for (int i = 0; i < nf; i++) {
                    int next = n + 1;
                    for (int k = t + 1; k < n; k++)
                        if (refs[k] == page[i]) {
                            next = k;
                            break;
                        }
                    if (next > far) {
                        far = next;
                        v = i;
                    }
                }
            }
            page[v] = r;
            loaded[v] = used[v] = t;
            refbit[v] = true;
        }
        if (trace) {
            sb_printf(trace, "  %3d %s |", r, hit >= 0 ? "   " : "PF ");
            for (int i = 0; i < frames; i++)
                if (i < nf)
                    sb_printf(trace, " %3d", page[i]);
                else
                    sb_printf(trace, "   -");
            sb_printf(trace, "\n");
        }
    }
    return faults;
}
