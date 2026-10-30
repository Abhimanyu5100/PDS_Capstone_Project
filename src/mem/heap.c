#include "heap.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "../common/util.h"

#define ALIGN 16
#define ROUND_UP(x, a) (((x) + (a)-1) & ~((size_t)(a)-1))

/* ------------------------------------------------------------------ layout
 * Free-list block:  [hdr 16][payload ...][ftr 8]
 *   hdr.size_flags : block size (multiple of 16) | 1 if allocated
 *   hdr.requested  : bytes asked for (0 when free)
 *   free blocks reuse the payload for the free-list links.
 * Buddy block:      [bhdr 16][payload ...]   size = 1 << order            */

typedef struct {
    uint64_t size_flags;
    uint64_t requested;
} fl_hdr_t;

typedef struct fl_free {
    fl_hdr_t hdr;
    struct fl_free *prev, *next;
} fl_free_t;

#define FL_FTR 8
#define FL_MIN_BLOCK ROUND_UP(sizeof(fl_free_t) + FL_FTR, ALIGN) /* 48 */

#define BUDDY_MAGIC 0xB0DD1E5u
#define BUDDY_MIN_ORDER 5 /* 32-byte blocks */
#define BUDDY_MAX_ORDERS 40

typedef struct bblk {
    uint32_t magic;
    uint8_t order;
    uint8_t is_free;
    uint16_t pad;
    uint64_t requested;
    struct bblk *prev, *next; /* only meaningful while free */
} bblk_t;

struct heap {
    pthread_mutex_t lock;
    alloc_policy_t policy;
    uint8_t *base;
    size_t cap;      /* managed bytes */
    size_t map_len;  /* mmap length */
    /* free-list state */
    fl_free_t *free_head;
    fl_free_t *rover; /* next-fit cursor */
    /* buddy state */
    int max_order;
    bblk_t *bfree[BUDDY_MAX_ORDERS];
    /* counters */
    size_t used_bytes, requested_bytes;
    uint64_t used_blocks, allocs, frees, failures, splits, coalesces, search_steps;
};

heap_t *g_heap;

/* ------------------------------------------------------------------ free-list helpers */

static inline size_t fl_size(const fl_hdr_t *h) { return h->size_flags & ~(uint64_t)15; }
static inline bool fl_used(const fl_hdr_t *h) { return h->size_flags & 1; }
static inline uint64_t *fl_footer(fl_hdr_t *h) { return (uint64_t *)((uint8_t *)h + fl_size(h) - FL_FTR); }

static void fl_set(fl_hdr_t *h, size_t size, bool used)
{
    h->size_flags = size | (used ? 1 : 0);
    *fl_footer(h) = h->size_flags;
}

static void fl_list_remove(heap_t *h, fl_free_t *b)
{
    if (b->prev)
        b->prev->next = b->next;
    else
        h->free_head = b->next;
    if (b->next)
        b->next->prev = b->prev;
    if (h->rover == b)
        h->rover = b->next;
}

/* address-ordered insert keeps first-fit faithful to the textbook definition */
static void fl_list_insert(heap_t *h, fl_free_t *b)
{
    fl_free_t *prev = NULL, *cur = h->free_head;
    while (cur && cur < b) {
        prev = cur;
        cur = cur->next;
    }
    b->prev = prev;
    b->next = cur;
    if (prev)
        prev->next = b;
    else
        h->free_head = b;
    if (cur)
        cur->prev = b;
}

