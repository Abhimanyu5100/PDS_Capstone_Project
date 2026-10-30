#include "banker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool bk_safety(int n, int m, const int64_t *avail, const int64_t *max, const int64_t *alloc, int *seq)
{
    int64_t work[BK_MAX_RES];
    memcpy(work, avail, sizeof(int64_t) * (size_t)m);
    bool *finish = calloc((size_t)(n ? n : 1), sizeof(bool));
    int count = 0;
    bool progress = true;
    while (count < n && progress) {
        progress = false;
        for (int i = 0; i < n; i++) {
            if (finish[i])
                continue;
            bool fits = true;
            for (int r = 0; r < m; r++)
                if (max[i * m + r] - alloc[i * m + r] > work[r]) {
                    fits = false;
                    break;
                }
            if (!fits)
                continue;
            /* customer i can draw its remaining need, finish, and repay everything */
            for (int r = 0; r < m; r++)
                work[r] += alloc[i * m + r];
            finish[i] = true;
            if (seq)
                seq[count] = i;
            count++;
            progress = true;
        }
    }
    free(finish);
    return count == n;
}

void bk_init(banker_t *b, int nres, const int64_t total[], const char *const names[])
{
    memset(b, 0, sizeof *b);
    pthread_mutex_init(&b->m, NULL);
    b->nres = nres > BK_MAX_RES ? BK_MAX_RES : nres;
    for (int r = 0; r < b->nres; r++) {
        b->total[r] = b->available[r] = total[r];
        b->res_names[r] = names ? names[r] : "R";
    }
}

void bk_destroy(banker_t *b)
{
    free(b->rows);
    b->rows = NULL;
    b->nrows = b->cap = 0;
}

static bk_row_t *row_get(banker_t *b, uint64_t cust, bool create)
{
    for (int i = 0; i < b->nrows; i++)
        if (b->rows[i].cust == cust)
            return &b->rows[i];
    if (!create)
        return NULL;
    if (b->nrows == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 16;
        b->rows = realloc(b->rows, sizeof(bk_row_t) * (size_t)b->cap);
        if (!b->rows)
            die("out of memory");
    }
    bk_row_t *r = &b->rows[b->nrows++];
    memset(r, 0, sizeof *r);
    r->cust = cust;
    return r;
}

/* drop rows with no remaining claim, so finished customers leave the matrix */
static void row_gc(banker_t *b, bk_row_t *row)
{
    for (int r = 0; r < b->nres; r++)
        if (row->max[r] || row->alloc[r])
            return;
    *row = b->rows[--b->nrows];
}

static bool safe_locked(banker_t *b, int *seq)
{
    b->safety_checks++;
    int n = b->nrows, m = b->nres;
    int64_t *max = malloc(sizeof(int64_t) * (size_t)(n * m + 1));
    int64_t *alloc = malloc(sizeof(int64_t) * (size_t)(n * m + 1));
    for (int i = 0; i < n; i++)
        for (int r = 0; r < m; r++) {
            max[i * m + r] = b->rows[i].max[r];
            alloc[i * m + r] = b->rows[i].alloc[r];
        }
    bool ok = bk_safety(n, m, b->available, max, alloc, seq);
    free(max);
    free(alloc);
    return ok;
}

void bk_load(banker_t *b, uint64_t cust, int res, int64_t max, int64_t alloc)
{
    pthread_mutex_lock(&b->m);
    bk_row_t *row = row_get(b, cust, true);
    row->max[res] += max;
    row->alloc[res] += alloc;
    b->available[res] -= alloc;
    pthread_mutex_unlock(&b->m);
}

bk_result_t bk_raise_max(banker_t *b, uint64_t cust, int res, int64_t amount)
{
    pthread_mutex_lock(&b->m);
    bk_row_t *row = row_get(b, cust, true);
    if (row->max[res] + amount > b->total[res]) {
        b->sanction_denials++;
        row_gc(b, row);
        pthread_mutex_unlock(&b->m);
        return BK_EXCEEDS_TOTAL;
    }
    row->max[res] += amount;
    int *seq = malloc(sizeof(int) * (size_t)(b->nrows + 1));
    bool ok = safe_locked(b, seq);
    free(seq);
    if (!ok) {
        row = row_get(b, cust, false);
        row->max[res] -= amount;
        row_gc(b, row);
        b->sanction_denials++;
        pthread_mutex_unlock(&b->m);
        return BK_UNSAFE;
    }
    pthread_mutex_unlock(&b->m);
    return BK_OK;
}

bk_result_t bk_request(banker_t *b, uint64_t cust, int res, int64_t amount)
{
    pthread_mutex_lock(&b->m);
    bk_row_t *row = row_get(b, cust, false);
    if (!row || amount > row->max[res] - row->alloc[res]) {
        pthread_mutex_unlock(&b->m);
        return BK_EXCEEDS_NEED;
    }
    if (amount > b->available[res]) {
        b->waits++;
        pthread_mutex_unlock(&b->m);
        return BK_WAIT;
    }
    /* pretend to allocate, then test safety */
    b->available[res] -= amount;
    row->alloc[res] += amount;
    int *seq = malloc(sizeof(int) * (size_t)(b->nrows + 1));
    bool ok = safe_locked(b, seq);
    free(seq);
    if (!ok) {
        b->available[res] += amount;
        row->alloc[res] -= amount;
        b->unsafe_denials++;
        pthread_mutex_unlock(&b->m);
        return BK_UNSAFE;
    }
    b->grants++;
    pthread_mutex_unlock(&b->m);
    return BK_OK;
}

