#include "bank.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../common/crypto.h"
#include "../mem/heap.h"

#define BODY(rec, T) ((T *)(void *)(rec)->body)
#define MAX_TXN_MONEY (1 * CRORE)
#define MAX_DEADLOCK_RETRIES 64

/* every money-moving transaction holds this as a reader from commit to ledger
 * update; the auditor takes it as a writer to see a consistent snapshot */
static rwlock_t g_audit_gate;

enum { H_OPEN, H_DEPOSIT, H_WITHDRAW, H_XFER_OUT, H_XFER_IN, H_FD_OPEN, H_FD_CLOSE, H_LOAN_DRAW,
       H_LOAN_REPAY, H_PREMIUM, H_INTEREST };
static const char *hist_names[] = {"OPEN",     "DEPOSIT",   "WITHDRAW",  "TRANSFER-OUT",
                                   "TRANSFER-IN", "FD-OPEN", "FD-CLOSE",  "LOAN-DRAW",
                                   "LOAN-REPAY", "PREMIUM",  "INTEREST"};

typedef struct {
    int64_t ts;
    uint64_t acct;
    int64_t amount;
    int64_t balance;
    uint64_t ref;
    int32_t kind;
    int32_t month;
    char memo[40];
} hist_t;

const char *bstatus_name(bstatus_t s)
{
    static const char *n[] = {"OK",     "PENDING", "BAD_REQUEST", "NOT_FOUND", "AUTH",
                              "LOCKED", "FUNDS",   "LIMIT",       "DENIED",    "INTERNAL"};
    return s <= B_INTERNAL ? n[s] : "?";
}

/* ------------------------------------------------------------------ index */

static unsigned idx_hash(uint64_t key) { return (unsigned)((key * 0x9E3779B97F4A7C15ull) >> 52) % IDX_BUCKETS; }

static idx_entry_t *idx_find(bank_t *b, uint64_t key)
{
    for (idx_entry_t *e = b->idx[idx_hash(key)]; e; e = e->next)
        if (e->key == key)
            return e;
    return NULL;
}

static idx_entry_t *idx_insert_locked(bank_t *b, uint64_t key, uint64_t rid, uint64_t owner, int type)
{
    idx_entry_t *e = pcalloc(sizeof *e);
    if (!e)
        die("index: custom heap exhausted");
    e->key = key;
    e->rid = rid;
    e->owner = owner;
    e->type = type;
    unsigned h = idx_hash(key);
    e->next = b->idx[h];
    b->idx[h] = e;
    if (type == REC_ACCOUNT)
        b->naccounts++;
    return e;
}

static void idx_add_child_locked(idx_entry_t *owner, uint64_t child)
{
    if (owner->nchild == owner->capchild) {
        int cap = owner->capchild ? owner->capchild * 2 : 4;
        uint64_t *n = pmalloc(sizeof(uint64_t) * (size_t)cap);
        if (!n)
            die("index: custom heap exhausted");
        if (owner->children) {
            memcpy(n, owner->children, sizeof(uint64_t) * (size_t)owner->nchild);
            pfree(owner->children);
        }
        owner->children = n;
        owner->capchild = cap;
    }
    owner->children[owner->nchild++] = child;
}

/* returns the record location for key (0 if absent), checking its type */
static uint64_t idx_rid(bank_t *b, uint64_t key, int type, uint64_t *owner)
{
    rw_rdlock(&b->idx_lock);
    idx_entry_t *e = idx_find(b, key);
    uint64_t rid = e && e->type == type ? e->rid : 0;
    if (e && owner)
        *owner = e->owner;
    rw_rdunlock(&b->idx_lock);
    return rid;
}

static void idx_set_tier(bank_t *b, uint64_t acct, int tier)
{
    rw_wrlock(&b->idx_lock);
    idx_entry_t *e = idx_find(b, acct);
    if (e)
        e->tier = tier;
    rw_wrunlock(&b->idx_lock);
}

int bank_account_tier(bank_t *b, uint64_t acct)
{
    rw_rdlock(&b->idx_lock);
    idx_entry_t *e = idx_find(b, acct);
    int t = e && e->type == REC_ACCOUNT ? e->tier : -1;
    rw_rdunlock(&b->idx_lock);
    return t;
}

bool bank_account_exists(bank_t *b, uint64_t acct) { return bank_account_tier(b, acct) >= 0; }

/* children of an account, copied out under the read lock */
static int idx_children(bank_t *b, uint64_t acct, uint64_t *out, int max)
{
    rw_rdlock(&b->idx_lock);
    idx_entry_t *e = idx_find(b, acct);
    int n = 0;
    if (e)
        for (int i = 0; i < e->nchild && n < max; i++)
            out[n++] = e->children[i];
    rw_rdunlock(&b->idx_lock);
    return n;
}

/* ------------------------------------------------------------------ keys, history */

static const uint64_t key_base[REC_TYPES] = {0, 100000000ull, 2000000000ull, 3000000000ull, 4000000000ull};

static uint64_t key_seq(int type, uint64_t key)
{
    return type == REC_ACCOUNT ? key / 10 - key_base[type] : key - key_base[type];
}

static uint64_t new_key(bank_t *b, int type)
{
    pthread_mutex_lock(&b->seq_m);
    uint64_t s = b->seq[type]++;
    pthread_mutex_unlock(&b->seq_m);
    if (type == REC_ACCOUNT) {
        uint64_t base = key_base[type] + s;
        return base * 10 + (uint64_t)luhn_digit(base); /* 10 digits, self-checking */
    }
    return key_base[type] + s;
}

static uint64_t next_txn(bank_t *b) { return atomic_fetch_add(&b->txn_seq, 1); }

int bank_month(bank_t *b)
{
    int64_t m = (wall_time() - b->epoch) / (b->month_secs > 0 ? b->month_secs : 60);
    return m < 0 ? 0 : (int)m;
}

static void history(bank_t *b, uint64_t acct, int kind, int64_t amount, int64_t balance, uint64_t ref,
                    const char *memo)
{
    hist_t h = {wall_time(), acct, amount, balance, ref, kind, bank_month(b), {0}};
    if (memo)
        snprintf(h.memo, sizeof h.memo, "%s", memo);
    pthread_mutex_lock(&b->hist_m);
    ssize_t w = write(b->hist_fd, &h, sizeof h);
    pthread_mutex_unlock(&b->hist_m);
    (void)w;
}

/* ------------------------------------------------------------------ open */

typedef struct {
    bank_t *b;
} scan_ctx;

static void scan_cb(void *ctx, uint64_t rid, const record_t *rec)
{
    bank_t *b = ((scan_ctx *)ctx)->b;
    if (rec->type <= 0 || rec->type >= REC_TYPES)
        return;
    idx_entry_t *e = idx_insert_locked(b, rec->key, rid, rec->owner, rec->type);
    uint64_t s = key_seq(rec->type, rec->key) + 1;
    if (s > b->seq[rec->type])
        b->seq[rec->type] = s;
    if (rec->type == REC_ACCOUNT) {
        const acct_body_t *a = (const acct_body_t *)(const void *)rec->body;
        e->tier = a->tier;
        b->ledger += a->balance;
    } else if (rec->type == REC_LOAN) {
        const loan_body_t *l = (const loan_body_t *)(const void *)rec->body;
        if (l->status == LOAN_SANCTIONED)
            bk_load(&b->bk, rec->owner, l->type, l->sanctioned - l->repaid, l->drawn - l->repaid);
    }
}

