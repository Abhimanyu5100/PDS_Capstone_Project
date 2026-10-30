/* bank.h - Plutus Fincorp banking operations as ACID transactions.
 *
 * Each operation:
 *   1. begins a transaction in the lock manager and locks every account it
 *      touches (a deadlock victim rolls back and retries automatically)
 *   2. reads the current records through the buffer pool
 *   3. validates and builds new after-images
 *   4. commits through the WAL (durable before it is visible)
 *   5. updates the in-memory index and the statement history, then unlocks
 *
 * The in-memory index (account number -> record location, plus each account's
 * FDs, loans and policies) is protected by a readers-writers lock whose
 * preference policy can be switched at run time. Index entries are allocated
 * from the custom heap. */
#ifndef PLUTUS_BANK_H
#define PLUTUS_BANK_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "../common/sync.h"
#include "../common/util.h"
#include "../deadlock/banker.h"
#include "../deadlock/lockmgr.h"
#include "../storage/db.h"
#include "rules.h"

enum { REC_ACCOUNT = 1, REC_FD = 2, REC_LOAN = 3, REC_POLICY = 4, REC_TYPES = 5 };

/* status codes returned by every operation */
typedef enum {
    B_OK = 0,
    B_PENDING,  /* accepted but deferred (Banker's algorithm) */
    B_BADREQ,
    B_NOTFOUND,
    B_AUTH,
    B_LOCKED,
    B_FUNDS,
    B_LIMIT,
    B_DENIED,
    B_INTERNAL
} bstatus_t;
const char *bstatus_name(bstatus_t s);

typedef struct {
    char name[48];
    char city[32];
    int32_t age;
    int32_t tier;
    int64_t phone;
    uint8_t salt[16];
    uint8_t hash[32];
    int64_t balance;
    int64_t created;
    int32_t open_month;
    int32_t pad;
} acct_body_t;

enum { FD_ACTIVE = 1, FD_CLOSED = 2 };
typedef struct {
    int64_t principal;
    int32_t months;
    int32_t senior;
    int32_t start_month;
    int32_t status;
    int64_t payout;
    int64_t opened;
    double rate;
} fd_body_t;

enum { LOAN_SANCTIONED = 1, LOAN_CLOSED = 2 };
typedef struct {
    int32_t type;
    int32_t cibil;
    int32_t rate;
    int32_t tenure;
    int64_t sanctioned;
    int64_t drawn;
    int64_t repaid;
    int64_t emi;
    int64_t income;
    int32_t status;
    int32_t pad;
} loan_body_t;

enum { POL_ACTIVE = 1, POL_LAPSED = 2 };
typedef struct {
    int32_t kind;
    int32_t status;
    char holder[48];
    char beneficiary[48];
    int64_t cover;
    int64_t premium;
    int32_t start_month;
    int32_t paid_months;
    int64_t detail1, detail2; /* age/smoker, or car value/car age */
} policy_body_t;

typedef struct idx_entry {
    uint64_t key;
    uint64_t rid;
    uint64_t owner;
    int type;
    int tier;          /* accounts: cached for scheduling priority */
    int failed_logins; /* accounts */
    int nchild, capchild;
    uint64_t *children; /* accounts: keys of FDs, loans, policies */
    struct idx_entry *next;
} idx_entry_t;

#define IDX_BUCKETS 4096

typedef struct pending_draw {
    uint64_t id, acct, loan;
    int64_t amount;
    int64_t queued_at;
    struct pending_draw *next;
} pending_draw_t;

typedef struct {
    int frames;
    repl_policy_t repl;
    wal_sync_t sync;
    dl_policy_t deadlock;
    int64_t month_secs;         /* real seconds per simulated bank month */
    int64_t pools[LOAN_TYPES];  /* loan fund pools, paise */
} bank_config_t;

