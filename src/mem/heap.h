/* heap.h - a custom heap allocator over a private mmap'd region.
 *
 * Two allocator families live behind one interface:
 *
 *  1. Free-list allocators (first / next / best / worst fit). Every block has a
 *     16-byte header and an 8-byte footer ("boundary tags") so that free() can
 *     coalesce with both physical neighbours in O(1). Free blocks sit on an
 *     address-ordered, doubly linked explicit free list.
 *
 *  2. A binary buddy allocator. Blocks are powers of two; a block's buddy is
 *     found by XOR-ing its offset with its size, and free buddies merge
 *     recursively. External fragmentation is low, internal fragmentation is high.
 *
 * The daemon allocates its jobs, transactions, session objects and account cache
 * entries from this heap (see pmalloc/pfree), so the fragmentation statistics
 * reported by `plutus-admin memstat` come from real traffic. */
#ifndef PLUTUS_HEAP_H
#define PLUTUS_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ALLOC_FIRST_FIT,
    ALLOC_NEXT_FIT,
    ALLOC_BEST_FIT,
    ALLOC_WORST_FIT,
    ALLOC_BUDDY,
    ALLOC_POLICY_COUNT
} alloc_policy_t;

typedef struct heap heap_t;

typedef struct {
    alloc_policy_t policy;
    size_t capacity;        /* bytes managed */
    size_t used_bytes;      /* bytes in allocated blocks, including headers */
    size_t requested_bytes; /* bytes callers asked for */
    size_t free_bytes;
    size_t largest_free;
    uint64_t free_blocks, used_blocks;
    uint64_t allocs, frees, failures, splits, coalesces;
    uint64_t search_steps;  /* free-list nodes examined, a cost measure */
    double external_frag;   /* 1 - largest_free / free_bytes */
    double internal_frag;   /* 1 - requested / used */
} heap_stats_t;

heap_t *heap_create(size_t bytes, alloc_policy_t policy);
void heap_destroy(heap_t *h);
void *heap_alloc(heap_t *h, size_t n);
void *heap_calloc(heap_t *h, size_t n);
void heap_free(heap_t *h, void *p);
void heap_stats(heap_t *h, heap_stats_t *out);
/* ASCII picture of the region: '#' allocated, '.' free, one char per bucket */
void heap_map(heap_t *h, char *out, size_t width);
/* Walks every block and verifies headers, footers and the free list. */
bool heap_check(heap_t *h, char *why, size_t why_cap);
alloc_policy_t heap_policy(heap_t *h);

const char *alloc_policy_name(alloc_policy_t p);
bool alloc_policy_parse(const char *s, alloc_policy_t *out);

/* The daemon-wide heap. Falls back to malloc when no heap is installed
 * (unit tests, client binaries). */
extern heap_t *g_heap;
void *pmalloc(size_t n);
void *pcalloc(size_t n);
void pfree(void *p);

#endif