int bank_open(bank_t *b, const char *dir, const bank_config_t *cfg)
{
    memset(b, 0, sizeof *b);
    rw_init(&g_audit_gate, RW_WRITER_PREF);
    if (db_open(&b->db, dir, cfg->frames, cfg->repl, cfg->sync) != 0)
        return -1;
    lm_init(&b->lm, cfg->deadlock);
    lm_start_detector(&b->lm, 50 * 1000000LL);
    static const char *pool_names[LOAN_TYPES] = {"home", "personal", "student", "business"};
    bk_init(&b->bk, LOAN_TYPES, cfg->pools, pool_names);
    rw_init(&b->idx_lock, RW_WRITER_PREF);
    pthread_mutex_init(&b->seq_m, NULL);
    pthread_mutex_init(&b->hist_m, NULL);
    pthread_mutex_init(&b->pend_m, NULL);
    for (int t = 1; t < REC_TYPES; t++)
        b->seq[t] = 1;
    b->txn_seq = 1;
    b->month_secs = cfg->month_secs;
    b->epoch = b->db.sb.created;
    b->race_delay_us = 2000;

    scan_ctx c = {b};
    db_scan(&b->db, scan_cb, &c);
    /* attach FDs, loans and policies to their owning accounts */
    for (int h = 0; h < IDX_BUCKETS; h++)
        for (idx_entry_t *e = b->idx[h]; e; e = e->next)
            if (e->type != REC_ACCOUNT) {
                idx_entry_t *o = idx_find(b, e->owner);
                if (o)
                    idx_add_child_locked(o, e->key);
            }

    char path[512];
    snprintf(path, sizeof path, "%s/history.log", dir);
    b->hist_fd = open(path, O_RDWR | O_CREAT | O_APPEND, 0600);
    if (b->hist_fd < 0)
        return -1;
    LOGI("bank open: %llu accounts, ledger %lld paise, month %d", (unsigned long long)b->naccounts,
         (long long)b->ledger, bank_month(b));
    return 0;
}

void bank_close(bank_t *b)
{
    lm_stop(&b->lm);
    db_close(&b->db);
    close(b->hist_fd);
    bk_destroy(&b->bk);
}

/* ------------------------------------------------------------------ txn helpers */

typedef struct {
    bank_t *b;
    txn_t t;
    db_write_t w[16];
    int nw;
} op_t;

static bool op_lock(op_t *op, const uint64_t *accts, int n)
{
    for (int tries = 0; tries < MAX_DEADLOCK_RETRIES; tries++) {
        if (lm_lock_set(&op->b->lm, &op->t, accts, n) == LM_OK)
            return true;
        atomic_fetch_add(&op->b->deadlock_retries, 1);
    }
    return false;
}

static void op_begin(op_t *op, bank_t *b, const char *label)
{
    op->b = b;
    op->nw = 0;
    txn_begin(&b->lm, &op->t, next_txn(b), label);
}

static void op_end(op_t *op) { txn_end(&op->b->lm, &op->t); }

static record_t *op_stage(op_t *op, uint64_t rid, const record_t *rec)
{
    for (int i = 0; i < op->nw; i++)
        if (op->w[i].rid == rid) {
            op->w[i].rec = *rec;
            return &op->w[i].rec;
        }
    op->w[op->nw].rid = rid;
    op->w[op->nw].rec = *rec;
    return &op->w[op->nw++].rec;
}

static bool op_commit(op_t *op) { return db_commit(&op->b->db, op->t.id, op->w, op->nw) == 0; }

static bool read_account(bank_t *b, uint64_t acct, uint64_t *rid, record_t *rec)
{
    *rid = idx_rid(b, acct, REC_ACCOUNT, NULL);
    if (!*rid)
        return false;
    db_read(&b->db, *rid, rec);
    return rec->type == REC_ACCOUNT && rec->key == acct;
}

static void refresh_tier(bank_t *b, record_t *acct_rec)
{
    acct_body_t *a = BODY(acct_rec, acct_body_t);
    a->tier = tier_for_balance(a->balance);
}

#define FAIL(code, ...)                  \
    do {                                 \
        sb_printf(out, __VA_ARGS__);     \
        sb_putc(out, '\n');              \
        return (code);                   \
    } while (0)

/* ------------------------------------------------------------------ accounts */

bstatus_t bank_register(bank_t *b, const char *name, const char *city, int age, int64_t phone,
                        const char *pw, int64_t deposit, sbuf_t *out, uint64_t *acct_out)
{
    atomic_fetch_add(&b->ops[0], 1);
    if (!valid_person_name(name))
        FAIL(B_BADREQ, "Name must contain letters and spaces only.");
    if (!city[0] || strlen(city) > 31)
        FAIL(B_BADREQ, "Please enter a valid city.");
    if (age > 0 && age < 18)
        FAIL(B_DENIED, "You are under 18 years of age and are not eligible to get a bank account.");
    if (!valid_age(age))
        FAIL(B_BADREQ, "Please enter a valid age.");
    if (!valid_phone(phone))
        FAIL(B_BADREQ, "Invalid mobile number (10 digits, starting with 6-9).");
    if (!valid_password(pw))
        FAIL(B_BADREQ, "Password must be between 6 and 12 characters.");
    if (deposit < 0 || deposit > MAX_TXN_MONEY)
        FAIL(B_LIMIT, "Initial deposit must be between Rs 0 and Rs 1 crore.");

    record_t rec;
    memset(&rec, 0, sizeof rec);
    rec.type = REC_ACCOUNT;
    acct_body_t *a = BODY(&rec, acct_body_t);
    snprintf(a->name, sizeof a->name, "%s", name);
    snprintf(a->city, sizeof a->city, "%s", city);
    a->age = age;
    a->phone = phone;
    random_bytes(a->salt, sizeof a->salt);
    password_hash(pw, a->salt, a->hash); /* the CPU-heavy part of this job */
    a->balance = deposit;
    a->tier = tier_for_balance(deposit);
    a->created = wall_time();
    a->open_month = bank_month(b);
    uint64_t acct = new_key(b, REC_ACCOUNT);
    rec.key = rec.owner = acct;

    op_t op;
    op_begin(&op, b, "register");
    op_lock(&op, &acct, 1);
    uint64_t rid = db_alloc_rid(&b->db);
    op_stage(&op, rid, &rec);
    rw_rdlock(&g_audit_gate);
    if (!op_commit(&op)) {
        rw_rdunlock(&g_audit_gate);
        db_release_rid(&b->db, rid);
        op_end(&op);
        FAIL(B_INTERNAL, "Could not persist the account.");
    }
    rw_wrlock(&b->idx_lock);
    idx_entry_t *e = idx_insert_locked(b, acct, rid, acct, REC_ACCOUNT);
    e->tier = a->tier;
    rw_wrunlock(&b->idx_lock);
    b->ledger += deposit;
    rw_rdunlock(&g_audit_gate);
    op_end(&op);
    history(b, acct, H_OPEN, deposit, deposit, 0, "account opened");
    char m[48];
    sb_printf(out, "Account created.\n");
    sb_printf(out, "ACCOUNT DETAILS:\n");
    sb_printf(out, "  Account number : %llu\n", (unsigned long long)acct);
    sb_printf(out, "  Name           : %s\n", a->name);
    sb_printf(out, "  City           : %s\n", a->city);
    sb_printf(out, "  Age            : %d\n", a->age);
    sb_printf(out, "  Phone          : %lld\n", (long long)a->phone);
    sb_printf(out, "  Balance        : %s\n", fmt_money(deposit, m, sizeof m));
    sb_printf(out, "  Tier           : %s\n", tier_name(a->tier));
    if (acct_out)
        *acct_out = acct;
    return B_OK;
}

