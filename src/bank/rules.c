#include "rules.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ accounts */

int tier_for_balance(int64_t balance)
{
    if (balance >= 10 * LAKH)
        return TIER_VIP;
    if (balance >= 1 * LAKH)
        return TIER_PREMIUM;
    return TIER_REGULAR;
}

const char *tier_name(int tier)
{
    switch (tier) {
    case TIER_VIP: return "VIP";
    case TIER_PREMIUM: return "Premium";
    case TIER_REGULAR: return "Regular";
    default: return "Batch";
    }
}

bool valid_phone(int64_t phone) { return phone >= 6000000000LL && phone <= 9999999999LL; }

bool valid_password(const char *pw)
{
    size_t n = strlen(pw);
    return n >= 6 && n <= 12;
}

bool valid_age(int age) { return age >= 18 && age <= 130; }

/* ------------------------------------------------------------------ FDs */

double fd_rate(int months, bool senior)
{
    static const double senior_r[] = {6.00, 6.50, 7.00, 7.50, 7.75, 8.00, 8.25, 8.50, 8.75, 9.00};
    static const double normal_r[] = {5.50, 6.00, 6.50, 7.00, 7.25, 7.50, 7.75, 8.00, 8.25, 8.50};
    if (months <= 0)
        return 0.0;
    int band = months / 6; /* 0-5, 6-11, ... 54+ */
    if (band > 9)
        band = 9;
    return senior ? senior_r[band] : normal_r[band];
}

double fd_premature_rate(int months, bool senior)
{
    static const double senior_r[] = {5.50, 6.00, 6.50, 7.00, 7.50};
    static const double normal_r[] = {5.00, 5.50, 6.00, 6.50, 7.00};
    if (months <= 0)
        return 0.0;
    int band = months / 12;
    if (band > 4)
        band = 4;
    return senior ? senior_r[band] : normal_r[band];
}

int64_t fd_simple_interest(int64_t principal, int months, double rate_pa)
{
    /* maturityAmount = amount + amount * duration * interestRate / 1200 */
    return (int64_t)llround((double)principal * months * rate_pa / 1200.0);
}

/* ------------------------------------------------------------------ loans */

const char *loan_type_name(int t)
{
    static const char *n[] = {"home", "personal", "student", "business"};
    return t >= 0 && t < LOAN_TYPES ? n[t] : "?";
}

bool loan_type_parse(const char *s, loan_type_t *out)
{
    for (int i = 0; i < LOAN_TYPES; i++)
        if (!strcmp(s, loan_type_name(i))) {
            *out = (loan_type_t)i;
            return true;
        }
    return false;
}

int loan_max_lakhs(int cibil)
{
    if (cibil >= 550 && cibil < 580)
        return 20;
    if (cibil >= 580 && cibil < 620)
        return 45;
    if (cibil >= 620 && cibil < 650)
        return 70;
    if (cibil >= 650 && cibil < 700)
        return 100;
    if (cibil >= 700 && cibil <= 900)
        return 250;
    return 0;
}

bool loan_eligible(int cibil, loan_type_t t)
{
    if (cibil < 550 || cibil > 900)
        return false;
    switch (t) {
    case LOAN_HOME: return true;
    case LOAN_PERSONAL: return cibil >= 620;
    case LOAN_STUDENT: return cibil >= 670;
    case LOAN_BUSINESS: return cibil >= 700;
    default: return false;
    }
}

int loan_rate(int cibil, loan_type_t t)
{
    switch (t) {
    case LOAN_HOME:
        if (cibil < 620) {
            int r = 15 - (cibil - 550) / 10; /* 550-559:15 ... 610-619:9 */
            return r < 9 ? 9 : r;
        }
        return 8;
    case LOAN_STUDENT:
        if (cibil < 680) return 12;
        if (cibil < 690) return 10;
        if (cibil < 700) return 9;
        if (cibil < 710) return 8;
        return 7;
    case LOAN_PERSONAL:
        if (cibil < 630) return 18;
        if (cibil < 640) return 16;
        if (cibil < 650) return 15;
        if (cibil < 670) return 13;
        if (cibil < 690) return 12;
        if (cibil < 700) return 11;
        return 10;
    case LOAN_BUSINESS:
        if (cibil < 720) return 24;
        if (cibil < 730) return 20;
        if (cibil < 740) return 18;
        if (cibil < 760) return 15;
        if (cibil < 770) return 14;
        return 13;
    default:
        return 0;
    }
}