static fl_free_t *fl_find(heap_t *h, size_t need)
{
    fl_free_t *pick = NULL;
    switch (h->policy) {
    case ALLOC_FIRST_FIT:
        for (fl_free_t *b = h->free_head; b; b = b->next) {
            h->search_steps++;
            if (fl_size(&b->hdr) >= need)
                return b;
        }
        return NULL;
    case ALLOC_NEXT_FIT: {
        fl_free_t *start = h->rover ? h->rover : h->free_head;
        for (fl_free_t *b = start; b; b = b->next) {
            h->search_steps++;
            if (fl_size(&b->hdr) >= need)
                return b;
        }
        for (fl_free_t *b = h->free_head; b && b != start; b = b->next) {
            h->search_steps++;
            if (fl_size(&b->hdr) >= need)
                return b;
        }
        return NULL;
    }
    case ALLOC_BEST_FIT:
        for (fl_free_t *b = h->free_head; b; b = b->next) {
            h->search_steps++;
            size_t s = fl_size(&b->hdr);
            if (s >= need && (!pick || s < fl_size(&pick->hdr))) {
                pick = b;
                if (s == need)
                    break;
            }
        }
        return pick;
    case ALLOC_WORST_FIT:
        for (fl_free_t *b = h->free_head; b; b = b->next) {
            h->search_steps++;
            size_t s = fl_size(&b->hdr);
            if (s >= need && (!pick || s > fl_size(&pick->hdr)))
                pick = b;
        }
        return pick;
    default:
        return NULL;
    }
}

static void *fl_alloc(heap_t *h, size_t n)
{
    size_t need = ROUND_UP(n + sizeof(fl_hdr_t) + FL_FTR, ALIGN);
    if (need < FL_MIN_BLOCK)
        need = FL_MIN_BLOCK;
    fl_free_t *b = fl_find(h, need);
    if (!b)
        return NULL;
    fl_list_remove(h, b);
    size_t have = fl_size(&b->hdr);
    if (have - need >= FL_MIN_BLOCK) { /* split, keep the tail free */
        fl_free_t *rest = (fl_free_t *)((uint8_t *)b + need);
        fl_set(&rest->hdr, have - need, false);
        rest->hdr.requested = 0;
        fl_list_insert(h, rest);
        if (h->policy == ALLOC_NEXT_FIT)
            h->rover = rest;
        h->splits++;
        have = need;
    } else if (h->policy == ALLOC_NEXT_FIT && !h->rover) {
        h->rover = h->free_head;
    }
    fl_set(&b->hdr, have, true);
    b->hdr.requested = n;
    h->used_bytes += have;
    h->requested_bytes += n;
    return (uint8_t *)b + sizeof(fl_hdr_t);
}

static void fl_free_block(heap_t *h, fl_hdr_t *blk)
{
    size_t size = fl_size(blk);
    h->used_bytes -= size;
    h->requested_bytes -= blk->requested;
    blk->requested = 0;
    uint8_t *start = (uint8_t *)blk;
    /* coalesce with the following block */
    uint8_t *after = start + size;
    if (after < h->base + h->cap) {
        fl_hdr_t *nb = (fl_hdr_t *)after;
        if (!fl_used(nb)) {
            fl_list_remove(h, (fl_free_t *)nb);
            size += fl_size(nb);
            h->coalesces++;
        }
    }
    /* coalesce with the preceding block, found through its footer */
    if (start > h->base) {
        uint64_t prev_tag = *(uint64_t *)(start - FL_FTR);
        if (!(prev_tag & 1)) {
            size_t psize = prev_tag & ~(uint64_t)15;
            fl_hdr_t *pb = (fl_hdr_t *)(start - psize);
            fl_list_remove(h, (fl_free_t *)pb);
            start = (uint8_t *)pb;
            size += psize;
            h->coalesces++;
        }
    }
    fl_hdr_t *merged = (fl_hdr_t *)start;
    fl_set(merged, size, false);
    merged->requested = 0;
    fl_list_insert(h, (fl_free_t *)merged);
}

/* ------------------------------------------------------------------ buddy helpers */

static void b_push(heap_t *h, bblk_t *b, int order)
{
    b->magic = BUDDY_MAGIC;
    b->order = (uint8_t)order;
    b->is_free = 1;
    b->requested = 0;
    b->prev = NULL;
    b->next = h->bfree[order];
    if (b->next)
        b->next->prev = b;
    h->bfree[order] = b;
}