bstatus_t bank_login(bank_t *b, uint64_t acct, const char *pw, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[1], 1);
    if (!luhn_valid(acct))
        FAIL(B_AUTH, "Invalid account number (check digit mismatch).");
    rw_rdlock(&b->idx_lock);
    idx_entry_t *e = idx_find(b, acct);
    bool locked = e && e->failed_logins >= 5;
    uint64_t rid = e && e->type == REC_ACCOUNT ? e->rid : 0;
    rw_rdunlock(&b->idx_lock);
    if (!rid)
        FAIL(B_AUTH, "Account number or password is incorrect.");
    if (locked)
        FAIL(B_LOCKED, "Account locked after 5 failed logins. Visit a branch.");
    record_t rec;
    db_read(&b->db, rid, &rec);
    acct_body_t *a = BODY(&rec, acct_body_t);
    bool ok = password_verify(pw, a->salt, a->hash);
    rw_wrlock(&b->idx_lock);
    e = idx_find(b, acct);
    if (e)
        e->failed_logins = ok ? 0 : e->failed_logins + 1;
    int left = e ? 5 - e->failed_logins : 0;
    rw_wrunlock(&b->idx_lock);
    if (!ok)
        FAIL(B_AUTH, "Account number or password is incorrect (%d attempts left).", left);
    char m[48];
    sb_printf(out, "Welcome back, %s!\n", a->name);
    sb_printf(out, "  Tier: %s   Balance: %s\n", tier_name(a->tier), fmt_money(a->balance, m, sizeof m));
    return B_OK;
}

bstatus_t bank_balance(bank_t *b, uint64_t acct, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[2], 1);
    op_t op;
    op_begin(&op, b, "balance");
    if (!op_lock(&op, &acct, 1)) {
        op_end(&op);
        FAIL(B_INTERNAL, "Could not lock the account.");
    }
    uint64_t rid;
    record_t rec;
    bool found = read_account(b, acct, &rid, &rec);
    op_end(&op);
    if (!found)
        FAIL(B_NOTFOUND, "No such account.");
    acct_body_t *a = BODY(&rec, acct_body_t);
    char m[48];
    sb_printf(out, "Account %llu (%s, %s)\n", (unsigned long long)acct, a->name, tier_name(a->tier));
    sb_printf(out, "  Available balance: %s\n", fmt_money(a->balance, m, sizeof m));
    sb_printf(out, "  Bank month: %d\n", bank_month(b));
    return B_OK;
}

/* race mode: deliberately skip the account lock and widen the read-modify-write
 * window, so concurrent deposits lose updates (the auditor will notice) */
static bstatus_t money_move(bank_t *b, uint64_t acct, int64_t delta, int hkind, sbuf_t *out)
{
    if (delta == 0 || llabs(delta) > MAX_TXN_MONEY)
        FAIL(B_LIMIT, "Amount must be between Rs 0.01 and Rs 1 crore.");
    bool race = atomic_load(&b->race_mode);
    op_t op;
    op_begin(&op, b, delta > 0 ? "deposit" : "withdraw");
    if (!race && !op_lock(&op, &acct, 1)) {
        op_end(&op);
        FAIL(B_INTERNAL, "Could not lock the account.");
    }
    uint64_t rid;
    record_t rec;
    if (!read_account(b, acct, &rid, &rec)) {
        op_end(&op);
        FAIL(B_NOTFOUND, "No such account.");
    }
    acct_body_t *a = BODY(&rec, acct_body_t);
    if (a->balance + delta < 0) {
        op_end(&op);
        char m[48];
        FAIL(B_FUNDS, "Insufficient funds: balance is %s.", fmt_money(a->balance, m, sizeof m));
    }
    if (race)
        sleep_us(atomic_load(&b->race_delay_us)); /* another teller reads the same balance here */
    a->balance += delta;
    refresh_tier(b, &rec);
    op_stage(&op, rid, &rec);
    rw_rdlock(&g_audit_gate);
    bool ok = op_commit(&op);
    if (ok)
        b->ledger += delta;
    rw_rdunlock(&g_audit_gate);
    op_end(&op);
    if (!ok)
        FAIL(B_INTERNAL, "Could not persist the transaction.");
    idx_set_tier(b, acct, a->tier);
    history(b, acct, hkind, delta, a->balance, 0, NULL);
    char m1[48], m2[48];
    sb_printf(out, "%s of %s successful.\n", delta > 0 ? "Deposit" : "Withdrawal",
              fmt_money(llabs(delta), m1, sizeof m1));
    sb_printf(out, "  New balance: %s\n", fmt_money(a->balance, m2, sizeof m2));
    return B_OK;
}

bstatus_t bank_deposit(bank_t *b, uint64_t acct, int64_t amount, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[3], 1);
    if (amount <= 0)
        FAIL(B_BADREQ, "Amount must be positive.");
    return money_move(b, acct, amount, H_DEPOSIT, out);
}

bstatus_t bank_withdraw(bank_t *b, uint64_t acct, int64_t amount, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[4], 1);
    if (amount <= 0)
        FAIL(B_BADREQ, "Amount must be positive.");
    return money_move(b, acct, -amount, H_WITHDRAW, out);
}

bstatus_t bank_transfer(bank_t *b, uint64_t from, uint64_t to, int64_t amount, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[5], 1);
    if (from == to)
        FAIL(B_BADREQ, "Cannot transfer to the same account.");
    if (amount <= 0 || amount > MAX_TXN_MONEY)
        FAIL(B_LIMIT, "Amount must be between Rs 0.01 and Rs 1 crore.");
    if (!bank_account_exists(b, to))
        FAIL(B_NOTFOUND, "Beneficiary account %llu does not exist.", (unsigned long long)to);
    op_t op;
    op_begin(&op, b, "transfer");
    /* request order is (from, to): two opposite transfers can deadlock unless
     * the lock manager's policy prevents or resolves it */
    uint64_t accts[2] = {from, to};
    if (!op_lock(&op, accts, 2)) {
        op_end(&op);
        FAIL(B_INTERNAL, "Gave up after repeated deadlocks.");
    }
    uint64_t rf, rt;
    record_t f, t;
    if (!read_account(b, from, &rf, &f) || !read_account(b, to, &rt, &t)) {
        op_end(&op);
        FAIL(B_NOTFOUND, "No such account.");
    }
    acct_body_t *fa = BODY(&f, acct_body_t), *ta = BODY(&t, acct_body_t);
    if (fa->balance < amount) {
        op_end(&op);
        char m[48];
        FAIL(B_FUNDS, "Insufficient funds: balance is %s.", fmt_money(fa->balance, m, sizeof m));
    }
    fa->balance -= amount;
    ta->balance += amount;
    refresh_tier(b, &f);
    refresh_tier(b, &t);
    op_stage(&op, rf, &f);
    op_stage(&op, rt, &t);
    rw_rdlock(&g_audit_gate);
    bool ok = op_commit(&op); /* both updates commit atomically or not at all */
    rw_rdunlock(&g_audit_gate);
    int restarts = op.t.restarts;
    op_end(&op);
    if (!ok)
        FAIL(B_INTERNAL, "Could not persist the transfer.");
    idx_set_tier(b, from, fa->tier);
    idx_set_tier(b, to, ta->tier);
    history(b, from, H_XFER_OUT, -amount, fa->balance, to, ta->name);
    history(b, to, H_XFER_IN, amount, ta->balance, from, fa->name);
    char m1[48], m2[48];
    sb_printf(out, "Transferred %s to %llu (%s).\n", fmt_money(amount, m1, sizeof m1),
              (unsigned long long)to, ta->name);
    sb_printf(out, "  New balance: %s\n", fmt_money(fa->balance, m2, sizeof m2));
    if (restarts)
        sb_printf(out, "  (this transfer was a deadlock victim %d time%s and was retried)\n", restarts,
                  restarts > 1 ? "s" : "");
    return B_OK;
}

