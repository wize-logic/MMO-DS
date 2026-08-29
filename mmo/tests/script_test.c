/* The client-action script parser. */
#include <stdio.h>
#include <string.h>

#include "script.h"

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

/* Parse a NUL-terminated literal; returns the rc, fills *out. */
static int parse(const char *s, openmmo_script *out, char *err, size_t errsz)
{
    return openmmo_script_parse(s, strlen(s), out, err, errsz);
}

int script_tests_run(void)
{
    openmmo_script scr;
    char err[128];

    failures = 0;
    printf("client-action script parser:\n");

    /* A full session script exercising every directive, plus comments, blank
     * lines, indentation, CRLF and a trailing line with no newline. */
    static const char *ok =
        "# a reproducible join-and-walk session\r\n"
        "\r\n"
        "   connect\r\n"
        "await authed\r\n"
        "await in_game\r\n"
        "frames 120\r\n"
        "await-event map\r\n"
        "move north\r\n"
        "move east\r\n"
        "chat /testbattle now please\r\n"
        "await-event encounter\r\n"
        "disconnect";
    int rc = parse(ok, &scr, err, sizeof err);
    CHECK(rc == 0, "the full script parses");
    CHECK(scr.count == 10, "ten directives, comments and blanks skipped");

    CHECK(scr.act[0].op == OPENMMO_SCRIPT_CONNECT, "0: connect");
    CHECK(scr.act[1].op == OPENMMO_SCRIPT_AWAIT_STATUS &&
          scr.act[1].arg == OPENMMO_AUTHED, "1: await authed");
    CHECK(scr.act[2].op == OPENMMO_SCRIPT_AWAIT_STATUS &&
          scr.act[2].arg == OPENMMO_IN_GAME, "2: await in_game");
    CHECK(scr.act[3].op == OPENMMO_SCRIPT_FRAMES &&
          scr.act[3].arg == 120, "3: frames 120");
    CHECK(scr.act[4].op == OPENMMO_SCRIPT_AWAIT_EVENT &&
          scr.act[4].arg == OPENMMO_EV_MAP, "4: await-event map");
    CHECK(scr.act[5].op == OPENMMO_SCRIPT_MOVE && scr.act[5].arg == 0, "5: move north (dir 0)");
    CHECK(scr.act[6].op == OPENMMO_SCRIPT_MOVE && scr.act[6].arg == 3, "6: move east (dir 3)");
    CHECK(scr.act[7].op == OPENMMO_SCRIPT_CHAT &&
          strcmp(scr.act[7].text, "/testbattle now please") == 0,
          "7: chat keeps the whole line");
    CHECK(scr.act[8].op == OPENMMO_SCRIPT_AWAIT_EVENT &&
          scr.act[8].arg == OPENMMO_EV_ENCOUNTER, "8: await-event encounter");
    CHECK(scr.act[9].op == OPENMMO_SCRIPT_DISCONNECT, "9: disconnect");

    /* Source line numbers survive comments/blanks, for the executor's trace. */
    CHECK(scr.act[0].line == 3, "connect is source line 3");
    CHECK(scr.act[9].line == 12, "disconnect is source line 12");

    /* All four facings map to DIR_* N=0/S=1/W=2/E=3. */
    rc = parse("move north\nmove south\nmove west\nmove east\n", &scr, err, sizeof err);
    CHECK(rc == 0 && scr.count == 4 &&
          scr.act[0].arg == 0 && scr.act[1].arg == 1 &&
          scr.act[2].arg == 2 && scr.act[3].arg == 3, "the four facings map to DIR_*");

    /* An empty / comment-only script is valid but yields nothing. */
    rc = parse("# nothing here\n\n   \n", &scr, err, sizeof err);
    CHECK(rc == 0 && scr.count == 0, "a comment-only script parses to zero directives");

    /* A trailing inline comment is stripped from a directive line... */
    rc = parse("frames 30   # let it settle\n", &scr, err, sizeof err);
    CHECK(rc == 0 && scr.count == 1 && scr.act[0].op == OPENMMO_SCRIPT_FRAMES &&
          scr.act[0].arg == 30, "an inline comment is stripped off frames");

    /* ...but a chat message is verbatim: its '#' (even after a space) is kept. */
    rc = parse("chat go to #5 now\n", &scr, err, sizeof err);
    CHECK(rc == 0 && scr.count == 1 && scr.act[0].op == OPENMMO_SCRIPT_CHAT &&
          strcmp(scr.act[0].text, "go to #5 now") == 0,
          "a chat message keeps its '#' verbatim");

    /* Malformed lines are rejected at their line. */
    rc = parse("connect\nwiggle\n", &scr, err, sizeof err);
    CHECK(rc == -1 && strstr(err, "line 2") != NULL, "an unknown directive fails at its line");

    rc = parse("await\n", &scr, err, sizeof err);
    CHECK(rc == -1 && strstr(err, "line 1") != NULL, "await with no status fails");

    rc = parse("await bogus\n", &scr, err, sizeof err);
    CHECK(rc == -1 && strstr(err, "status") != NULL, "an unknown status name fails");

    rc = parse("await-event bogus\n", &scr, err, sizeof err);
    CHECK(rc == -1, "an unknown event kind fails");

    rc = parse("move up\n", &scr, err, sizeof err);
    CHECK(rc == -1, "an unknown direction fails");

    rc = parse("frames 0\n", &scr, err, sizeof err);
    CHECK(rc == -1, "frames 0 fails (a count must be positive)");

    rc = parse("frames -5\n", &scr, err, sizeof err);
    CHECK(rc == -1, "a negative frame count fails");

    rc = parse("frames lots\n", &scr, err, sizeof err);
    CHECK(rc == -1, "a non-numeric frame count fails");

    rc = parse("chat\n", &scr, err, sizeof err);
    CHECK(rc == -1, "chat with no message fails");

    if (failures)
        printf("script: %d check(s) FAILED\n", failures);
    else
        printf("script: all checks passed\n");
    return failures;
}
