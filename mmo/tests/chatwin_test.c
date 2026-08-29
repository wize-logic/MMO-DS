/* The chat window a person reads and types into. */
#include "chatwin.h"
#include "entry.h"
#include "game.h"

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

static void test_hidden_until_shown(void)
{
    mmo_chatwin w;

    printf("the window is a joined-session surface:\n");
    mmo_chatwin_reset(&w);
    CHECK(w.mode == MMO_CHATWIN_HIDDEN, "it starts hidden");
    CHECK(!mmo_chatwin_visible(&w), "and is not drawn");
    CHECK(mmo_chatwin_toggle_compose(&w) == 0, "START does nothing while hidden");
    CHECK(w.mode == MMO_CHATWIN_HIDDEN, "and it stays hidden");
    mmo_chatwin_show(&w);
    CHECK(w.mode == MMO_CHATWIN_LOG, "show opens the log");
    CHECK(mmo_chatwin_visible(&w) && !mmo_chatwin_composing(&w),
          "the log is visible and not composing");
    mmo_chatwin_hide(&w);
    CHECK(w.mode == MMO_CHATWIN_HIDDEN, "hide puts it away");
}

static void test_compose_is_the_field(void)
{
    mmo_chatwin w;

    printf("compose is the re-hosted engine keyboard:\n");
    mmo_chatwin_reset(&w);
    mmo_chatwin_show(&w);
    CHECK(mmo_chatwin_toggle_compose(&w) == 1, "START opens compose");
    CHECK(mmo_chatwin_composing(&w) && mmo_chatwin_needs_entry(&w),
          "compose wants somewhere to type");
    CHECK(mmo_entry_path_for(MMO_ENTRY_CHAT) == MMO_ENTRY_FIELD,
          "and the table sends it to the field");
    CHECK(mmo_entry_max_units(MMO_ENTRY_CHAT) == OSK_MAX_TEXT,
          "capped at the field's 64");
    CHECK(mmo_chatwin_toggle_compose(&w) == 1, "START again closes compose");
    CHECK(w.mode == MMO_CHATWIN_LOG && !mmo_chatwin_needs_entry(&w),
          "back to the log, keyboard closed");
}

static void test_format_and_colour(void)
{
    mmo_chatwin_line line;
    char buf[MMO_CHATWIN_LINE];

    printf("a line is formatted by type, and types do not share a colour:\n");
    line.type = MMO_CHAT_NORMAL;
    line.sender = "Dawn";
    line.text = "hello";
    mmo_chatwin_format(&line, buf, sizeof buf);
    CHECK(strcmp(buf, "Dawn: hello") == 0, "a player line is name then text");

    line.type = MMO_CHAT_SYSTEM;
    line.sender = "ignored";
    line.text = "Welcome to OpenMMO!";
    mmo_chatwin_format(&line, buf, sizeof buf);
    CHECK(strcmp(buf, "Welcome to OpenMMO!") == 0,
          "a system line has no sender prefix");

    line.type = MMO_CHAT_NOTICE;
    line.sender = "";
    line.text = "Player Sinnoh joined";
    mmo_chatwin_format(&line, buf, sizeof buf);
    CHECK(strcmp(buf, "Player Sinnoh joined") == 0,
          "a notice is just the text");

    CHECK(mmo_chatwin_color(MMO_CHAT_NORMAL)
              != mmo_chatwin_color(MMO_CHAT_WHISPER),
          "a whisper is not the same colour as a local line");
    CHECK(mmo_chatwin_color(MMO_CHAT_SYSTEM)
              != mmo_chatwin_color(MMO_CHAT_NOTICE),
          "a notice is not the same colour as a system line");
    CHECK(mmo_chatwin_color(MMO_CHAT_SHOUT)
              != mmo_chatwin_color(MMO_CHAT_NORMAL),
          "a shout is not the same colour as a local line");
}

static void test_log_newest_at_bottom(void)
{
    mmo_chatwin w;
    mmo_chatwin_line log[10];
    mmo_chatwin_row row;
    int i;

    printf("the log shows the newest rows at the bottom:\n");
    mmo_chatwin_reset(&w);
    mmo_chatwin_show(&w);
    for (i = 0; i < 10; i++) {
        log[i].type = MMO_CHAT_NORMAL;
        log[i].sender = "A";
        log[i].text = (i == 9) ? "last" : (i == 2) ? "third" : "x";
    }
    CHECK(mmo_chatwin_visible_count(&w, 10) == MMO_CHATWIN_LOG_ROWS,
          "a long log is capped at the log budget");
    CHECK(mmo_chatwin_get_row(&w, log, 10, 0, &row) == 1, "the top row reads");
    CHECK(strstr(row.text, "third") != NULL,
          "the top of an 8-row window on 10 lines is line 2");
    CHECK(mmo_chatwin_get_row(&w, log, 10, 7, &row) == 1, "the bottom row reads");
    CHECK(strstr(row.text, "last") != NULL, "the bottom is the newest");

    mmo_chatwin_toggle_compose(&w);
    CHECK(mmo_chatwin_visible_count(&w, 10) == MMO_CHATWIN_COMPOSE_ROWS,
          "compose shows fewer rows so the field fits");
    CHECK(mmo_chatwin_get_row(&w, log, 10, 2, &row) == 1
              && strstr(row.text, "last") != NULL,
          "compose still pins the newest at the bottom");
}