bstatus_t bank_statement(bank_t *b, uint64_t acct, int n, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[6], 1);
    if (n <= 0 || n > 100)
        n = 15;
    struct stat st;
    pthread_mutex_lock(&b->hist_m);
    fstat(b->hist_fd, &st);
    pthread_mutex_unlock(&b->hist_m);
    off_t count = st.st_size / (off_t)sizeof(hist_t);
    hist_t chunk[64];
    hist_t *found = malloc(sizeof(hist_t) * (size_t)n);
    int nf = 0;
    for (off_t end = count; end > 0 && nf < n;) {
        off_t start = end > 64 ? end - 64 : 0;
        ssize_t r = pread(b->hist_fd, chunk, (size_t)(end - start) * sizeof(hist_t),
                          start * (off_t)sizeof(hist_t));
        int got = r > 0 ? (int)(r / (ssize_t)sizeof(hist_t)) : 0;
        for (int i = got - 1; i >= 0 && nf < n; i--)
            if (chunk[i].acct == acct)
                found[nf++] = chunk[i];
        end = start;
    }
    sb_printf(out, "Statement for %llu (latest %d entries, newest first)\n", (unsigned long long)acct, nf);
    sb_printf(out, "  %-19s %-5s %-13s %18s %18s  %s\n", "date/time", "month", "type", "amount",
              "balance", "details");
    for (int i = 0; i < nf; i++) {
        char ts[32], m1[48], m2[48];
        time_t tt = (time_t)found[i].ts;
        struct tm tm;
        localtime_r(&tt, &tm);
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
        char ref[64] = "";
        if (found[i].ref)
            snprintf(ref, sizeof ref, "ref %llu ", (unsigned long long)found[i].ref);
        sb_printf(out, "  %-19s %-5d %-13s %18s %18s  %s%s\n", ts, found[i].month,
                  hist_names[found[i].kind], fmt_money(found[i].amount, m1, sizeof m1),
                  fmt_money(found[i].balance, m2, sizeof m2), ref, found[i].memo);
    }
    free(found);
    return B_OK;
}

/* ------------------------------------------------------------------ fixed deposits */

bstatus_t bank_fd_quote(int64_t amount, int months, bool senior, sbuf_t *out)
{
    if (amount <= 0 || amount > FD_MAX_PRINCIPAL)
        FAIL(B_LIMIT, "Invalid deposit amount. Please enter an amount up to Rs 2 crore.");
    if (months < 1 || months > FD_MAX_MONTHS)
        FAIL(B_BADREQ, "Invalid duration. Please enter a duration between 1 and 60 months.");
    double rate = fd_rate(months, senior);
    char m[48], m2[48];
    sb_printf(out, "Expected percentage returns (%s):\n", senior ? "senior citizen" : "regular");
    for (int band = 0; band < 10; band++)
        sb_printf(out, "  %2d - %2d months = %.2f%% p.a.\n", band * 6, band * 6 + 6,
                  fd_rate(band * 6 + (band ? 0 : 1), senior));
    sb_printf(out, "Before maturity (premature withdrawal):\n");
    for (int y = 0; y < 5; y++)
        sb_printf(out, "  %d - %d year = %.2f%% p.a.\n", y, y + 1, fd_premature_rate(y * 12 + (y ? 0 : 1), senior));
    sb_printf(out, "Your FD: %s for %d months at %.2f%% p.a.\n", fmt_money(amount, m, sizeof m), months, rate);
    sb_printf(out, "  Expected maturity amount: %s\n",
              fmt_money(amount + fd_simple_interest(amount, months, rate), m2, sizeof m2));
    return B_OK;
}

bstatus_t bank_fd_open(bank_t *b, uint64_t acct, int64_t amount, int months, bool senior, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[7], 1);
    if (amount <= 0 || amount > FD_MAX_PRINCIPAL)
        FAIL(B_LIMIT, "Invalid deposit amount. Please enter an amount up to Rs 2 crore.");
    if (months < 1 || months > FD_MAX_MONTHS)
        FAIL(B_BADREQ, "Invalid duration. Please enter a duration between 1 and 60 months.");
    op_t op;
    op_begin(&op, b, "fd-open");
    if (!op_lock(&op, &acct, 1)) {
        op_end(&op);
        FAIL(B_INTERNAL, "Could not lock the account.");
    }
    uint64_t rid;
    record_t rec;
    if (!read_account(b, acct, &rid, &rec)) {
        op_end(&op);
        FAIL(B_NOTFOUND, "No such account.");
    }
    acct_body_t *a = BODY(&rec, acct_body_t);
    if (senior && a->age < 60) {
        op_end(&op);
        FAIL(B_DENIED, "Senior citizen rates need an account holder aged 60 or above (you are %d).", a->age);
    }
    if (a->balance < amount) {
        op_end(&op);
        char m[48];
        FAIL(B_FUNDS, "Insufficient funds: balance is %s.", fmt_money(a->balance, m, sizeof m));
    }
    a->balance -= amount;
    refresh_tier(b, &rec);
    record_t fdr;
    memset(&fdr, 0, sizeof fdr);
    fdr.type = REC_FD;
    fdr.key = new_key(b, REC_FD);
    fdr.owner = acct;
    fd_body_t *fd = BODY(&fdr, fd_body_t);
    fd->principal = amount;
    fd->months = months;
    fd->senior = senior;
    fd->rate = fd_rate(months, senior);
    fd->start_month = bank_month(b);
    fd->status = FD_ACTIVE;
    fd->opened = wall_time();
    uint64_t fdrid = db_alloc_rid(&b->db);
    op_stage(&op, rid, &rec);
    op_stage(&op, fdrid, &fdr);
    rw_rdlock(&g_audit_gate);
    bool ok = op_commit(&op);
    if (ok)
        b->ledger -= amount;
    rw_rdunlock(&g_audit_gate);
    if (!ok) {
        db_release_rid(&b->db, fdrid);
        op_end(&op);
        FAIL(B_INTERNAL, "Could not persist the FD.");
    }
    rw_wrlock(&b->idx_lock);
    idx_insert_locked(b, fdr.key, fdrid, acct, REC_FD);
    idx_entry_t *o = idx_find(b, acct);
    if (o) {
        idx_add_child_locked(o, fdr.key);
        o->tier = a->tier;
    }
    rw_wrunlock(&b->idx_lock);
    op_end(&op);
    history(b, acct, H_FD_OPEN, -amount, a->balance, fdr.key, "fixed deposit");
    char m1[48], m2[48];
    sb_printf(out, "Your FD account has been created:\n");
    sb_printf(out, "  New FD account  : %llu\n", (unsigned long long)fdr.key);
    sb_printf(out, "  Principal       : %s\n", fmt_money(amount, m1, sizeof m1));
    sb_printf(out, "  Duration        : %d months (matures in bank month %d)\n", months, fd->start_month + months);
    sb_printf(out, "  Interest rate   : %.2f%% p.a.\n", fd->rate);
    sb_printf(out, "  Expected return after maturity: %s\n",
              fmt_money(amount + fd_simple_interest(amount, months, fd->rate), m2, sizeof m2));
    return B_OK;
}

