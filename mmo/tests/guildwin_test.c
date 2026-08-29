/* MOTD and who is on, at a glance. */
#include "guildwin.h"

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
    mmo_guildwin w;
    mmo_guildwin_view view;
    mmo_guildwin_app app;

    printf("an empty glance is a title, blank body and NONE:\n");
    CHECK(MMO_GUILDWIN_APP_ROWS == 10, "the app is ten rows");
    CHECK(1 + MMO_GUILDWIN_MOTD_ROWS + MMO_GUILDWIN_ONLINE_ROWS + 1
              == MMO_GUILDWIN_APP_ROWS,
          "those ten are title + motd + who is on + footer");

    memset(&view, 0, sizeof view);
    mmo_guildwin_reset(&w);
    mmo_guildwin_render(&w, &view, NULL, 0, &app);
    CHECK(strcmp(app.line[0], "GUILD") == 0, "the title is GUILD");
    CHECK(app.line[1][0] == '\0', "the first MOTD row is empty");
    CHECK(app.line[2][0] == '\0', "the second MOTD row is empty");
    CHECK(app.line[3][0] == '\0', "the first online row is empty");
    CHECK(app.line[8][0] == '\0', "the last online row is empty");
    CHECK(strcmp(app.line[9], "NONE") == 0, "the footer is NONE");
}

static void test_motd_and_who_is_on(void)
{
    mmo_guildwin w;
    mmo_guildwin_view view;
    mmo_guildwin_line list[3];
    mmo_guildwin_app app;
    char row[MMO_GUILDWIN_APP_COLS + 1];

    printf("MOTD wraps, who is on is the online names, the tag sits on the title:\n");
    memset(&view, 0, sizeof view);
    view.in_guild = 1;
    view.name = "Knights";
    view.tag = "KNT";
    view.motd = "Your Team has been successfully created!";
    list[0].name = "PathB";
    list[0].online = 1;
    list[1].name = "PathA";
    list[1].online = 0;
    list[2].name = "PathC";
    list[2].online = 1;

    mmo_guildwin_format_title(&view, row, sizeof row);
    CHECK(strncmp(row, "Knights", 7) == 0, "the title starts with the name");
    CHECK(strcmp(row + strlen(row) - 3, "KNT") == 0,
          "and ends with the tag");
    CHECK(strlen(row) == (size_t)MMO_GUILDWIN_APP_COLS,
          "a tagged title fills the 24-col window");

    mmo_guildwin_format_motd(view.motd, 0, row, sizeof row);
    CHECK(strcmp(row, "Your Team has been succe") == 0,
          "the first MOTD row is the first 24 characters");
    mmo_guildwin_format_motd(view.motd, 1, row, sizeof row);
    CHECK(strcmp(row, "ssfully created!") == 0,
          "the second MOTD row is the rest");

    mmo_guildwin_format_member(&list[0], row, sizeof row);
    CHECK(strncmp(row, "PathB", 5) == 0, "an online row starts with the name");
    CHECK(strcmp(row + strlen(row) - 2, "ON") == 0, "and ends with ON");

    CHECK(mmo_guildwin_online_count(list, 3) == 2,
          "the online count skips whoever is offline");

    mmo_guildwin_reset(&w);
    mmo_guildwin_render(&w, &view, list, 3, &app);
    CHECK(strncmp(app.line[0], "Knights", 7) == 0, "the title stays the name");
    CHECK(strcmp(app.line[1], "Your Team has been succe") == 0,
          "row 1 is the first MOTD wrap");
    CHECK(strcmp(app.line[2], "ssfully created!") == 0,
          "row 2 is the rest of the MOTD");
    CHECK(strncmp(app.line[3], "PathB", 5) == 0, "the first online row is PathB");
    CHECK(strcmp(app.line[3] + strlen(app.line[3]) - 2, "ON") == 0,
          "PathB is marked online");
    CHECK(strncmp(app.line[4], "PathC", 5) == 0,
          "PathA is offline so PathC is next");
    CHECK(app.line[5][0] == '\0', "unused online rows stay empty");
    CHECK(strcmp(app.line[9], "2") == 0,
          "without a log the footer is the online count");
}

static void test_log_moved(void)
{
    mmo_guildwin w;
    mmo_guildwin_view view;
    mmo_guildwin_line list[1];
    mmo_guildwin_app app;

    printf("a held log moves the footer to LOG n:\n");
    memset(&view, 0, sizeof view);
    view.in_guild = 1;
    view.name = "Knights";
    view.tag = "KNT";
    view.motd = "Hello knights";
    view.log_valid = 1;
    view.log_count = 1;
    list[0].name = "PathB";
    list[0].online = 1;

    mmo_guildwin_reset(&w);
    mmo_guildwin_render(&w, &view, list, 1, &app);
    CHECK(strcmp(app.line[1], "Hello knights") == 0, "a short MOTD is one row");
    CHECK(app.line[2][0] == '\0', "the second MOTD row stays empty");
    CHECK(strcmp(app.line[9], "LOG 1") == 0, "the footer is LOG 1");
}

static void test_scroll_clamps(void)
{
    mmo_guildwin w;
    mmo_guildwin_view view;
    mmo_guildwin_line list[10];
    mmo_guildwin_app app;
    char names[10][4];
    int i;

    printf("the online window scrolls without showing past either end:\n");
    memset(&view, 0, sizeof view);
    view.in_guild = 1;
    view.name = "Knights";
    for (i = 0; i < 10; i++) {
        names[i][0] = (char)('A' + i);
        names[i][1] = '\0';
        list[i].name = names[i];
        list[i].online = 1;
    }
    mmo_guildwin_reset(&w);
    mmo_guildwin_render(&w, &view, list, 10, &app);
    CHECK(strncmp(app.line[3], "A", 1) == 0, "the top online row is the first");
    CHECK(strncmp(app.line[8], "F", 1) == 0, "the bottom online row is the sixth");
    CHECK(strcmp(app.line[9], "10") == 0, "the footer is still the full online count");

    mmo_guildwin_scroll(&w, 1, 10);
    mmo_guildwin_render(&w, &view, list, 10, &app);
    CHECK(strncmp(app.line[3], "B", 1) == 0, "scrolling down by one starts at the second");
    CHECK(strncmp(app.line[8], "G", 1) == 0, "and the sixth visible is the seventh");

    mmo_guildwin_scroll(&w, 100, 10);
    CHECK(w.scroll == 4, "scrolling past the end stops with six still visible");
    mmo_guildwin_render(&w, &view, list, 10, &app);
    CHECK(strncmp(app.line[3], "E", 1) == 0, "the last window starts at the fifth");
    CHECK(strncmp(app.line[8], "J", 1) == 0, "and ends on the last name");

    mmo_guildwin_scroll(&w, -100, 10);
    CHECK(w.scroll == 0, "scrolling up stops at the start of the list");
}

int guildwin_tests_run(void)
{
    failures = 0;
    test_empty_is_title_and_none();
    test_motd_and_who_is_on();
    test_log_moved();
    test_scroll_clamps();

    if (failures)
        printf("guildwin: %d check(s) FAILED\n", failures);
    else
        printf("guildwin: all checks passed\n");
    return failures;
}
