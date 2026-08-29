/* Eight rows of name and online state. */
#include "friendswin.h"

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
    mmo_friendswin w;
    mmo_friendswin_app app;

    printf("an empty list is a title, eight blank rows and NONE:\n");
    CHECK(MMO_FRIENDSWIN_APP_ROWS == 10, "the app is ten rows");
    CHECK(MMO_FRIENDSWIN_ROWS + 2 == MMO_FRIENDSWIN_APP_ROWS,
          "those ten are title + eight + footer");

    mmo_friendswin_reset(&w);
    mmo_friendswin_render(&w, NULL, 0, &app);
    CHECK(strcmp(app.line[0], "FRIENDS") == 0, "the title is FRIENDS");
    CHECK(app.line[1][0] == '\0', "the first body row is empty");
    CHECK(app.line[8][0] == '\0', "the last body row is empty");
    CHECK(strcmp(app.line[9], "NONE") == 0, "the footer is NONE");
}

static void test_name_and_online(void)
{
    mmo_friendswin w;
    mmo_friendswin_line list[3];
    mmo_friendswin_app app;
    char row[MMO_FRIENDSWIN_APP_COLS + 1];

    printf("a row is the name, with ON on the right when they are in the world:\n");
    list[0].name = "Red";
    list[0].online = 1;
    list[1].name = "Blue";
    list[1].online = 0;
    list[2].name = "Green";
    list[2].online = 0;

    mmo_friendswin_format(&list[0], row, sizeof row);
    CHECK(strncmp(row, "Red", 3) == 0, "an online row starts with the name");
    CHECK(strcmp(row + strlen(row) - 2, "ON") == 0,
          "and ends with ON");
    CHECK(strlen(row) == (size_t)MMO_FRIENDSWIN_APP_COLS,
          "an online row fills the 24-col window");
    mmo_friendswin_format(&list[1], row, sizeof row);
    CHECK(strcmp(row, "Blue") == 0, "an offline row is the name alone");

    {
        mmo_friendswin_line empty;

        empty.name = "";
        empty.online = 0;
        mmo_friendswin_format(&empty, row, sizeof row);
        CHECK(strcmp(row, "?") == 0, "an empty name is a question mark");
    }

    mmo_friendswin_reset(&w);
    mmo_friendswin_render(&w, list, 3, &app);
    CHECK(strcmp(app.line[0], "FRIENDS") == 0, "the title stays FRIENDS");
    CHECK(strncmp(app.line[1], "Red", 3) == 0, "the first body row is Red");
    CHECK(strcmp(app.line[1] + strlen(app.line[1]) - 2, "ON") == 0,
          "Red is marked online");
    CHECK(strcmp(app.line[2], "Blue") == 0, "the second body row is Blue");
    CHECK(strcmp(app.line[3], "Green") == 0, "the third body row is Green");
    CHECK(app.line[4][0] == '\0', "unused body rows stay empty");
    CHECK(strcmp(app.line[9], "3") == 0, "the footer is the count");
}

static void test_scroll_clamps(void)
{
    mmo_friendswin w;
    mmo_friendswin_line list[10];
    mmo_friendswin_app app;
    char names[10][4];
    int i;

    printf("the window scrolls without showing past either end:\n");
    for (i = 0; i < 10; i++) {
        names[i][0] = (char)('A' + i);
        names[i][1] = '\0';
        list[i].name = names[i];
        list[i].online = 0;
    }
    mmo_friendswin_reset(&w);
    mmo_friendswin_render(&w, list, 10, &app);
    CHECK(strcmp(app.line[1], "A") == 0, "the top body row is the first name");
    CHECK(strcmp(app.line[8], "H") == 0, "the bottom body row is the eighth");
    CHECK(strcmp(app.line[9], "10") == 0, "the footer is still the full count");

    mmo_friendswin_scroll(&w, 1, 10);
    mmo_friendswin_render(&w, list, 10, &app);
    CHECK(strcmp(app.line[1], "B") == 0, "scrolling down by one starts at the second");
    CHECK(strcmp(app.line[8], "I") == 0, "and the eighth visible is the ninth");

    mmo_friendswin_scroll(&w, 100, 10);
    CHECK(w.scroll == 2, "scrolling past the end stops with eight still visible");
    mmo_friendswin_render(&w, list, 10, &app);
    CHECK(strcmp(app.line[1], "C") == 0, "the last window starts at the third");
    CHECK(strcmp(app.line[8], "J") == 0, "and ends on the last name");

    mmo_friendswin_scroll(&w, -100, 10);
    CHECK(w.scroll == 0, "scrolling up stops at the start of the list");
}

int friendswin_tests_run(void)
{
    failures = 0;
    test_empty_is_title_and_none();
    test_name_and_online();
    test_scroll_clamps();

    if (failures)
        printf("friendswin: %d check(s) FAILED\n", failures);
    else
        printf("friendswin: all checks passed\n");
    return failures;
}