typedef struct bank {
    db_t db;
    lockmgr_t lm;
    banker_t bk;
    rwlock_t idx_lock;
    idx_entry_t *idx[IDX_BUCKETS];
    uint64_t naccounts;
    pthread_mutex_t seq_m;
    uint64_t seq[REC_TYPES];
    _Atomic uint64_t txn_seq;
    int hist_fd;
    pthread_mutex_t hist_m;
    int64_t month_secs, epoch;
    _Atomic int64_t ledger; /* the sum of all balances, as it should be */
    _Atomic bool race_mode;
    _Atomic int64_t race_delay_us;
    pthread_mutex_t pend_m;
    pending_draw_t *pending;
    uint64_t pending_seq;
    _Atomic uint64_t ops[16];
    _Atomic uint64_t deadlock_retries;
} bank_t;

int bank_open(bank_t *b, const char *dir, const bank_config_t *cfg);
void bank_close(bank_t *b);
int bank_month(bank_t *b);
int bank_account_tier(bank_t *b, uint64_t acct); /* -1 if unknown */
bool bank_account_exists(bank_t *b, uint64_t acct);

/* every operation appends human-readable lines to `out` */
bstatus_t bank_register(bank_t *b, const char *name, const char *city, int age, int64_t phone,
                        const char *pw, int64_t deposit, sbuf_t *out, uint64_t *acct_out);
bstatus_t bank_login(bank_t *b, uint64_t acct, const char *pw, sbuf_t *out);
bstatus_t bank_balance(bank_t *b, uint64_t acct, sbuf_t *out);
bstatus_t bank_deposit(bank_t *b, uint64_t acct, int64_t amount, sbuf_t *out);
bstatus_t bank_withdraw(bank_t *b, uint64_t acct, int64_t amount, sbuf_t *out);
bstatus_t bank_transfer(bank_t *b, uint64_t from, uint64_t to, int64_t amount, sbuf_t *out);
bstatus_t bank_statement(bank_t *b, uint64_t acct, int n, sbuf_t *out);

bstatus_t bank_fd_quote(int64_t amount, int months, bool senior, sbuf_t *out);
bstatus_t bank_fd_open(bank_t *b, uint64_t acct, int64_t amount, int months, bool senior, sbuf_t *out);
bstatus_t bank_fd_list(bank_t *b, uint64_t acct, sbuf_t *out);
bstatus_t bank_fd_close(bank_t *b, uint64_t acct, uint64_t fd_no, sbuf_t *out);

bstatus_t bank_loan_quote(int cibil, loan_type_t t, int lakhs, int tenure, int64_t income, sbuf_t *out);
bstatus_t bank_loan_apply(bank_t *b, uint64_t acct, int cibil, loan_type_t t, int lakhs, int tenure,
                          int64_t income, sbuf_t *out);
bstatus_t bank_loan_list(bank_t *b, uint64_t acct, sbuf_t *out);
bstatus_t bank_loan_draw(bank_t *b, uint64_t acct, uint64_t loan_no, int64_t amount, sbuf_t *out);
bstatus_t bank_loan_repay(bank_t *b, uint64_t acct, uint64_t loan_no, int64_t amount, sbuf_t *out);

bstatus_t bank_ins_quote(ins_kind_t k, int a1, int64_t a2, int a3, sbuf_t *out);
bstatus_t bank_ins_buy(bank_t *b, uint64_t acct, ins_kind_t k, const char *holder,
                       const char *beneficiary, int a1, int64_t a2, int a3, sbuf_t *out);
bstatus_t bank_ins_list(bank_t *b, uint64_t acct, sbuf_t *out);

/* monthly batch: savings interest and premium collection for every account */
bstatus_t bank_month_end(bank_t *b, sbuf_t *out);
/* sums every balance and compares it with the ledger */
bool bank_audit(bank_t *b, int64_t *actual, int64_t *expected, uint64_t *accounts);
/* writes off any audit difference (e.g. money lost in race mode); returns it */
int64_t bank_reconcile(bank_t *b);
void bank_list_accounts(bank_t *b, int limit, sbuf_t *out);
void bank_render_pending(bank_t *b, sbuf_t *out);

#endif
