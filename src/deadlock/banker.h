/* banker.h - Dijkstra's Banker's algorithm, applied to an actual bank.
 *
 * Resources : the bank's loan fund pools (home, personal, student, business),
 *             measured in paise.
 * Processes : customers holding sanctioned credit lines.
 * Max       : a customer's sanctioned limit per pool (what they may draw).
 * Allocation: what they have drawn and not yet repaid.
 * Need      : Max - Allocation.
 *
 * A disbursement (DRAW) is granted only if, after pretending to grant it, there
 * is still an order in which every customer can draw their full remaining
 * sanction and repay (a "safe sequence"). Otherwise the request is deferred, and
 * it is re-evaluated whenever someone repays. A new sanction is refused if it
 * would make the current state unsafe, because the bank could no longer honour
 * every credit line it has promised. Repayment lowers both Allocation and Max,
 * as with a term loan that cannot be redrawn. */
#ifndef PLUTUS_BANKER_H
#define PLUTUS_BANKER_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "../common/util.h"

#define BK_MAX_RES 4

typedef enum { BK_OK, BK_EXCEEDS_NEED, BK_EXCEEDS_TOTAL, BK_WAIT, BK_UNSAFE } bk_result_t;

typedef struct {
    uint64_t cust;
    int64_t max[BK_MAX_RES];
    int64_t alloc[BK_MAX_RES];
} bk_row_t;

typedef struct {
    pthread_mutex_t m;
    int nres;
    const char *res_names[BK_MAX_RES];
    int64_t total[BK_MAX_RES];
    int64_t available[BK_MAX_RES];
    bk_row_t *rows;
    int nrows, cap;
    uint64_t safety_checks, grants, waits, unsafe_denials, sanction_denials;
} banker_t;

void bk_init(banker_t *b, int nres, const int64_t total[], const char *const names[]);
void bk_destroy(banker_t *b);

/* Bootstraps the state from persisted loans, without a safety check. */
void bk_load(banker_t *b, uint64_t cust, int res, int64_t max, int64_t alloc);

bk_result_t bk_raise_max(banker_t *b, uint64_t cust, int res, int64_t amount);
bk_result_t bk_request(banker_t *b, uint64_t cust, int res, int64_t amount);
/* repay: lowers Allocation and Max by `amount` */
void bk_repay(banker_t *b, uint64_t cust, int res, int64_t amount);
/* cancel an undrawn part of a sanction */
void bk_lower_max(banker_t *b, uint64_t cust, int res, int64_t amount);

bool bk_is_safe(banker_t *b, uint64_t *seq, int *seq_len);
void bk_render(banker_t *b, sbuf_t *out);
const char *bk_result_name(bk_result_t r);

/* The pure algorithm: n processes, m resources, row-major matrices. Fills
 * seq with process indices in a safe order. Returns true when safe. */
bool bk_safety(int n, int m, const int64_t *avail, const int64_t *max, const int64_t *alloc, int *seq);

#endif