static void b_unlink(heap_t *h, bblk_t *b)
{
    if (b->prev)
        b->prev->next = b->next;
    else
        h->bfree[b->order] = b->next;
    if (b->next)
        b->next->prev = b->prev;
    b->is_free = 0;
}

static void *b_alloc(heap_t *h, size_t n)
{
    size_t need = n + 16; /* the header part of bblk_t */
    int order = BUDDY_MIN_ORDER;
    while (((size_t)1 << order) < need)
        order++;
    if (order > h->max_order)
        return NULL;
    int o = order;
    while (o <= h->max_order && !h->bfree[o]) {
        o++;
        h->search_steps++;
    }
    if (o > h->max_order)
        return NULL;
    bblk_t *b = h->bfree[o];
    b_unlink(h, b);
    while (o > order) { /* split down, pushing the upper halves */
        o--;
        bblk_t *upper = (bblk_t *)((uint8_t *)b + ((size_t)1 << o));
        b_push(h, upper, o);
        h->splits++;
    }
    b->magic = BUDDY_MAGIC;
    b->order = (uint8_t)order;
    b->is_free = 0;
    b->requested = n;
    h->used_bytes += (size_t)1 << order;
    h->requested_bytes += n;
    return (uint8_t *)b + 16;
}

static void b_free_block(heap_t *h, bblk_t *b)
{
    int order = b->order;
    h->used_bytes -= (size_t)1 << order;
    h->requested_bytes -= b->requested;
    while (order < h->max_order) {
        size_t off = (size_t)((uint8_t *)b - h->base);
        bblk_t *buddy = (bblk_t *)(h->base + (off ^ ((size_t)1 << order)));
        if (buddy->magic != BUDDY_MAGIC || !buddy->is_free || buddy->order != order)
            break;
        b_unlink(h, buddy);
        if (buddy < b)
            b = buddy;
        order++;
        h->coalesces++;
    }
    b_push(h, b, order);
}

/* ------------------------------------------------------------------ public API */

heap_t *heap_create(size_t bytes, alloc_policy_t policy)
{
    heap_t *h = calloc(1, sizeof *h);
    if (!h)
        return NULL;
    pthread_mutex_init(&h->lock, NULL);
    h->policy = policy;
    if (policy == ALLOC_BUDDY) {
        int order = BUDDY_MIN_ORDER;
        while (order + 1 < BUDDY_MAX_ORDERS && ((size_t)1 << (order + 1)) <= bytes)
            order++;
        h->max_order = order;
        h->cap = (size_t)1 << order;
    } else {
        h->cap = ROUND_UP(bytes, ALIGN);
        if (h->cap < FL_MIN_BLOCK)
            h->cap = FL_MIN_BLOCK;
    }
    h->map_len = h->cap;
    void *m = mmap(NULL, h->map_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m == MAP_FAILED) {
        free(h);
        return NULL;
    }
    h->base = m;
    if (policy == ALLOC_BUDDY) {
        b_push(h, (bblk_t *)h->base, h->max_order);
    } else {
        fl_free_t *b = (fl_free_t *)h->base;
        fl_set(&b->hdr, h->cap, false);
        b->hdr.requested = 0;
        b->prev = b->next = NULL;
        h->free_head = b;
        h->rover = b;
    }
    return h;
}

void heap_destroy(heap_t *h)
{
    if (!h)
        return;
    munmap(h->base, h->map_len);
    pthread_mutex_destroy(&h->lock);
    free(h);
}

void *heap_alloc(heap_t *h, size_t n)
{
    if (n == 0)
        n = 1;
    pthread_mutex_lock(&h->lock);
    void *p = h->policy == ALLOC_BUDDY ? b_alloc(h, n) : fl_alloc(h, n);
    if (p) {
        h->allocs++;
        h->used_blocks++;
    } else {
        h->failures++;
    }
    pthread_mutex_unlock(&h->lock);
    return p;
}

