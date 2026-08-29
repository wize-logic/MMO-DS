/* Who is on this map, as names. */
#include "mapwin.h"

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
    mmo_mapwin w;
    mmo_mapwin_app app;

    printf("an empty list is a title, eight blank rows and NONE:\n");
    CHECK(MMO_MAPWIN_APP_ROWS == 10, "the app is ten rows");
    CHECK(MMO_MAPWIN_ROWS + 2 == MMO_MAPWIN_APP_ROWS,
          "those ten are title + eight + footer");

    mmo_mapwin_reset(&w);
    mmo_mapwin_render(&w, NULL, 0, &app);
    CHECK(strcmp(app.line[0], "HERE") == 0, "the title is HERE");
    CHECK(app.line[1][0] == '\0', "the first body row is empty");
    CHECK(app.line[8][0] == '\0', "the last body row is empty");
    CHECK(strcmp(app.line[9], "NONE") == 0, "the footer is NONE");
}

static void test_names(void)
{
    mmo_mapwin w;
    mmo_mapwin_line list[3];
    mmo_mapwin_app app;
    char row[MMO_MAPWIN_APP_COLS + 1];

    printf("a row is the name, and the footer is how many are here:\n");
    list[0].name = "PathA";
    list[1].name = "PathB";
    list[2].name = "PathC";

    mmo_mapwin_format(&list[0], row, sizeof row);
    CHECK(strcmp(row, "PathA") == 0, "a named row is the name alone");

    {
        mmo_mapwin_line empty;

        empty.name = "";
        mmo_mapwin_format(&empty, row, sizeof row);
        CHECK(strcmp(row, "?") == 0, "an empty name is a question mark");
    }

    mmo_mapwin_reset(&w);
    mmo_mapwin_render(&w, list, 3, &app);
    CHECK(strcmp(app.line[0], "HERE") == 0, "the title stays HERE");
    CHECK(strcmp(app.line[1], "PathA") == 0, "the first body row is PathA");
    CHECK(strcmp(app.line[2], "PathB") == 0, "the second body row is PathB");
    CHECK(strcmp(app.line[3], "PathC") == 0, "the third body row is PathC");
    CHECK(app.line[4][0] == '\0', "unused body rows stay empty");
    CHECK(strcmp(app.line[9], "3") == 0, "the footer is the count");
}

static void test_scroll_clamps(void)
{
    mmo_mapwin w;
    mmo_mapwin_line list[10];
    mmo_mapwin_app app;
    char names[10][4];
    int i;

    printf("the window scrolls without showing past either end:\n");
    for (i = 0; i < 10; i++) {
        names[i][0] = (char)('A' + i);
        names[i][1] = '\0';
        list[i].name = names[i];
    }
    mmo_mapwin_reset(&w);
    mmo_mapwin_render(&w, list, 10, &app);
    CHECK(strcmp(app.line[1], "A") == 0, "the top body row is the first name");
    CHECK(strcmp(app.line[8], "H") == 0, "the bottom body row is the eighth");
    CHECK(strcmp(app.line[9], "10") == 0, "the footer is still the full count");

    mmo_mapwin_scroll(&w, 1, 10);
    mmo_mapwin_render(&w, list, 10, &app);
    CHECK(strcmp(app.line[1], "B") == 0, "scrolling down by one starts at the second");
    CHECK(strcmp(app.line[8], "I") == 0, "and the eighth visible is the ninth");

    mmo_mapwin_scroll(&w, 100, 10);
    CHECK(w.scroll == 2, "scrolling past the end stops with eight still visible");
    mmo_mapwin_render(&w, list, 10, &app);
    CHECK(strcmp(app.line[1], "C") == 0, "the last window starts at the third");
    CHECK(strcmp(app.line[8], "J") == 0, "and ends on the last name");

    mmo_mapwin_scroll(&w, -100, 10);
    CHECK(w.scroll == 0, "scrolling up stops at the start of the list");
}

int mapwin_tests_run(void)
{
    failures = 0;
    test_empty_is_title_and_none();
    test_names();
    test_scroll_clamps();

    if (failures)
        printf("mapwin: %d check(s) FAILED\n", failures);
    else
        printf("mapwin: all checks passed\n");
    return failures;
}