static void test_scroll_clamps(void)
{
    mmo_chatwin w;
    mmo_chatwin_line log[10];
    mmo_chatwin_row row;
    int i;

    printf("scroll stops at each end of the log:\n");
    mmo_chatwin_reset(&w);
    mmo_chatwin_show(&w);
    for (i = 0; i < 10; i++) {
        log[i].type = MMO_CHAT_NORMAL;
        log[i].sender = "A";
        log[i].text = (i == 0) ? "first" : "x";
    }
    mmo_chatwin_scroll(&w, 100, 10);
    CHECK(mmo_chatwin_get_row(&w, log, 10, 0, &row) == 1
              && strstr(row.text, "first") != NULL,
          "scrolling older stops on the first line");
    mmo_chatwin_scroll(&w, -100, 10);
    CHECK(w.scroll == 0, "scrolling newer stops at the live end");
}

static void test_app_is_title_eight_footer(void)
{
    mmo_chatwin w;
    mmo_chatwin_line log[10];
    mmo_chatwin_app app;
    int i;

    printf("the walking log is a title, eight body rows and a footer:\n");
    CHECK(MMO_CHATWIN_APP_ROWS == 10, "the app is ten rows");
    CHECK(MMO_CHATWIN_LOG_ROWS + 2 == MMO_CHATWIN_APP_ROWS,
          "those ten are title + log budget + footer");

    mmo_chatwin_reset(&w);
    mmo_chatwin_render_app(&w, NULL, 0, &app, NULL);
    CHECK(strcmp(app.line[0], "CHAT") == 0, "hidden still has a title");
    CHECK(app.type[0] == -1, "the title is chrome");
    CHECK(app.line[1][0] == '\0', "hidden has an empty body");
    CHECK(strcmp(app.line[9], "START TO TALK") == 0, "and the same footer");

    mmo_chatwin_show(&w);
    for (i = 0; i < 10; i++) {
        log[i].type = (i == 9) ? MMO_CHAT_SHOUT : MMO_CHAT_NORMAL;
        log[i].sender = "Dawn";
        log[i].text = (i == 9) ? "last" : (i == 2) ? "third" : "x";
    }
    mmo_chatwin_render_app(&w, log, 10, &app, NULL);
    CHECK(strcmp(app.line[0], "CHAT") == 0, "the title stays CHAT");
    CHECK(strstr(app.line[1], "third") != NULL,
          "the top body row is the top of the 8-row window");
    CHECK(strstr(app.line[8], "last") != NULL, "the bottom body row is newest");
    CHECK(app.type[8] == MMO_CHAT_SHOUT, "a body row keeps its wire type");
    CHECK(app.type[8] != app.type[1], "types on the body are not collapsed");
    CHECK(strcmp(app.line[9], "START TO TALK") == 0, "the footer is the prompt");

    mmo_chatwin_toggle_compose(&w);
    mmo_chatwin_render_app(&w, log, 10, &app, NULL);
    CHECK(strcmp(app.line[0], "CHAT") == 0, "compose keeps the title");
    CHECK(strcmp(app.line[9], "START CLOSES") == 0,
          "compose tells START to close");
    CHECK(strstr(app.line[1], "third") == NULL,
          "compose does not put the log on the device");

    {
        osk_state k;

        osk_reset(&k);
        mmo_chatwin_render_app(&w, log, 10, &app, &k);
        CHECK(strstr(app.line[2], "ABC") != NULL,
              "compose paints the home row");
        CHECK(strstr(app.line[3], "BCDEFGHIJ") != NULL,
              "compose paints the upper-page letters");
    }
}

int chatwin_tests_run(void)
{
    failures = 0;
    test_hidden_until_shown();
    test_compose_is_the_field();
    test_format_and_colour();
    test_log_newest_at_bottom();
    test_scroll_clamps();
    test_app_is_title_eight_footer();

    if (failures)
        printf("chatwin: %d check(s) FAILED\n", failures);
    else
        printf("chatwin: all checks passed\n");
    return failures;
}
