/* The description a packet fills and four widgets draw. */
#include "widget.h"

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

static void fill_options(mmo_option_list *o, int count)
{
    int i;

    memset(o, 0, sizeof *o);
    o->kind = 2;
    o->count = count;
    for (i = 0; i < count && i < MMO_UI_OPTION_MAX; i++) {
        o->entry[i].type_id = (s8)(i + 1);
        o->entry[i].sub_type = 7;
        o->entry[i].value[0] = (s16)(100 + i);
        o->entry[i].value[1] = (s16)(200 + i);
    }
}

static void test_labelless_rows_keep_their_numbers(void)
{
    mmo_option_list o;
    mmo_screen s;

    printf("a row with no text on the wire is not given a name:\n");
    fill_options(&o, 3);
    CHECK(mmo_screen_from_option_list(&o, &s) == 1 && s.valid,
          "an option list compiles to a screen");
    CHECK(s.opcode == 0x5B && strcmp(s.name, "options") == 0,
          "the screen names the opcode it came from");
    CHECK(s.part_count == 1 && s.part[0].kind == MMO_WIDGET_GRID,
          "three options are the fixed grid");
    CHECK(s.part[0].row_count == 3, "every option is a row");
    CHECK(strstr(s.part[0].row[0].label, "1/7") != NULL
              && strstr(s.part[0].row[0].label, "100") != NULL,
          "the row shows the type and the values the packet carried");
    CHECK(s.part[0].row[2].value == 2,
          "with no id on the wire the pick is the position");
    CHECK(s.reply_op == 0, "no reply opcode is invented for a pick");
}

static void test_a_long_list_scrolls(void)
{
    mmo_option_list o;
    mmo_screen s;
    const mmo_widget *w;

    printf("a grid that would not fit becomes the scrolling list:\n");
    fill_options(&o, MMO_WIDGET_GRID_MAX);
    CHECK(mmo_screen_from_option_list(&o, &s) == 1
              && s.part[0].kind == MMO_WIDGET_GRID,
          "the largest grid is still a grid");
    fill_options(&o, MMO_WIDGET_GRID_MAX + 1);
    CHECK(mmo_screen_from_option_list(&o, &s) == 1
              && s.part[0].kind == MMO_WIDGET_LIST,
          "one row more is the list");

    w = &s.part[0];
    CHECK(mmo_widget_visible(w) == MMO_WIDGET_LIST_VISIBLE,
          "the list shows its window, not every row");
    CHECK(mmo_widget_first_visible(w, 0) == 0,
          "the cursor at the top shows the first row");
    CHECK(mmo_widget_first_visible(w, w->row_count - 1)
              == w->row_count - MMO_WIDGET_LIST_VISIBLE,
          "the cursor at the bottom stops at the last window");
    CHECK(mmo_widget_first_visible(w, 500)
              == w->row_count - MMO_WIDGET_LIST_VISIBLE,
          "a cursor past the end clamps rather than running off");
    CHECK(w->rect.height == 2 * MMO_WIDGET_LIST_VISIBLE,
          "the list is sized for the rows it shows");
}

static void test_list_window_labels(void)
{
    mmo_list_window l;
    mmo_screen s;

    printf("a row that does carry text keeps it:\n");
    memset(&l, 0, sizeof l);
    l.window_id = 41;
    l.first_page = 1;
    l.header[0] = 3;
    l.count = 2;
    l.row[0].row_type = 5;
    snprintf(l.row[0].label, sizeof l.row[0].label, "Potion");
    l.row[1].row_type = 6;

    CHECK(mmo_screen_from_list_window(&l, &s) == 1 && s.opcode == 0x5C,
          "a list window compiles to a screen");
    CHECK(strcmp(s.part[0].row[0].label, "Potion") == 0,
          "the label the server sent is the label drawn");
    CHECK(strstr(s.part[0].row[1].label, "type 6") != NULL,
          "a row with no label falls back to its position and type");
    CHECK(strstr(s.part[0].text, "window 41") != NULL
              && strstr(s.part[0].text, "first") != NULL,
          "the header carries the window id and the page flags");
    CHECK(mmo_screen_focus(&s) == 0, "the list is the part taking input");
}

static void test_names_pick_the_entity(void)
{
    mmo_name_choices n;
    mmo_screen s;
    s64 got = 0;

    printf("a packet that does name an entity per row picks by it:\n");
    memset(&n, 0, sizeof n);
    n.kind = 1;
    n.count = 2;
    n.entry[0].entity_id = 4242;
    snprintf(n.entry[0].name, sizeof n.entry[0].name, "RED");
    n.entry[1].entity_id = 4243;

    CHECK(mmo_screen_from_name_choices(&n, &s) == 1 && s.opcode == 0x5A,
          "name choices compile to a screen");
    CHECK(strcmp(s.part[0].row[0].label, "RED") == 0, "the name is the label");
    CHECK(mmo_widget_pick(&s.part[0], 0, &got) == 1 && got == 4242,
          "the pick reports the entity the row named");
    CHECK(mmo_widget_pick(&s.part[0], 9, &got) == 0 && got == 4242,
          "a cursor off the end picks nothing and changes nothing");
}