bstatus_t bank_fd_list(bank_t *b, uint64_t acct, sbuf_t *out)
{
    uint64_t kids[256];
    int n = idx_children(b, acct, kids, 256);
    int month = bank_month(b), shown = 0;
    sb_printf(out, "Fixed deposits (bank month %d):\n", month);
    for (int i = 0; i < n; i++) {
        uint64_t rid = idx_rid(b, kids[i], REC_FD, NULL);
        if (!rid)
            continue;
        record_t r;
        db_read(&b->db, rid, &r);
        fd_body_t *fd = BODY(&r, fd_body_t);
        char m1[48], m2[48], state[80];
        int due = fd->start_month + fd->months;
        if (fd->status == FD_CLOSED)
            snprintf(state, sizeof state, "closed, paid %s", fmt_money(fd->payout, m2, sizeof m2));
        else if (month >= due)
            snprintf(state, sizeof state, "MATURED (close it to collect)");
        else
            snprintf(state, sizeof state, "active, matures in month %d", due);
        sb_printf(out, "  %llu  %s  %2d months @ %.2f%%  %s\n", (unsigned long long)r.key,
                  fmt_money(fd->principal, m1, sizeof m1), fd->months, fd->rate, state);
        shown++;
    }
    if (!shown)
        sb_printf(out, "  (none)\n");
    return B_OK;
}

bstatus_t bank_fd_close(bank_t *b, uint64_t acct, uint64_t fd_no, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[8], 1);
    uint64_t owner = 0;
    uint64_t fdrid = idx_rid(b, fd_no, REC_FD, &owner);
    if (!fdrid || owner != acct)
        FAIL(B_NOTFOUND, "FD %llu not found on this account.", (unsigned long long)fd_no);
    op_t op;
    op_begin(&op, b, "fd-close");
    if (!op_lock(&op, &acct, 1)) {
        op_end(&op);
        FAIL(B_INTERNAL, "Could not lock the account.");
    }
    uint64_t rid;
    record_t rec, fdr;
    read_account(b, acct, &rid, &rec);
    db_read(&b->db, fdrid, &fdr);
    fd_body_t *fd = BODY(&fdr, fd_body_t);
    if (fd->status != FD_ACTIVE) {
        op_end(&op);
        FAIL(B_DENIED, "FD %llu is already closed.", (unsigned long long)fd_no);
    }
    int elapsed = bank_month(b) - fd->start_month;
    bool matured = elapsed >= fd->months;
    int64_t interest;
    double rate;
    if (matured) {
        rate = fd->rate;
        interest = fd_simple_interest(fd->principal, fd->months, rate);
    } else {
        rate = fd_premature_rate(elapsed, fd->senior);
        interest = fd_simple_interest(fd->principal, elapsed, rate);
    }
    int64_t payout = fd->principal + interest;
    fd->status = FD_CLOSED;
    fd->payout = payout;
    acct_body_t *a = BODY(&rec, acct_body_t);
    a->balance += payout;
    refresh_tier(b, &rec);
    op_stage(&op, rid, &rec);
    op_stage(&op, fdrid, &fdr);
    rw_rdlock(&g_audit_gate);
    bool ok = op_commit(&op);
    if (ok)
        b->ledger += payout;
    rw_rdunlock(&g_audit_gate);
    op_end(&op);
    if (!ok)
        FAIL(B_INTERNAL, "Could not persist the FD closure.");
    idx_set_tier(b, acct, a->tier);
    history(b, acct, H_FD_CLOSE, payout, a->balance, fd_no, matured ? "FD matured" : "FD premature");
    char m1[48], m2[48];
    sb_printf(out, "FD %llu closed %s after %d of %d months.\n", (unsigned long long)fd_no,
              matured ? "at maturity" : "BEFORE maturity", elapsed < 0 ? 0 : elapsed, fd->months);
    sb_printf(out, "  Rate applied : %.2f%% p.a.%s\n", rate, matured ? "" : " (premature rate)");
    sb_printf(out, "  Paid out     : %s (interest %s)\n", fmt_money(payout, m1, sizeof m1),
              fmt_money(interest, m2, sizeof m2));
    return B_OK;
}

/* ------------------------------------------------------------------ loans */

bstatus_t bank_loan_quote(int cibil, loan_type_t t, int lakhs, int tenure, int64_t income, sbuf_t *out)
{
    loan_quote_t q = loan_quote(cibil, t, lakhs, tenure, income);
    if (cibil >= 300 && cibil <= 900) {
        sb_printf(out, "CIBIL %d: eligible for", cibil);
        bool any = false;
        for (int k = 0; k < LOAN_TYPES; k++)
            if (loan_eligible(cibil, (loan_type_t)k)) {
                sb_printf(out, " %s", loan_type_name(k));
                any = true;
            }
        sb_printf(out, "%s; maximum %d lakhs.\n", any ? "" : " nothing", loan_max_lakhs(cibil));
    }
    if (!q.ok)
        FAIL(B_DENIED, "We are sorry: %s.", q.reason);
    char m1[48], m2[48];
    sb_printf(out, "%s loan: approved %s for %d months at an annual interest rate of %d%%.\n",
              loan_type_name(t), fmt_money(q.approved, m1, sizeof m1), tenure, q.rate);
    sb_printf(out, "  EMI: %s per month%s\n", fmt_money(q.emi, m2, sizeof m2),
              q.reduced_by_income ? " (amount reduced so the EMI fits your income)" : "");
    return B_OK;
}

bstatus_t bank_loan_apply(bank_t *b, uint64_t acct, int cibil, loan_type_t t, int lakhs, int tenure,
                          int64_t income, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[9], 1);
    loan_quote_t q = loan_quote(cibil, t, lakhs, tenure, income);
    if (!q.ok)
        FAIL(B_DENIED, "We are sorry: %s.", q.reason);
    /* Banker's algorithm: can the bank still honour every credit line? */
    bk_result_t br = bk_raise_max(&b->bk, acct, t, q.approved);
    if (br != BK_OK)
        FAIL(B_DENIED, "Sanction refused: %s.", bk_result_name(br));
    op_t op;
    op_begin(&op, b, "loan-apply");
    if (!op_lock(&op, &acct, 1)) {
        op_end(&op);
        bk_lower_max(&b->bk, acct, t, q.approved);
        FAIL(B_INTERNAL, "Could not lock the account.");
    }
    if (!bank_account_exists(b, acct)) {
        op_end(&op);
        bk_lower_max(&b->bk, acct, t, q.approved);
        FAIL(B_NOTFOUND, "No such account.");
    }
    record_t lr;
    memset(&lr, 0, sizeof lr);
    lr.type = REC_LOAN;
    lr.key = new_key(b, REC_LOAN);
    lr.owner = acct;
    loan_body_t *l = BODY(&lr, loan_body_t);
    l->type = t;
    l->cibil = cibil;
    l->rate = q.rate;
    l->tenure = tenure;
    l->sanctioned = q.approved;
    l->emi = q.emi;
    l->income = income;
    l->status = LOAN_SANCTIONED;
    uint64_t lrid = db_alloc_rid(&b->db);
    op_stage(&op, lrid, &lr);
    if (!op_commit(&op)) {
        db_release_rid(&b->db, lrid);
        op_end(&op);
        bk_lower_max(&b->bk, acct, t, q.approved);
        FAIL(B_INTERNAL, "Could not persist the loan.");
    }
    rw_wrlock(&b->idx_lock);
    idx_insert_locked(b, lr.key, lrid, acct, REC_LOAN);
    idx_entry_t *o = idx_find(b, acct);
    if (o)
        idx_add_child_locked(o, lr.key);
    rw_wrunlock(&b->idx_lock);
    op_end(&op);
    char m1[48], m2[48];
    sb_printf(out, "Loan %llu sanctioned: %s %s loan at %d%% for %d months.\n", (unsigned long long)lr.key,
              fmt_money(q.approved, m1, sizeof m1), loan_type_name(t), q.rate, tenure);
    sb_printf(out, "  EMI: %s per month. Draw funds with LOAN_DRAW; the Banker's algorithm checks each "
                   "disbursement.\n",
              fmt_money(q.emi, m2, sizeof m2));
    return B_OK;
}

