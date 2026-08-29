/* The connection, as ten rows. */
#include "netwin.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok   %s\n", msg);                                        \
        } else {                                                               \
            printf("  FAIL %s\n", msg);                                        \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_empty_is_title_and_none(void)
{
    mmo_netwin_view view;
    mmo_netwin_app app;

    printf("an empty view is a title, blank body and NONE:\n");
    CHECK(MMO_NETWIN_APP_ROWS == 10, "the app is ten rows");
    CHECK(1 + 1 + 1 + MMO_NETWIN_REASON_ROWS + 1 == MMO_NETWIN_APP_ROWS,
          "those ten are title + status + battle + reason + footer");

    memset(&view, 0, sizeof view);
    view.latency_ms = -1;
    mmo_netwin_render(&view, &app);
    CHECK(strcmp(app.line[0], "NET") == 0, "the title is NET");
    CHECK(app.line[1][0] == '\0', "the status row is empty");
    CHECK(app.line[2][0] == '\0', "the battle row is empty");
    CHECK(app.line[3][0] == '\0', "the first reason row is empty");
    CHECK(app.line[8][0] == '\0', "the last reason row is empty");
    CHECK(strcmp(app.line[9], "NONE") == 0, "the footer is NONE");
}

static void test_status_and_latency(void)
{
    mmo_netwin_view view;
    mmo_netwin_app app;
    char row[MMO_NETWIN_APP_COLS + 1];

    printf("status is the caption, latency is the footer:\n");
    memset(&view, 0, sizeof view);
    view.status = "ONLINE";
    view.latency_ms = -1;
    mmo_netwin_render(&view, &app);
    CHECK(strcmp(app.line[0], "NET") == 0, "the title stays NET");
    CHECK(strcmp(app.line[1], "ONLINE") == 0, "the status row is the caption");
    CHECK(strcmp(app.line[9], "NONE") == 0, "no RTT is NONE");

    mmo_netwin_format_latency(12, row, sizeof row);
    CHECK(strcmp(row, "12 MS") == 0, "a measured RTT is n MS");
    mmo_netwin_format_latency(0, row, sizeof row);
    CHECK(strcmp(row, "0 MS") == 0, "a zero RTT is still a measurement");
    mmo_netwin_format_latency(-1, row, sizeof row);
    CHECK(strcmp(row, "NONE") == 0, "a missing RTT is NONE");

    view.latency_ms = 12;
    mmo_netwin_render(&view, &app);
    CHECK(strcmp(app.line[9], "12 MS") == 0, "the footer is the RTT");
}

static void test_battle_and_reason(void)
{
    mmo_netwin_view view;
    mmo_netwin_app app;
    char row[MMO_NETWIN_APP_COLS + 1];
    const char *long_reason =
        "0123456789abcdef0123456789ABCDEF extra words after the wrap";

    printf("a battle hold sits under the status, a reason wraps:\n");
    memset(&view, 0, sizeof view);
    view.status = "ONLINE";
    view.latency_ms = -1;
    view.battle = "IN BATTLE - B TO RUN";
    mmo_netwin_render(&view, &app);
    CHECK(strcmp(app.line[2], "IN BATTLE - B TO RUN") == 0,
          "an active fight is the hold line");

    view.battle = "BATTLE...";
    mmo_netwin_render(&view, &app);
    CHECK(strcmp(app.line[2], "BATTLE...") == 0,
          "an entering fight is BATTLE...");

    view.status = "SERVER SAID NO";
    view.battle = NULL;
    view.reason = long_reason;
    mmo_netwin_render(&view, &app);
    CHECK(strcmp(app.line[1], "SERVER SAID NO") == 0,
          "a refusal is the caption");
    CHECK(app.line[2][0] == '\0', "no fight leaves the battle row empty");
    mmo_netwin_format_reason(long_reason, 0, row, sizeof row);
    CHECK(strcmp(row, "0123456789abcdef01234567") == 0,
          "the first reason row is the first 24 characters");
    CHECK(strcmp(app.line[3], "0123456789abcdef01234567") == 0,
          "that row is painted");
    mmo_netwin_format_reason(long_reason, 1, row, sizeof row);
    CHECK(strcmp(row, "89ABCDEF extra words aft") == 0,
          "the second reason row continues");
    CHECK(strcmp(app.line[4], "89ABCDEF extra words aft") == 0,
          "and is painted");
}

int netwin_tests_run(void)
{
    failures = 0;
    test_empty_is_title_and_none();
    test_status_and_latency();
    test_battle_and_reason();

    if (failures)
        printf("netwin: %d check(s) FAILED\n", failures);
    else
        printf("netwin: all checks passed\n");
    return failures;
}
