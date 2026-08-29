/* The party, as six rows of name and status. */
#include "partywin.h"

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
    mmo_partywin_app app;

    printf("an empty party is a title, eight blank rows and NONE:\n");
    CHECK(MMO_PARTYWIN_APP_ROWS == 10, "the app is ten rows");
    CHECK(MMO_PARTYWIN_ROWS + 2 == MMO_PARTYWIN_APP_ROWS,
          "those ten are title + eight + footer");

    mmo_partywin_render(NULL, 0, &app);
    CHECK(strcmp(app.line[0], "PARTY") == 0, "the title is PARTY");
    CHECK(app.line[1][0] == '\0', "the first body row is empty");
    CHECK(app.line[8][0] == '\0', "the last body row is empty");
    CHECK(strcmp(app.line[9], "NONE") == 0, "the footer is NONE");
}

static void test_name_level_hp(void)
{
    mmo_partywin_line list[3];
    mmo_partywin_app app;
    char row[MMO_PARTYWIN_APP_COLS + 1];

    printf("a hatched row is the name, with level and HP on the right:\n");
    list[0].name = "Turtwig";
    list[0].egg = 0;
    list[0].level = 5;
    list[0].hp = 20;
    list[1].name = "Chimchar";
    list[1].egg = 0;
    list[1].level = 5;
    list[1].hp = 0;
    list[2].name = "";
    list[2].egg = 0;
    list[2].level = 1;
    list[2].hp = 1;

    mmo_partywin_format(&list[0], row, sizeof row);
    CHECK(strncmp(row, "Turtwig", 7) == 0, "a hatched row starts with the name");
    CHECK(strcmp(row + strlen(row) - 6, "Lv5 20") == 0,
          "and ends with LvN and current HP");
    CHECK(strlen(row) == (size_t)MMO_PARTYWIN_APP_COLS,
          "a hatched row fills the 24-col window");
    mmo_partywin_format(&list[1], row, sizeof row);
    CHECK(strcmp(row + strlen(row) - 5, "Lv5 0") == 0,
          "a fainted member still shows HP 0");
    mmo_partywin_format(&list[2], row, sizeof row);
    CHECK(strncmp(row, "?", 1) == 0, "an empty name is a question mark");

    mmo_partywin_render(list, 3, &app);
    CHECK(strcmp(app.line[0], "PARTY") == 0, "the title stays PARTY");
    CHECK(strncmp(app.line[1], "Turtwig", 7) == 0, "the first body row is Turtwig");
    CHECK(strncmp(app.line[2], "Chimchar", 8) == 0, "the second is Chimchar");
    CHECK(app.line[4][0] == '\0', "unused body rows stay empty");
    CHECK(strcmp(app.line[9], "3") == 0, "the footer is the count");
}

static void test_egg_is_name_alone(void)
{
    mmo_partywin_line line;
    mmo_partywin_app app;
    char row[MMO_PARTYWIN_APP_COLS + 1];

    printf("an egg is the name alone:\n");
    line.name = "EGG";
    line.egg = 1;
    line.level = 1;
    line.hp = 20;
    mmo_partywin_format(&line, row, sizeof row);
    CHECK(strcmp(row, "EGG") == 0, "an egg row is the name with no stats");

    mmo_partywin_render(&line, 1, &app);
    CHECK(strcmp(app.line[1], "EGG") == 0, "the body row is the egg");
    CHECK(strcmp(app.line[9], "1") == 0, "the footer still counts it");
}

int partywin_tests_run(void)
{
    failures = 0;
    test_empty_is_title_and_none();
    test_name_level_hp();
    test_egg_is_name_alone();

    if (failures)
        printf("partywin: %d check(s) FAILED\n", failures);
    else
        printf("partywin: all checks passed\n");
    return failures;
}
