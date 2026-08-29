/* The in-binary self-test aggregator is wired and green. */
#include <stdio.h>

#include "selftest.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            printf("  ok   %s\n", msg);                                         \
        } else {                                                                \
            printf("  FAIL %s\n", msg);                                         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

int selftest_tests_run(void)
{
    failures = 0;
    printf("in-binary self-test aggregator:\n");

    FILE *sink = tmpfile();          /* suppress the aggregator's own ok/FAIL lines */
    int rc = openmmo_selftest_run(sink ? sink : stderr);
    if (sink)
        fclose(sink);

    CHECK(rc == 0, "openmmo_selftest_run reports zero failures");

    if (failures)
        printf("selftest-agg: %d check(s) FAILED\n", failures);
    else
        printf("selftest-agg: all checks passed\n");
    return failures;
}