bstatus_t bank_loan_list(bank_t *b, uint64_t acct, sbuf_t *out)
{
    uint64_t kids[256];
    int n = idx_children(b, acct, kids, 256), shown = 0;
    sb_printf(out, "Loans:\n");
    for (int i = 0; i < n; i++) {
        uint64_t rid = idx_rid(b, kids[i], REC_LOAN, NULL);
        if (!rid)
            continue;
        record_t r;
        db_read(&b->db, rid, &r);
        loan_body_t *l = BODY(&r, loan_body_t);
        char m1[48], m2[48], m3[48];
        sb_printf(out, "  %llu  %-8s sanctioned %s | drawn %s | repaid %s | %d%% | %s\n",
                  (unsigned long long)r.key, loan_type_name(l->type), fmt_money(l->sanctioned, m1, sizeof m1),
                  fmt_money(l->drawn, m2, sizeof m2), fmt_money(l->repaid, m3, sizeof m3), l->rate,
                  l->status == LOAN_CLOSED ? "closed" : "active");
        shown++;
    }
    if (!shown)
        sb_printf(out, "  (none)\n");
    pthread_mutex_lock(&b->pend_m);
    for (pending_draw_t *p = b->pending; p; p = p->next)
        if (p->acct == acct) {
            char m[48];
            sb_printf(out, "  pending disbursement #%llu: %s on loan %llu (waiting for a safe state)\n",
                      (unsigned long long)p->id, fmt_money(p->amount, m, sizeof m), (unsigned long long)p->loan);
        }
    pthread_mutex_unlock(&b->pend_m);
    return B_OK;
}

/* applies an already-granted disbursement */
static bstatus_t apply_draw(bank_t *b, uint64_t acct, uint64_t loan_no, int64_t amount, sbuf_t *out)
{
    uint64_t lrid = idx_rid(b, loan_no, REC_LOAN, NULL);
    op_t op;
    op_begin(&op, b, "loan-draw");
    if (!op_lock(&op, &acct, 1)) {
        op_end(&op);
        FAIL(B_INTERNAL, "Could not lock the account.");
    }
    uint64_t rid;
    record_t rec, lr;
    read_account(b, acct, &rid, &rec);
    db_read(&b->db, lrid, &lr);
    loan_body_t *l = BODY(&lr, loan_body_t);
    acct_body_t *a = BODY(&rec, acct_body_t);
    l->drawn += amount;
    a->balance += amount;
    refresh_tier(b, &rec);
    op_stage(&op, rid, &rec);
    op_stage(&op, lrid, &lr);
    rw_rdlock(&g_audit_gate);
    bool ok = op_commit(&op);
    if (ok)
        b->ledger += amount;
    rw_rdunlock(&g_audit_gate);
    op_end(&op);
    if (!ok)
        FAIL(B_INTERNAL, "Could not persist the disbursement.");
    idx_set_tier(b, acct, a->tier);
    history(b, acct, H_LOAN_DRAW, amount, a->balance, loan_no, loan_type_name(l->type));
    char m1[48], m2[48];
    sb_printf(out, "Disbursed %s from loan %llu.\n", fmt_money(amount, m1, sizeof m1), (unsigned long long)loan_no);
    sb_printf(out, "  New balance: %s\n", fmt_money(a->balance, m2, sizeof m2));
    return B_OK;
}

static void process_pending(bank_t *b)
{
    for (;;) {
        pending_draw_t *grant = NULL;
        pthread_mutex_lock(&b->pend_m);
        pending_draw_t **pp = &b->pending;
        for (; *pp; pp = &(*pp)->next) {
            uint64_t owner = 0;
            uint64_t lrid = idx_rid(b, (*pp)->loan, REC_LOAN, &owner);
            if (!lrid)
                continue;
            record_t lr;
            db_read(&b->db, lrid, &lr);
            if (bk_request(&b->bk, (*pp)->acct, BODY(&lr, loan_body_t)->type, (*pp)->amount) == BK_OK) {
                grant = *pp;
                *pp = grant->next;
                break;
            }
        }
        pthread_mutex_unlock(&b->pend_m);
        if (!grant)
            return;
        sbuf_t tmp;
        sb_init(&tmp);
        apply_draw(b, grant->acct, grant->loan, grant->amount, &tmp);
        LOGI("banker: deferred disbursement #%llu granted (%lld paise to %llu)",
             (unsigned long long)grant->id, (long long)grant->amount, (unsigned long long)grant->acct);
        sb_free(&tmp);
        pfree(grant);
    }
}

bstatus_t bank_loan_draw(bank_t *b, uint64_t acct, uint64_t loan_no, int64_t amount, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[10], 1);
    uint64_t owner = 0;
    uint64_t lrid = idx_rid(b, loan_no, REC_LOAN, &owner);
    if (!lrid || owner != acct)
        FAIL(B_NOTFOUND, "Loan %llu not found on this account.", (unsigned long long)loan_no);
    if (amount <= 0)
        FAIL(B_BADREQ, "Amount must be positive.");
    record_t lr;
    db_read(&b->db, lrid, &lr);
    loan_body_t *l = BODY(&lr, loan_body_t);
    if (l->status != LOAN_SANCTIONED)
        FAIL(B_DENIED, "Loan %llu is closed.", (unsigned long long)loan_no);
    bk_result_t br = bk_request(&b->bk, acct, l->type, amount);
    if (br == BK_EXCEEDS_NEED) {
        char m[48];
        FAIL(B_LIMIT, "Exceeds the undrawn sanction of %s.", fmt_money(l->sanctioned - l->drawn, m, sizeof m));
    }
    if (br == BK_WAIT || br == BK_UNSAFE) {
        pending_draw_t *p = pcalloc(sizeof *p);
        if (!p)
            FAIL(B_INTERNAL, "Out of memory.");
        pthread_mutex_lock(&b->pend_m);
        p->id = ++b->pending_seq;
        p->acct = acct;
        p->loan = loan_no;
        p->amount = amount;
        p->queued_at = wall_time();
        pending_draw_t **tail = &b->pending;
        while (*tail)
            tail = &(*tail)->next;
        *tail = p; /* FIFO */
        pthread_mutex_unlock(&b->pend_m);
        sb_printf(out, "Disbursement deferred (#%llu): %s.\n", (unsigned long long)p->id, bk_result_name(br));
        sb_printf(out, "  It will be paid automatically once repayments restore a safe state.\n");
        return B_PENDING;
    }
    return apply_draw(b, acct, loan_no, amount, out);
}