static void test_confirm_is_not_a_yes_no(void)
{
    mmo_confirm_prompt c;
    mmo_screen s;

    printf("a prompt with no text is not made into a question:\n");
    memset(&c, 0, sizeof c);
    c.visible = 1;
    c.entity_id = 77;
    c.request_s = 30;
    c.response_s = 30;

    CHECK(mmo_screen_from_confirm(&c, &s) == 1 && s.opcode == 0xD9,
          "a visible confirm compiles to a screen");
    CHECK(s.part_count == 1 && s.part[0].kind == MMO_WIDGET_MESSAGE,
          "it is the message box, not the yes/no menu");
    CHECK(mmo_screen_focus(&s) == -1, "a message-only screen takes no input");
    CHECK(strstr(s.part[0].text, "77") != NULL, "the entity it named is shown");
    CHECK(s.timeout_s == 30, "the second count the packet carried is kept");

    c.visible = 0;
    CHECK(mmo_screen_from_confirm(&c, &s) == 0 && !s.valid,
          "a confirm the server hid draws nothing");
}

static void test_geometry_is_the_engines(void)
{
    mmo_confirm_prompt c;
    mmo_option_list o;
    mmo_screen s;

    printf("the placements are the ones the engine already uses:\n");
    memset(&c, 0, sizeof c);
    c.visible = 1;
    mmo_screen_from_confirm(&c, &s);
    CHECK(s.part[0].rect.left == 2 && s.part[0].rect.top == 19
              && s.part[0].rect.width == 27 && s.part[0].rect.height == 4,
          "the message box is the field message box's own rectangle");

    fill_options(&o, 2);
    mmo_screen_from_option_list(&o, &s);
    CHECK(s.part[0].rect.top + s.part[0].rect.height == 19,
          "a short menu sits on top of the message box, not over it");
    CHECK(s.part[0].framed && s.part[0].cancelable,
          "a choice widget is framed and B closes it");
}

static void test_the_store_picks_the_screen(void)
{
    openmmo_ui ui;
    mmo_screen s;

    printf("the store compiles the packet that arrived last:\n");
    memset(&ui, 0, sizeof ui);
    CHECK(mmo_screen_from_ui(&ui, &s) == 0,
          "an empty store describes no screen");

    ui.valid = 1;
    ui.last_op = 0x59;
    ui.scale_valid = 1;
    ui.scale = 5;
    CHECK(mmo_screen_from_ui(&ui, &s) == 0,
          "the join's empty pages and scale draw nothing");

    fill_options(&ui.options, 2);
    ui.options_valid = 1;
    CHECK(mmo_screen_from_ui(&ui, &s) == 1 && s.opcode == 0x5B,
          "an opcode that describes no screen falls back to one that does");

    ui.list_valid = 1;
    ui.list.window_id = 9;
    ui.list.count = 1;
    snprintf(ui.list.row[0].label, sizeof ui.list.row[0].label, "row");
    ui.last_op = 0x5B;
    CHECK(mmo_screen_from_ui(&ui, &s) == 1 && s.opcode == 0x5B,
          "the last opcode wins over a shape that is merely held");
    ui.last_op = 0x5C;
    CHECK(mmo_screen_from_ui(&ui, &s) == 1 && s.opcode == 0x5C,
          "and the next packet replaces it");
}

/* The three packets `/ui` sends after a join, with the values a live
 * server put on the wire: an option list of two typed rows, a one-row
 * page labelled Potion, and a confirm on entity 0 waiting 30/30. */
static void test_the_live_ui_sequence(void)
{
    openmmo_ui ui;
    mmo_screen s;

    printf("the three screens a live /ui raises after a join:\n");
    memset(&ui, 0, sizeof ui);
    ui.valid = 1;
    ui.last_op = 0x59;
    ui.scale_valid = 1;
    ui.scale = 5;
    CHECK(mmo_screen_from_ui(&ui, &s) == 0,
          "the join's two empty pages raise no screen");

    ui.last_op = 0x5B;
    ui.options_valid = 1;
    ui.options.count = 2;
    ui.options.entry[0].type_id = 1;
    ui.options.entry[1].type_id = 2;
    CHECK(mmo_screen_from_ui(&ui, &s) == 1 && s.opcode == 0x5B
              && s.part[0].kind == MMO_WIDGET_GRID
              && s.part[0].row_count == 2,
          "the option list is a two-row grid");

    ui.last_op = 0x5C;
    ui.list_valid = 1;
    ui.list.window_id = 1;
    ui.list.count = 1;
    ui.list.row[0].row_type = 1;
    snprintf(ui.list.row[0].label, sizeof ui.list.row[0].label, "Potion");
    CHECK(mmo_screen_from_ui(&ui, &s) == 1 && s.opcode == 0x5C
              && s.part[0].kind == MMO_WIDGET_LIST
              && strcmp(s.part[0].row[0].label, "Potion") == 0,
          "the labelled page is a list that still says Potion");

    ui.last_op = 0xD9;
    ui.confirm_valid = 1;
    ui.confirm.visible = 1;
    ui.confirm.entity_id = 0;
    ui.confirm.request_s = 30;
    ui.confirm.response_s = 30;
    CHECK(mmo_screen_from_ui(&ui, &s) == 1 && s.opcode == 0xD9
              && s.part[0].kind == MMO_WIDGET_MESSAGE && s.timeout_s == 30,
          "the confirm is the message box and keeps its 30 seconds");
}

int widget_tests_run(void)
{
    failures = 0;
    test_labelless_rows_keep_their_numbers();
    test_a_long_list_scrolls();
    test_list_window_labels();
    test_names_pick_the_entity();
    test_confirm_is_not_a_yes_no();
    test_geometry_is_the_engines();
    test_the_store_picks_the_screen();
    test_the_live_ui_sequence();

    if (failures)
        printf("widget: %d check(s) FAILED\n", failures);
    else
        printf("widget: all checks passed\n");
    return failures;
}
