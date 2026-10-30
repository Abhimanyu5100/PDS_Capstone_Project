/* testlib.h - a tiny assertion framework for the unit tests. */
#ifndef PLUTUS_TESTLIB_H
#define PLUTUS_TESTLIB_H

#include <stdio.h>
#include <string.h>

extern int t_failures, t_checks;
extern const char *t_suite;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        t_checks++;                                                                     \
        if (!(cond)) {                                                                  \
            t_failures++;                                                               \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        long long _a = (long long)(a), _b = (long long)(b);                             \
        t_checks++;                                                                     \
        if (_a != _b) {                                                                 \
            t_failures++;                                                               \
            fprintf(stderr, "  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, \
                    #a, #b, _a, _b);                                                    \
        }                                                                               \
    } while (0)

#define CHECK_STR(a, b)                                                                 \
    do {                                                                                \
        const char *_a = (a), *_b = (b);                                                \
        t_checks++;                                                                     \
        if (strcmp(_a, _b)) {                                                           \
            t_failures++;                                                               \
            fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b); \
        }                                                                               \
    } while (0)

#define TEST(name) static void name(void)
#define RUN(name)                                                                       \
    do {                                                                                \
        int _f = t_failures;                                                            \
        name();                                                                         \
        printf("  %-40s %s\n", #name, t_failures == _f ? "ok" : "FAILED");              \
    } while (0)

void t_begin(const char *suite);
int t_end(void);

#endif
