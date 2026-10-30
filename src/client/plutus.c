/* plutus - customer terminal for Plutus Fincorp.
 *
 * Interactive (no arguments): the menus of the original capstone, now talking
 * to the plutusd server over a Unix socket.
 * Scripted:  plutus [-D dir] [-u ACCOUNT -p PASSWORD] COMMAND [ARGS...]
 *            e.g. plutus -u 1000000018 -p secret1 TRANSFER 1000000026 500 */
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/proto.h"
#include "../common/util.h"

#define C_TITLE "\x1b[1;33m"
#define C_MENU "\x1b[1;36m"
#define C_ASK "\x1b[1;34m"
#define C_OK "\x1b[1;32m"
#define C_ERR "\x1b[1;31m"
#define C_DIM "\x1b[2m"
#define C_RESET "\x1b[0m"

static conn_t g_conn;
static sbuf_t g_body;
static char g_status[512];
static uint64_t g_acct;
static char g_name[64];

/* ------------------------------------------------------------------ transport */

static int call(int n, const char *const f[])
{
    int rc = proto_call(&g_conn, &g_body, g_status, sizeof g_status, n, f);
    if (rc < 0) {
        fprintf(stderr, C_ERR "\nLost connection to the bank server.\n" C_RESET);
        exit(3);
    }
    return rc;
}

static void show(int rc)
{
    if (g_body.len)
        printf("%s%s%s", rc == 1 ? C_OK : C_ERR, g_body.buf, C_RESET);
    if (!strncmp(g_status, "ERR\tBUSY", 8))
        printf(C_ERR "(the bank is busy - every teller is occupied)\n" C_RESET);
}

static void connect_or_die(const char *sock)
{
    int fd = unix_connect(sock);
    if (fd < 0) {
        fprintf(stderr, "plutus: cannot reach the bank at %s: %s\n(is plutusd running?)\n", sock,
                strerror(errno));
        exit(2);
    }
    conn_init(&g_conn, fd);
    sb_init(&g_body);
}

/* ------------------------------------------------------------------ input helpers */