void *heap_calloc(heap_t *h, size_t n)
{
    void *p = heap_alloc(h, n);
    if (p)
        memset(p, 0, n);
    return p;
}

void heap_free(heap_t *h, void *p)
{
    if (!p)
        return;
    uint8_t *u = p;
    if (u < h->base + 16 || u >= h->base + h->cap)
        die("heap_free: pointer %p is outside the heap", p);
    pthread_mutex_lock(&h->lock);
    if (h->policy == ALLOC_BUDDY) {
        bblk_t *b = (bblk_t *)(u - 16);
        if (b->magic != BUDDY_MAGIC || b->is_free)
            die("heap_free: double free or corruption at %p", p);
        b_free_block(h, b);
    } else {
        fl_hdr_t *b = (fl_hdr_t *)(u - sizeof(fl_hdr_t));
        if (!fl_used(b) || *fl_footer(b) != b->size_flags)
            die("heap_free: double free or corruption at %p", p);
        fl_free_block(h, b);
    }
    h->frees++;
    h->used_blocks--;
    pthread_mutex_unlock(&h->lock);
}

/* walk physical blocks: calls fn(start, size, used) for each */
typedef void (*walk_fn)(void *ctx, size_t off, size_t size, bool used);

static void heap_walk(heap_t *h, walk_fn fn, void *ctx)
{
    size_t off = 0;
    while (off < h->cap) {
        size_t size;
        bool used;
        if (h->policy == ALLOC_BUDDY) {
            bblk_t *b = (bblk_t *)(h->base + off);
            size = (size_t)1 << b->order;
            used = !b->is_free;
        } else {
            fl_hdr_t *b = (fl_hdr_t *)(h->base + off);
            size = fl_size(b);
            used = fl_used(b);
        }
        if (size == 0)
            break; /* corrupted; heap_check reports it */
        fn(ctx, off, size, used);
        off += size;
    }
}

typedef struct {
    heap_stats_t *s;
} stats_ctx;

static void stats_cb(void *ctx, size_t off, size_t size, bool used)
{
    (void)off;
    heap_stats_t *s = ((stats_ctx *)ctx)->s;
    if (!used) {
        s->free_blocks++;
        s->free_bytes += size;
        if (size > s->largest_free)
            s->largest_free = size;
    }
}

void heap_stats(heap_t *h, heap_stats_t *s)
{
    memset(s, 0, sizeof *s);
    pthread_mutex_lock(&h->lock);
    s->policy = h->policy;
    s->capacity = h->cap;
    s->used_bytes = h->used_bytes;
    s->requested_bytes = h->requested_bytes;
    s->used_blocks = h->used_blocks;
    s->allocs = h->allocs;
    s->frees = h->frees;
    s->failures = h->failures;
    s->splits = h->splits;
    s->coalesces = h->coalesces;
    s->search_steps = h->search_steps;
    stats_ctx c = {s};
    heap_walk(h, stats_cb, &c);
    pthread_mutex_unlock(&h->lock);
    s->external_frag = s->free_bytes ? 1.0 - (double)s->largest_free / (double)s->free_bytes : 0.0;
    s->internal_frag = s->used_bytes ? 1.0 - (double)s->requested_bytes / (double)s->used_bytes : 0.0;
}

typedef struct {
    double *used;
    size_t width;
    double bucket;
} map_ctx;

static void map_cb(void *ctx, size_t off, size_t size, bool used)
{
    map_ctx *m = ctx;
    if (!used)
        return;
    size_t end = off + size;
    while (off < end) {
        size_t bi = (size_t)((double)off / m->bucket);
        if (bi >= m->width)
            bi = m->width - 1;
        size_t bucket_end = (size_t)((double)(bi + 1) * m->bucket);
        size_t chunk = MIN(end, bucket_end) - off;
        if (chunk == 0)
            chunk = end - off;
        m->used[bi] += (double)chunk;
        off += chunk;
    }
}