bstatus_t bank_loan_repay(bank_t *b, uint64_t acct, uint64_t loan_no, int64_t amount, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[11], 1);
    uint64_t owner = 0;
    uint64_t lrid = idx_rid(b, loan_no, REC_LOAN, &owner);
    if (!lrid || owner != acct)
        FAIL(B_NOTFOUND, "Loan %llu not found on this account.", (unsigned long long)loan_no);
    if (amount <= 0)
        FAIL(B_BADREQ, "Amount must be positive.");
    op_t op;
    op_begin(&op, b, "loan-repay");
    if (!op_lock(&op, &acct, 1)) {
        op_end(&op);
        FAIL(B_INTERNAL, "Could not lock the account.");
    }
    uint64_t rid;
    record_t rec, lr;
    read_account(b, acct, &rid, &rec);
    db_read(&b->db, lrid, &lr);
    loan_body_t *l = BODY(&lr, loan_body_t);
    acct_body_t *a = BODY(&rec, acct_body_t);
    int64_t outstanding = l->drawn - l->repaid;
    char m[48];
    if (amount > outstanding) {
        op_end(&op);
        FAIL(B_LIMIT, "Outstanding principal is only %s.", fmt_money(outstanding, m, sizeof m));
    }
    if (a->balance < amount) {
        op_end(&op);
        FAIL(B_FUNDS, "Insufficient funds: balance is %s.", fmt_money(a->balance, m, sizeof m));
    }
    l->repaid += amount;
    if (l->repaid >= l->sanctioned)
        l->status = LOAN_CLOSED;
    a->balance -= amount;
    refresh_tier(b, &rec);
    op_stage(&op, rid, &rec);
    op_stage(&op, lrid, &lr);
    rw_rdlock(&g_audit_gate);
    bool ok = op_commit(&op);
    if (ok)
        b->ledger -= amount;
    rw_rdunlock(&g_audit_gate);
    op_end(&op);
    if (!ok)
        FAIL(B_INTERNAL, "Could not persist the repayment.");
    bk_repay(&b->bk, acct, l->type, amount);
    idx_set_tier(b, acct, a->tier);
    history(b, acct, H_LOAN_REPAY, -amount, a->balance, loan_no, loan_type_name(l->type));
    char m1[48], m2[48];
    sb_printf(out, "Repaid %s on loan %llu%s.\n", fmt_money(amount, m1, sizeof m1), (unsigned long long)loan_no,
              l->status == LOAN_CLOSED ? " - loan fully repaid and closed" : "");
    sb_printf(out, "  Outstanding: %s\n", fmt_money(l->drawn - l->repaid, m2, sizeof m2));
    process_pending(b); /* released funds may unblock deferred disbursements */
    return B_OK;
}

/* ------------------------------------------------------------------ insurance */

static ins_quote_t ins_quote_any(ins_kind_t k, int a1, int64_t a2, int a3)
{
    switch (k) {
    case INS_LIFE: return ins_quote_life(a1);
    case INS_HEALTH: return ins_quote_health(a1, a3 != 0, (int)a2);
    case INS_CAR: return ins_quote_car(a1, a2, a3);
    default: return (ins_quote_t){.reason = "unknown insurance type"};
    }
}

bstatus_t bank_ins_quote(ins_kind_t k, int a1, int64_t a2, int a3, sbuf_t *out)
{
    ins_quote_t q = ins_quote_any(k, a1, a2, a3);
    if (!q.ok)
        FAIL(B_DENIED, "Sorry: %s.", q.reason);
    char m1[48], m2[48];
    sb_printf(out, "%s insurance: coverage %s, premium %s per month.\n", ins_kind_name(k),
              fmt_money(q.cover, m1, sizeof m1), fmt_money(q.monthly_premium, m2, sizeof m2));
    return B_OK;
}

bstatus_t bank_ins_buy(bank_t *b, uint64_t acct, ins_kind_t k, const char *holder,
                       const char *beneficiary, int a1, int64_t a2, int a3, sbuf_t *out)
{
    atomic_fetch_add(&b->ops[12], 1);
    ins_quote_t q = ins_quote_any(k, a1, a2, a3);
    if (!q.ok)
        FAIL(B_DENIED, "Sorry: %s.", q.reason);
    if (!valid_person_name(holder))
        FAIL(B_BADREQ, "Enter a valid name for the policy holder.");
    if (k != INS_CAR && !valid_person_name(beneficiary))
        FAIL(B_BADREQ, "Enter a valid name for the beneficiary.");
    op_t op;
    op_begin(&op, b, "insurance");
    if (!op_lock(&op, &acct, 1)) {
        op_end(&op);
        FAIL(B_INTERNAL, "Could not lock the account.");
    }
    uint64_t rid;
    record_t rec;
    if (!read_account(b, acct, &rid, &rec)) {
        op_end(&op);
        FAIL(B_NOTFOUND, "No such account.");
    }
    acct_body_t *a = BODY(&rec, acct_body_t);
    if (a->balance < q.monthly_premium) {
        op_end(&op);
        FAIL(B_FUNDS, "Insufficient funds for the first premium.");
    }
    a->balance -= q.monthly_premium;
    refresh_tier(b, &rec);
    record_t pr;
    memset(&pr, 0, sizeof pr);
    pr.type = REC_POLICY;
    pr.key = new_key(b, REC_POLICY);
    pr.owner = acct;
    policy_body_t *p = BODY(&pr, policy_body_t);
    p->kind = k;
    p->status = POL_ACTIVE;
    snprintf(p->holder, sizeof p->holder, "%s", holder);
    snprintf(p->beneficiary, sizeof p->beneficiary, "%s", k == INS_CAR ? "-" : beneficiary);
    p->cover = q.cover;
    p->premium = q.monthly_premium;
    p->start_month = bank_month(b);
    p->paid_months = 1;
    p->detail1 = a1;
    p->detail2 = a2;
    uint64_t prid = db_alloc_rid(&b->db);
    op_stage(&op, rid, &rec);
    op_stage(&op, prid, &pr);
    rw_rdlock(&g_audit_gate);
    bool ok = op_commit(&op);
    if (ok)
        b->ledger -= q.monthly_premium;
    rw_rdunlock(&g_audit_gate);
    if (!ok) {
        db_release_rid(&b->db, prid);
        op_end(&op);
        FAIL(B_INTERNAL, "Could not persist the policy.");
    }
    rw_wrlock(&b->idx_lock);
    idx_insert_locked(b, pr.key, prid, acct, REC_POLICY);
    idx_entry_t *o = idx_find(b, acct);
    if (o) {
        idx_add_child_locked(o, pr.key);
        o->tier = a->tier;
    }
    rw_wrunlock(&b->idx_lock);
    op_end(&op);
    history(b, acct, H_PREMIUM, -q.monthly_premium, a->balance, pr.key, "first premium");
    char m1[48], m2[48];
    sb_printf(out, "The details of your insurance are as follows:\n");
    sb_printf(out, "  Policy number  : %llu\n", (unsigned long long)pr.key);
    sb_printf(out, "  Type           : %s\n", ins_kind_name(k));
    sb_printf(out, "  Policy holder  : %s\n", p->holder);
    if (k != INS_CAR)
        sb_printf(out, "  Beneficiary    : %s\n", p->beneficiary);
    sb_printf(out, "  Coverage       : %s\n", fmt_money(q.cover, m1, sizeof m1));
    sb_printf(out, "  Premium        : %s per month (auto-debited at each month end)\n",
              fmt_money(q.monthly_premium, m2, sizeof m2));
    sb_printf(out, "Thank you for taking the insurance.\n");
    return B_OK;
}