static bool read_line(const char *prompt, char *buf, size_t n)
{
    printf(C_ASK "%s" C_RESET, prompt);
    fflush(stdout);
    if (!fgets(buf, (int)n, stdin)) {
        printf("\n");
        exit(0);
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    for (char *p = buf; *p; p++)
        if (*p == '\t')
            *p = ' '; /* tabs are the protocol separator */
    return buf[0] != '\0';
}

static int read_int(const char *prompt, int lo, int hi)
{
    char buf[64];
    for (;;) {
        int v;
        if (read_line(prompt, buf, sizeof buf) && parse_int(buf, &v) && v >= lo && v <= hi)
            return v;
        printf(C_ERR "Invalid input! Enter a number between %d and %d.\n" C_RESET, lo, hi);
    }
}

static void read_money(const char *prompt, char *out, size_t n)
{
    for (;;) {
        int64_t p;
        if (read_line(prompt, out, n) && parse_money(out, &p) && p > 0)
            return;
        printf(C_ERR "Invalid amount! Use digits, e.g. 2500 or 2500.50\n" C_RESET);
    }
}

static void read_text(const char *prompt, char *out, size_t n)
{
    while (!read_line(prompt, out, n))
        printf(C_ERR "This field is required.\n" C_RESET);
}

static void loading(const char *what)
{
    printf(C_DIM "%s", what);
    for (int i = 0; i < 3; i++) {
        printf(".");
        fflush(stdout);
        usleep(150000);
    }
    printf(C_RESET "\n");
}

static void banner(const char *title)
{
    printf("\n" C_TITLE "  ==== %s ====" C_RESET "\n", title);
}

/* ------------------------------------------------------------------ screens */

static void do_register(void)
{
    char name[64], city[64], age[16], phone[32], pw[64], dep[32];
    banner("OPEN A NEW ACCOUNT");
    for (;;) {
        read_text("Please enter your name: ", name, sizeof name);
        if (valid_person_name(name))
            break;
        printf(C_ERR "Names may contain letters and spaces only.\n" C_RESET);
    }
    read_text("Please enter your city: ", city, sizeof city);
    snprintf(age, sizeof age, "%d", read_int("Please enter your age in years: ", 1, 130));
    for (;;) {
        read_text("Enter your phone number: ", phone, sizeof phone);
        int64_t p;
        if (parse_i64(phone, &p) && p >= 6000000000LL && p <= 9999999999LL)
            break;
        printf(C_ERR "Invalid mobile number! Enter 10 digits starting with 6-9.\n" C_RESET);
    }
    for (;;) {
        read_text("Input a password (between 6 - 12 characters): ", pw, sizeof pw);
        size_t l = strlen(pw);
        if (l >= 6 && l <= 12)
            break;
        printf(C_ERR "Password must be between 6 and 12 characters.\n" C_RESET);
    }
    read_money("Please enter an initial deposit amount in rupees: ", dep, sizeof dep);
    loading("creating your account");
    const char *f[] = {"REGISTER", name, city, age, phone, pw, dep};
    int rc = call(7, f);
    show(rc);
    if (rc == 1) {
        uint64_t acct = strtoull(g_status + 3, NULL, 10);
        printf(C_TITLE "Please note your account number: %llu\n" C_RESET, (unsigned long long)acct);
        const char *l[] = {"LOGIN", g_status + 3, pw};
        if (call(3, l) == 1) {
            g_acct = acct;
            snprintf(g_name, sizeof g_name, "%s", name);
        }
    }
}

static void do_login(void)
{
    char acct[32], pw[64];
    banner("LOGIN");
    read_text("Account number: ", acct, sizeof acct);
    read_text("Password: ", pw, sizeof pw);
    const char *f[] = {"LOGIN", acct, pw};
    int rc = call(3, f);
    show(rc);
    if (rc == 1) {
        g_acct = strtoull(acct, NULL, 10);
        char *p = strstr(g_body.buf, "Welcome back, ");
        if (p) {
            snprintf(g_name, sizeof g_name, "%s", p + 14);
            g_name[strcspn(g_name, "!\n")] = '\0';
        }
    }
}

static void simple(const char *cmd)
{
    const char *f[] = {cmd};
    show(call(1, f));
}

static void do_money(const char *cmd, const char *prompt)
{
    char amt[32];
    read_money(prompt, amt, sizeof amt);
    const char *f[] = {cmd, amt};
    show(call(2, f));
}

static void do_transfer(void)
{
    char to[32], amt[32];
    read_text("Beneficiary account number: ", to, sizeof to);
    read_money("Amount to transfer in rupees: ", amt, sizeof amt);
    loading("processing");
    const char *f[] = {"TRANSFER", to, amt};
    show(call(3, f));
}

static void do_fd(void)
{
    banner("WELCOME TO THE FD SECTION");
    printf(C_MENU "1. Create an FD\n2. View my FDs\n3. Close an FD (premature or at maturity)\n4. Back\n" C_RESET);
    int c = read_int("Your choice: ", 1, 4);
    if (c == 2) {
        simple("FD_LIST");
        return;
    }
    if (c == 3) {
        simple("FD_LIST");
        char no[32];
        read_text("FD number to close: ", no, sizeof no);
        const char *f[] = {"FD_CLOSE", no};
        show(call(2, f));
        return;
    }
    if (c == 4)
        return;
    char amt[32], months[16], senior[4];
    read_money("Enter the initial deposit amount (not more than 2 crore): Rs ", amt, sizeof amt);
    snprintf(months, sizeof months, "%d", read_int("Enter the duration in months (not more than 60 months): ", 1, 60));
    snprintf(senior, sizeof senior, "%d", read_int("Enter 1 if you are a senior citizen, otherwise enter 0: ", 0, 1));
    loading("loading");
    const char *q[] = {"FD_QUOTE", amt, months, senior};
    int rc = call(4, q);
    show(rc);
    if (rc != 1)
        return;
    if (read_int("Enter 1 to proceed, 0 to cancel: ", 0, 1) == 0) {
        printf("Your FD application is cancelled.\n");
        return;
    }
    const char *f[] = {"FD_OPEN", amt, months, senior};
    show(call(4, f));
}

static void do_loan(void)
{
    banner("WELCOME TO THE LOAN SECTION");
    printf(C_MENU "1. Apply for a loan\n2. View my loans\n3. Draw (disburse) sanctioned funds\n4. Repay\n5. Back\n" C_RESET);
    int c = read_int("Your choice: ", 1, 5);
    if (c == 2) {
        simple("LOAN_LIST");
        return;
    }
    if (c == 3 || c == 4) {
        simple("LOAN_LIST");
        char no[32], amt[32];
        read_text("Loan number: ", no, sizeof no);
        read_money("Amount in rupees: ", amt, sizeof amt);
        const char *f[] = {c == 3 ? "LOAN_DRAW" : "LOAN_REPAY", no, amt};
        show(call(3, f));
        return;
    }
    if (c == 5)
        return;
    char cibil[16], lakhs[16], tenure[16], income[32];
    snprintf(cibil, sizeof cibil, "%d", read_int("Please enter your CIBIL score: ", 300, 900));
    int score = atoi(cibil);
    if (score < 550) {
        printf(C_ERR "We are sorry. You are not eligible to get a loan.\n" C_RESET);
        return;
    }
    const char *types[] = {"home", "personal", "student", "business"};
    int ntypes = score >= 700 ? 4 : score >= 670 ? 3 : score >= 620 ? 2 : 1;
    printf(C_MENU "You are eligible to get these loans:\n");
    for (int i = 0; i < ntypes; i++)
        printf("%d. %s loan\n", i + 1, types[i]);
    printf("%d. Exit\n" C_RESET, ntypes + 1);
    int t = read_int("Enter your choice: ", 1, ntypes + 1);
    if (t == ntypes + 1)
        return;
    int maxl = score >= 700 ? 250 : score >= 650 ? 100 : score >= 620 ? 70 : score >= 580 ? 45 : 20;
    printf("You can get a loan amounting up to %d lakhs.\n", maxl);
    snprintf(lakhs, sizeof lakhs, "%d", read_int("Please enter loan amount in lakhs: ", 1, maxl));
    snprintf(tenure, sizeof tenure, "%d", read_int("Please enter the tenure for your loan (24-300 months): ", 24, 300));
    read_money("Enter your monthly income in rupees: ", income, sizeof income);
    loading("checking eligibility");
    const char *q[] = {"LOAN_QUOTE", cibil, types[t - 1], lakhs, tenure, income};
    int rc = call(6, q);
    show(rc);
    if (rc != 1)
        return;
    printf(C_MENU "What would you like to do?\n1. Accept the loan.\n2. Exit\n" C_RESET);
    if (read_int("Your choice: ", 1, 2) != 1)
        return;
    const char *f[] = {"LOAN_APPLY", cibil, types[t - 1], lakhs, tenure, income};
    show(call(6, f));
}

static void do_insurance(void)
{
    banner("WELCOME TO THE INSURANCE SECTION");
    printf(C_MENU "1. Life insurance\n2. Health insurance\n3. Car insurance\n4. View my policies\n5. Back\n" C_RESET);
    int c = read_int("Enter your choice: ", 1, 5);
    if (c == 4) {
        simple("INS_LIST");
        return;
    }
    if (c == 5)
        return;
    char holder[64], benef[64] = "-", a1[32], a2[32], a3[32];
    const char *kind = c == 1 ? "life" : c == 2 ? "health" : "car";
    for (;;) {
        read_text(c == 3 ? "Enter the name of the car owner: " : "Enter the name of the policy holder: ", holder,
                  sizeof holder);
        if (valid_person_name(holder))
            break;
        printf(C_ERR "Enter a valid name.\n" C_RESET);
    }
    if (c != 3)
        for (;;) {
            read_text("Enter the name of the beneficiary: ", benef, sizeof benef);
            if (valid_person_name(benef))
                break;
            printf(C_ERR "Enter a valid name.\n" C_RESET);
        }
    if (c == 1) {
        printf("Select the coverage for your life insurance\n1. 50 lakhs\n2. 1 crore\n3. 2 crores\n");
        snprintf(a1, sizeof a1, "%d", read_int("Enter your choice (1, 2 or 3): ", 1, 3));
        snprintf(a2, sizeof a2, "0");
        snprintf(a3, sizeof a3, "0");
    } else if (c == 2) {
        snprintf(a1, sizeof a1, "%d", read_int("Enter your age: ", 1, 130));
        snprintf(a3, sizeof a3, "%d", read_int("Enter 1 if you smoke or consume alcohol, else 0: ", 0, 1));
        printf("The coverage amounts available:\n1. 2,00,000\n2. 5,00,000\n3. 10,00,000\n4. 20,00,000\n");
        snprintf(a2, sizeof a2, "%d", read_int("Choose your desired coverage: ", 1, 4));
    } else {
        snprintf(a1, sizeof a1, "%d", read_int("Enter the age of the owner: ", 1, 130));
        read_money("Enter the value of the car in rupees: ", a2, sizeof a2);
        snprintf(a3, sizeof a3, "%d", read_int("Enter the age of the car in years: ", 1, 60));
    }
    const char *q[] = {"INS_QUOTE", kind, a1, a2, a3};
    int rc = call(5, q);
    show(rc);
    if (rc != 1)
        return;
    if (read_int("Enter 1 to continue and 0 to exit: ", 0, 1) == 0) {
        printf("Your insurance application has been cancelled.\n");
        return;
    }
    const char *f[] = {"INS_BUY", kind, holder, benef, a1, a2, a3};
    show(call(7, f));
}

static void interactive(void)
{
    printf(C_TITLE "\n                      WELCOME TO PLUTUS FINCORP\n" C_RESET);
    printf(C_DIM "                  OS edition - every request is a scheduled job\n" C_RESET);
    for (;;) {
        if (!g_acct) {
            printf(C_MENU "\nWhat would you like to do?\n1. Register new account.\n2. Login into an existing account.\n3. Exit\n" C_RESET);
            int c = read_int("Your choice: ", 1, 3);
            if (c == 1)
                do_register();
            else if (c == 2)
                do_login();
            else
                break;
            continue;
        }
        printf(C_MENU "\n%s (%llu) - what would you like to do?\n" C_RESET, g_name, (unsigned long long)g_acct);
        printf(C_MENU "1. Balance          2. Deposit          3. Withdraw\n"
                      "4. Transfer         5. Statement        6. Fixed deposits\n"
                      "7. Loans            8. Insurance        9. Logout\n0. Exit\n" C_RESET);
        switch (read_int("Your choice: ", 0, 9)) {
        case 1: simple("BALANCE"); break;
        case 2: do_money("DEPOSIT", "Amount to deposit in rupees: "); break;
        case 3: do_money("WITHDRAW", "Amount to withdraw in rupees: "); break;
        case 4: do_transfer(); break;
        case 5: simple("STATEMENT"); break;
        case 6: do_fd(); break;
        case 7: do_loan(); break;
        case 8: do_insurance(); break;
        case 9:
            simple("LOGOUT");
            g_acct = 0;
            break;
        default: goto out;
        }
    }
out:
    simple("QUIT");
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN); /* report a lost server instead of dying silently */
    const char *dir = "plutus-data", *user = NULL, *pw = NULL;
    int ch;
    while ((ch = getopt(argc, argv, "+D:u:p:h")) != -1) {
        switch (ch) {
        case 'D': dir = optarg; break;
        case 'u': user = optarg; break;
        case 'p': pw = optarg; break;
        default:
            fprintf(stderr, "usage: plutus [-D datadir] [-u account -p password] [COMMAND ARGS...]\n");
            return 2;
        }
    }
    char sock[512];
    snprintf(sock, sizeof sock, "%s/plutus.sock", dir);
    connect_or_die(sock);
    if (optind >= argc) {
        interactive();
        return 0;
    }
    if (user) {
        const char *l[] = {"LOGIN", user, pw ? pw : ""};
        if (call(3, l) != 1) {
            fputs(g_body.buf, stdout);
            printf("%s\n", g_status);
            return 1;
        }
    }
    int rc = call(argc - optind, (const char *const *)argv + optind);
    fputs(g_body.buf, stdout);
    printf("%s\n", g_status);
    return rc == 1 ? 0 : 1;
}