void heap_map(heap_t *h, char *out, size_t width)
{
    if (width == 0)
        return;
    double used[512] = {0};
    if (width > 512)
        width = 512;
    map_ctx m = {used, width, (double)h->cap / (double)width};
    pthread_mutex_lock(&h->lock);
    heap_walk(h, map_cb, &m);
    pthread_mutex_unlock(&h->lock);
    for (size_t i = 0; i < width; i++) {
        double f = used[i] / m.bucket;
        out[i] = f > 0.66 ? '#' : f > 0.0 ? '+' : '.';
    }
    out[width] = '\0';
}

typedef struct {
    heap_t *h;
    bool ok;
    char *why;
    size_t cap;
    size_t total;
    uint64_t free_seen;
    bool prev_free;
} check_ctx;

static void check_cb(void *ctx, size_t off, size_t size, bool used)
{
    check_ctx *c = ctx;
    c->total += size;
    if (!c->ok)
        return;
    if (c->h->policy != ALLOC_BUDDY) {
        fl_hdr_t *b = (fl_hdr_t *)(c->h->base + off);
        if (*fl_footer(b) != b->size_flags) {
            c->ok = false;
            snprintf(c->why, c->cap, "footer mismatch at offset %zu", off);
        } else if (!used && c->prev_free) {
            c->ok = false;
            snprintf(c->why, c->cap, "two adjacent free blocks at offset %zu (missed coalesce)", off);
        }
    }
    if (!used)
        c->free_seen++;
    c->prev_free = !used;
}

bool heap_check(heap_t *h, char *why, size_t why_cap)
{
    char tmp[128];
    if (!why) {
        why = tmp;
        why_cap = sizeof tmp;
    }
    why[0] = '\0';
    check_ctx c = {h, true, why, why_cap, 0, 0, false};
    pthread_mutex_lock(&h->lock);
    heap_walk(h, check_cb, &c);
    uint64_t listed = 0;
    if (h->policy == ALLOC_BUDDY) {
        for (int o = 0; o <= h->max_order; o++)
            for (bblk_t *b = h->bfree[o]; b; b = b->next)
                listed++;
    } else {
        for (fl_free_t *b = h->free_head; b; b = b->next) {
            if (b->next && b->next <= b && c.ok) {
                c.ok = false;
                snprintf(why, why_cap, "free list not address ordered");
            }
            listed++;
        }
    }
    pthread_mutex_unlock(&h->lock);
    if (c.ok && c.total != h->cap) {
        c.ok = false;
        snprintf(why, why_cap, "blocks cover %zu of %zu bytes", c.total, h->cap);
    }
    if (c.ok && listed != c.free_seen) {
        c.ok = false;
        snprintf(why, why_cap, "free list has %llu blocks, heap has %llu", (unsigned long long)listed,
                 (unsigned long long)c.free_seen);
    }
    return c.ok;
}

alloc_policy_t heap_policy(heap_t *h) { return h->policy; }

const char *alloc_policy_name(alloc_policy_t p)
{
    static const char *n[] = {"first-fit", "next-fit", "best-fit", "worst-fit", "buddy"};
    return p < ALLOC_POLICY_COUNT ? n[p] : "?";
}

bool alloc_policy_parse(const char *s, alloc_policy_t *out)
{
    for (int i = 0; i < ALLOC_POLICY_COUNT; i++)
        if (!strcmp(s, alloc_policy_name((alloc_policy_t)i))) {
            *out = (alloc_policy_t)i;
            return true;
        }
    return false;
}

void *pmalloc(size_t n)
{
    if (!g_heap)
        return malloc(n);
    void *p = heap_alloc(g_heap, n);
    if (!p)
        LOGW("custom heap exhausted (%zu bytes requested)", n);
    return p;
}

void *pcalloc(size_t n)
{
    void *p = pmalloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}

void pfree(void *p)
{
    if (!g_heap) {
        free(p);
        return;
    }
    heap_free(g_heap, p);
}
