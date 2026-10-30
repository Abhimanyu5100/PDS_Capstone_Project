/* rules.h - the original capstone's business rules, as pure functions.
 *
 * Ported from legacy/FD.c, legacy/Loan.c and legacy/Insurance.c with the same
 * rate tables and formulas, so the OS edition gives the same quotes as the
 * original program. All money is in paise (int64) to avoid float drift. */
#ifndef PLUTUS_RULES_H
#define PLUTUS_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define PAISE_PER_RUPEE 100LL
#define LAKH (100000LL * PAISE_PER_RUPEE)
#define CRORE (100LL * LAKH)

/* ---- accounts ---- */
enum { TIER_VIP = 0, TIER_PREMIUM = 1, TIER_REGULAR = 2, PRIO_BATCH = 3 };
int tier_for_balance(int64_t balance);
const char *tier_name(int tier);
bool valid_phone(int64_t phone);
bool valid_password(const char *pw);
bool valid_age(int age);
#define SAVINGS_RATE_BP 300 /* 3.00% p.a., credited monthly */

/* ---- fixed deposits (FD.c) ---- */
#define FD_MAX_PRINCIPAL (2 * CRORE)
#define FD_MAX_MONTHS 60
double fd_rate(int months, bool senior);           /* CalculateInterestRate */
double fd_premature_rate(int months, bool senior); /* CalculatePreMaturityAmount */
int64_t fd_simple_interest(int64_t principal, int months, double rate_pa);

/* ---- loans (Loan.c) ---- */
typedef enum { LOAN_HOME, LOAN_PERSONAL, LOAN_STUDENT, LOAN_BUSINESS, LOAN_TYPES } loan_type_t;
const char *loan_type_name(int t);
bool loan_type_parse(const char *s, loan_type_t *out);
int loan_max_lakhs(int cibil); /* loan_amount_tracker */
bool loan_eligible(int cibil, loan_type_t t);
int loan_rate(int cibil, loan_type_t t);

typedef struct {
    bool ok;
    const char *reason;
    int rate;               /* % p.a. */
    int max_lakhs;
    int64_t requested;      /* paise */
    int64_t approved;       /* paise */
    int64_t emi;            /* paise per month */
    bool reduced_by_income; /* approved < requested because of the EMI cap */
} loan_quote_t;
loan_quote_t loan_quote(int cibil, loan_type_t t, int lakhs, int tenure_months, int64_t monthly_income);

/* ---- insurance (Insurance.c) ---- */
typedef enum { INS_LIFE, INS_HEALTH, INS_CAR, INS_KINDS } ins_kind_t;
const char *ins_kind_name(int k);
bool ins_kind_parse(const char *s, ins_kind_t *out);
typedef struct {
    bool ok;
    const char *reason;
    int64_t cover;           /* paise */
    int64_t monthly_premium; /* paise */
} ins_quote_t;
ins_quote_t ins_quote_life(int cover_choice);                       /* 1..3 */
ins_quote_t ins_quote_health(int age, bool smoker, int cover_choice); /* 1..4 */
ins_quote_t ins_quote_car(int owner_age, int64_t car_value, int car_age_years);

/* Luhn check digit, used to make account numbers self-validating */
int luhn_digit(uint64_t n);
bool luhn_valid(uint64_t n_with_digit);

#endif