bstatus_t bank_ins_list(bank_t *b, uint64_t acct, sbuf_t *out)
{
    uint64_t kids[256];
    int n = idx_children(b, acct, kids, 256), shown = 0;
    sb_printf(out, "Insurance policies:\n");
    for (int i = 0; i < n; i++) {
        uint64_t rid = idx_rid(b, kids[i], REC_POLICY, NULL);
        if (!rid)
            continue;
        record_t r;
        db_read(&b->db, rid, &r);
        policy_body_t *p = BODY(&r, policy_body_t);
        char m1[48], m2[48];
        sb_printf(out, "  %llu  %-6s cover %s  premium %s/month  paid %d months  %s\n",
                  (unsigned long long)r.key, ins_kind_name(p->kind), fmt_money(p->cover, m1, sizeof m1),
                  fmt_money(p->premium, m2, sizeof m2), p->paid_months,
                  p->status == POL_ACTIVE ? "active" : "LAPSED");
        shown++;
    }
    if (!shown)
        sb_printf(out, "  (none)\n");
    return B_OK;
}

/* ------------------------------------------------------------------ batch + audit */

static uint64_t *collect_accounts(bank_t *b, uint64_t *n)
{
    rw_rdlock(&b->idx_lock);
    uint64_t cap = b->naccounts + 1, k = 0;
    uint64_t *keys = malloc(sizeof(uint64_t) * cap);
    for (int h = 0; h < IDX_BUCKETS; h++)
        for (idx_entry_t *e = b->idx[h]; e; e = e->next)
            if (e->type == REC_ACCOUNT && k < cap)
                keys[k++] = e->key;
    rw_rdunlock(&b->idx_lock);
    *n = k;
    return keys;
}

bstatus_t bank_month_end(bank_t *b, sbuf_t *out)
{
    uint64_t n;
    uint64_t *keys = collect_accounts(b, &n);
    int64_t total_interest = 0, total_premium = 0;
    int lapsed = 0;
    for (uint64_t i = 0; i < n; i++) {
        op_t op;
        op_begin(&op, b, "month-end");
        if (!op_lock(&op, &keys[i], 1)) {
            op_end(&op);
            continue;
        }
        uint64_t rid;
        record_t rec;
        if (!read_account(b, keys[i], &rid, &rec)) {
            op_end(&op);
            continue;
        }
        acct_body_t *a = BODY(&rec, acct_body_t);
        int64_t interest = a->balance * SAVINGS_RATE_BP / 10000 / 12;
        a->balance += interest;
        int64_t premiums = 0;
        uint64_t kids[14];
        int nk = idx_children(b, keys[i], kids, 14);
        for (int c = 0; c < nk; c++) {
            uint64_t prid = idx_rid(b, kids[c], REC_POLICY, NULL);
            if (!prid)
                continue;
            record_t pr;
            db_read(&b->db, prid, &pr);
            policy_body_t *p = BODY(&pr, policy_body_t);
            if (p->status != POL_ACTIVE)
                continue;
            if (a->balance >= p->premium) {
                a->balance -= p->premium;
                premiums += p->premium;
                p->paid_months++;
            } else {
                p->status = POL_LAPSED;
                lapsed++;
            }
            op_stage(&op, prid, &pr);
        }
        refresh_tier(b, &rec);
        op_stage(&op, rid, &rec);
        rw_rdlock(&g_audit_gate);
        bool ok = op_commit(&op);
        if (ok)
            b->ledger += interest - premiums;
        rw_rdunlock(&g_audit_gate);
        op_end(&op);
        if (!ok)
            continue;
        idx_set_tier(b, keys[i], a->tier);
        if (interest)
            history(b, keys[i], H_INTEREST, interest, a->balance + premiums, 0, "savings interest");
        if (premiums)
            history(b, keys[i], H_PREMIUM, -premiums, a->balance, 0, "monthly premiums");
        total_interest += interest;
        total_premium += premiums;
    }
    free(keys);
    char m1[48], m2[48];
    sb_printf(out, "Month-end batch for %llu accounts: interest credited %s, premiums collected %s, %d "
                   "polic%s lapsed.\n",
              (unsigned long long)n, fmt_money(total_interest, m1, sizeof m1),
              fmt_money(total_premium, m2, sizeof m2), lapsed, lapsed == 1 ? "y" : "ies");
    return B_OK;
}

bool bank_audit(bank_t *b, int64_t *actual, int64_t *expected, uint64_t *accounts)
{
    uint64_t n;
    uint64_t *keys = collect_accounts(b, &n);
    uint64_t *rids = malloc(sizeof(uint64_t) * (n + 1));
    for (uint64_t i = 0; i < n; i++)
        rids[i] = idx_rid(b, keys[i], REC_ACCOUNT, NULL);
    int64_t sum = 0;
    rw_wrlock(&g_audit_gate); /* quiesce money movement for a consistent snapshot */
    for (uint64_t i = 0; i < n; i++) {
        record_t r;
        db_read(&b->db, rids[i], &r);
        sum += BODY(&r, acct_body_t)->balance;
    }
    int64_t exp = b->ledger;
    rw_wrunlock(&g_audit_gate);
    free(keys);
    free(rids);
    *actual = sum;
    *expected = exp;
    if (accounts)
        *accounts = n;
    return sum == exp;
}

int64_t bank_reconcile(bank_t *b)
{
    int64_t actual, expected;
    bank_audit(b, &actual, &expected, NULL);
    rw_wrlock(&g_audit_gate);
    int64_t diff = actual - (int64_t)b->ledger;
    b->ledger = actual; /* write the difference off */
    rw_wrunlock(&g_audit_gate);
    if (diff)
        LOGW("reconcile: ledger adjusted by %lld paise to match the balances", (long long)diff);
    return diff;
}

void bank_list_accounts(bank_t *b, int limit, sbuf_t *out)
{
    uint64_t n;
    uint64_t *keys = collect_accounts(b, &n);
    sb_printf(out, "%llu accounts\n", (unsigned long long)n);
    for (uint64_t i = 0; i < n && (int)i < limit; i++) {
        uint64_t rid = idx_rid(b, keys[i], REC_ACCOUNT, NULL);
        record_t r;
        db_read(&b->db, rid, &r);
        acct_body_t *a = BODY(&r, acct_body_t);
        char m[48];
        sb_printf(out, "  %llu  %-24s %-12s %-8s %18s\n", (unsigned long long)keys[i], a->name, a->city,
                  tier_name(a->tier), fmt_money(a->balance, m, sizeof m));
    }
    free(keys);
}

void bank_render_pending(bank_t *b, sbuf_t *out)
{
    pthread_mutex_lock(&b->pend_m);
    int n = 0;
    for (pending_draw_t *p = b->pending; p; p = p->next, n++) {
        char m[48];
        sb_printf(out, "  #%llu  account %llu  loan %llu  %s  waiting %llds\n", (unsigned long long)p->id,
                  (unsigned long long)p->acct, (unsigned long long)p->loan, fmt_money(p->amount, m, sizeof m),
                  (long long)(wall_time() - p->queued_at));
    }
    if (!n)
        sb_printf(out, "  (no deferred disbursements)\n");
    pthread_mutex_unlock(&b->pend_m);
}
