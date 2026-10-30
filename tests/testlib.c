#include "testlib.h"

int t_failures, t_checks;
const char *t_suite;

void t_begin(const char *suite)
{
    t_suite = suite;
    printf("[%s]\n", suite);
}

int t_end(void)
{
    printf("[%s] %d checks, %d failures\n", t_suite, t_checks, t_failures);
    return t_failures ? 1 : 0;
}