void bk_repay(banker_t *b, uint64_t cust, int res, int64_t amount)
{
    pthread_mutex_lock(&b->m);
    bk_row_t *row = row_get(b, cust, false);
    if (row) {
        if (amount > row->alloc[res])
            amount = row->alloc[res];
        row->alloc[res] -= amount;
        row->max[res] -= amount;
        b->available[res] += amount;
        row_gc(b, row);
    }
    pthread_mutex_unlock(&b->m);
}

void bk_lower_max(banker_t *b, uint64_t cust, int res, int64_t amount)
{
    pthread_mutex_lock(&b->m);
    bk_row_t *row = row_get(b, cust, false);
    if (row) {
        int64_t undrawn = row->max[res] - row->alloc[res];
        row->max[res] -= amount < undrawn ? amount : undrawn;
        row_gc(b, row);
    }
    pthread_mutex_unlock(&b->m);
}

bool bk_is_safe(banker_t *b, uint64_t *seq_out, int *seq_len)
{
    pthread_mutex_lock(&b->m);
    int *seq = malloc(sizeof(int) * (size_t)(b->nrows + 1));
    bool ok = safe_locked(b, seq);
    if (seq_out && seq_len) {
        int n = ok ? b->nrows : 0;
        if (n > *seq_len)
            n = *seq_len;
        for (int i = 0; i < n; i++)
            seq_out[i] = b->rows[seq[i]].cust;
        *seq_len = n;
    }
    free(seq);
    pthread_mutex_unlock(&b->m);
    return ok;
}

static void lakh(int64_t paise, char *out, size_t n)
{
    snprintf(out, n, "%.2fL", (double)paise / 100.0 / 100000.0);
}

void bk_render(banker_t *b, sbuf_t *out)
{
    pthread_mutex_lock(&b->m);
    char t[32];
    sb_printf(out, "Banker's algorithm - loan fund pools (L = lakh rupees)\n");
    sb_printf(out, "  %-12s", "");
    for (int r = 0; r < b->nres; r++)
        sb_printf(out, " %12s", b->res_names[r]);
    sb_printf(out, "\n  %-12s", "Total");
    for (int r = 0; r < b->nres; r++) {
        lakh(b->total[r], t, sizeof t);
        sb_printf(out, " %12s", t);
    }
    sb_printf(out, "\n  %-12s", "Available");
    for (int r = 0; r < b->nres; r++) {
        lakh(b->available[r], t, sizeof t);
        sb_printf(out, " %12s", t);
    }
    sb_printf(out, "\n\n  %-12s %-40s %-40s\n", "Customer", "Max (sanctioned)", "Allocation (drawn)");
    int shown = 0;
    for (int i = 0; i < b->nrows && shown < 30; i++, shown++) {
        sb_printf(out, "  %-12llu", (unsigned long long)b->rows[i].cust);
        for (int r = 0; r < b->nres; r++) {
            lakh(b->rows[i].max[r], t, sizeof t);
            sb_printf(out, " %9s", t);
        }
        sb_printf(out, "    ");
        for (int r = 0; r < b->nres; r++) {
            lakh(b->rows[i].alloc[r], t, sizeof t);
            sb_printf(out, " %9s", t);
        }
        sb_printf(out, "\n");
    }
    if (b->nrows > shown)
        sb_printf(out, "  ... %d more customers\n", b->nrows - shown);
    int *seq = malloc(sizeof(int) * (size_t)(b->nrows + 1));
    bool ok = safe_locked(b, seq);
    sb_printf(out, "\n  State: %s", ok ? "SAFE" : "UNSAFE");
    if (ok && b->nrows) {
        sb_printf(out, "   safe sequence: <");
        for (int i = 0; i < b->nrows && i < 12; i++)
            sb_printf(out, "%s%llu", i ? ", " : "", (unsigned long long)b->rows[seq[i]].cust);
        sb_printf(out, "%s>", b->nrows > 12 ? ", ..." : "");
    }
    free(seq);
    sb_printf(out,
              "\n  safety checks %llu | grants %llu | waits (insufficient funds) %llu | "
              "deferred as unsafe %llu | sanctions refused %llu\n",
              (unsigned long long)b->safety_checks, (unsigned long long)b->grants,
              (unsigned long long)b->waits, (unsigned long long)b->unsafe_denials,
              (unsigned long long)b->sanction_denials);
    pthread_mutex_unlock(&b->m);
}

const char *bk_result_name(bk_result_t r)
{
    switch (r) {
    case BK_OK: return "granted";
    case BK_EXCEEDS_NEED: return "exceeds sanctioned limit";
    case BK_EXCEEDS_TOTAL: return "exceeds the fund pool";
    case BK_WAIT: return "insufficient funds available";
    case BK_UNSAFE: return "would leave the bank in an unsafe state";
    }
    return "?";
}