loan_quote_t loan_quote(int cibil, loan_type_t t, int lakhs, int tenure, int64_t income_paise)
{
    loan_quote_t q = {0};
    if (cibil < 300 || cibil > 900) {
        q.reason = "CIBIL score must be between 300 and 900";
        return q;
    }
    if (!loan_eligible(cibil, t)) {
        q.reason = cibil < 550 ? "CIBIL score below 550: not eligible for any loan"
                               : "CIBIL score too low for this loan type";
        return q;
    }
    q.max_lakhs = loan_max_lakhs(cibil);
    if (lakhs <= 0 || lakhs > q.max_lakhs) {
        q.reason = "amount exceeds the maximum for this CIBIL score";
        return q;
    }
    if (tenure < 24 || tenure > 300) {
        q.reason = "tenure must be between 24 and 300 months";
        return q;
    }
    double income = (double)income_paise / 100.0;
    if (income < 20000) {
        q.reason = "monthly income below Rs 20,000";
        return q;
    }
    q.rate = loan_rate(cibil, t);
    /* the original formula, kept as-is: a = (1 + r)^(tenure / 12), EMI = P * a / tenure */
    double a = pow(1.0 + q.rate / 100.0, (double)(tenure / 12));
    double principal = (double)lakhs * 100000.0;
    double emi = principal * a / tenure;
    double approved = principal;
    double cap = income >= 100000 ? income / 2 : income >= 60000 ? income * 2 / 5 : income * 3 / 10;
    if (emi > cap) {
        emi = cap;
        approved = emi * tenure / a;
        q.reduced_by_income = true;
    }
    q.ok = true;
    q.requested = (int64_t)lakhs * LAKH;
    q.approved = (int64_t)llround(approved * 100.0);
    q.emi = (int64_t)llround(emi * 100.0);
    return q;
}

/* ------------------------------------------------------------------ insurance */

const char *ins_kind_name(int k)
{
    static const char *n[] = {"life", "health", "car"};
    return k >= 0 && k < INS_KINDS ? n[k] : "?";
}

bool ins_kind_parse(const char *s, ins_kind_t *out)
{
    for (int i = 0; i < INS_KINDS; i++)
        if (!strcmp(s, ins_kind_name(i))) {
            *out = (ins_kind_t)i;
            return true;
        }
    return false;
}

ins_quote_t ins_quote_life(int choice)
{
    static const int64_t cover[] = {50 * LAKH, 1 * CRORE, 2 * CRORE};
    static const int premium[] = {340, 580, 920};
    ins_quote_t q = {0};
    if (choice < 1 || choice > 3) {
        q.reason = "life cover choice must be 1 (50 lakh), 2 (1 crore) or 3 (2 crore)";
        return q;
    }
    q.ok = true;
    q.cover = cover[choice - 1];
    q.monthly_premium = premium[choice - 1] * PAISE_PER_RUPEE;
    return q;
}

ins_quote_t ins_quote_health(int age, bool smoker, int choice)
{
    static const int64_t cover[] = {2 * LAKH, 5 * LAKH, 10 * LAKH, 20 * LAKH};
    /* [cover][over 60][smoker] from Insurance.c */
    static const int premium[4][2][2] = {
        {{470, 580}, {540, 680}},
        {{570, 680}, {740, 880}},
        {{570, 780}, {740, 980}},
        {{770, 980}, {940, 1180}},
    };
    ins_quote_t q = {0};
    if (age < 18) {
        q.reason = "you must be 18 or older to apply for insurance";
        return q;
    }
    if (choice < 1 || choice > 4) {
        q.reason = "health cover choice must be 1 (2L), 2 (5L), 3 (10L) or 4 (20L)";
        return q;
    }
    q.ok = true;
    q.cover = cover[choice - 1];
    q.monthly_premium = premium[choice - 1][age > 60][smoker ? 1 : 0] * PAISE_PER_RUPEE;
    return q;
}

ins_quote_t ins_quote_car(int owner_age, int64_t car_value, int car_age)
{
    ins_quote_t q = {0};
    if (owner_age < 18 || owner_age > 80) {
        q.reason = "car owner must be between 18 and 80 years old";
        return q;
    }
    if (car_value <= 0) {
        q.reason = "car value must be positive";
        return q;
    }
    double pct;
    if (car_age > 0 && car_age <= 5)
        pct = 2.3625;
    else if (car_age > 5 && car_age <= 15)
        pct = 3.3625;
    else {
        q.reason = "cars older than 15 years cannot be insured";
        return q;
    }
    q.ok = true;
    q.cover = car_value;
    /* the original computes an annual premium; it is billed monthly here */
    q.monthly_premium = (int64_t)llround((double)car_value * pct / 100.0 / 12.0);
    return q;
}

/* ------------------------------------------------------------------ Luhn */

int luhn_digit(uint64_t n)
{
    int sum = 0;
    bool dbl = true;
    for (; n; n /= 10, dbl = !dbl) {
        int d = (int)(n % 10);
        if (dbl) {
            d *= 2;
            if (d > 9)
                d -= 9;
        }
        sum += d;
    }
    return (10 - sum % 10) % 10;
}

bool luhn_valid(uint64_t full) { return luhn_digit(full / 10) == (int)(full % 10); }
