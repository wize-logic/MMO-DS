/* The on-screen keyboard state machine. */
#include <stdio.h>
#include <string.h>

#include "osk.h"
#include "text_channel.h"
#include "charcode.h"

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

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

static int tap(osk_state *k, char ch)
{
    int i, n = osk_key_count(k);

    for (i = 0; i < n; i++) {
        osk_key_view v;

        osk_get_key(k, i, &v);
        if (v.kind == OSK_KEY_CHAR
            && lc((unsigned char)v.label[0]) == lc((unsigned char)ch)
            && v.label[1] == '\0')
            return osk_pen(k, v.x + v.w / 2, v.y + v.h / 2);
    }
    return 0;
}

static int tap_action(osk_state *k, osk_keykind kind)
{
    int i, n = osk_key_count(k);

    for (i = 0; i < n; i++) {
        osk_key_view v;

        osk_get_key(k, i, &v);
        if (v.kind == kind)
            return osk_pen(k, v.x + v.w / 2, v.y + v.h / 2);
    }
    return 0;
}

static int tap_page(osk_state *k, unsigned page)
{
    int i, n = osk_key_count(k);

    for (i = 0; i < n; i++) {
        osk_key_view v;

        osk_get_key(k, i, &v);
        if (v.kind == OSK_KEY_PAGE && v.unit == (uint16_t)page)
            return osk_pen(k, v.x + v.w / 2, v.y + v.h / 2);
    }
    return 0;
}

static void text_ascii(const osk_state *k, char *out, size_t cap)
{
    size_t i, n = osk_text_len(k);

    if (n > cap - 1)
        n = cap - 1;
    for (i = 0; i < n; i++)
        out[i] = (char)(k->text[i] & 0xFF);
    out[n] = '\0';
}

static void test_layout(void)
{
    osk_state k;
    osk_key_view v;
    int i, all_in, has_numpad, has_slash;

    printf("the layout is the engine 6x13 grid in the device window:\n");
    osk_reset(&k);
    CHECK(osk_page(&k) == 0, "the engine home page is upper");
    CHECK(osk_key_count(&k) > 13, "a page has more than one row of keys");
    CHECK(osk_get_key(&k, 0, &v) && v.kind == OSK_KEY_PAGE && v.unit == 0,
          "the home row starts on ABC");
    CHECK(!osk_get_key(&k, osk_key_count(&k), &v),
          "an out-of-range index is refused");
    CHECK(osk_get_key(&k, k.cursor, &v) && v.kind == OSK_KEY_CHAR
              && v.label[0] == 'A',
          "the cursor starts on A");

    all_in = 1;
    has_numpad = 0;
    for (i = 0; i < osk_key_count(&k); i++) {
        osk_get_key(&k, i, &v);
        if (v.x < OSK_ORIGIN_X || v.y < OSK_ORIGIN_Y
            || v.x + v.w > OSK_ORIGIN_X + OSK_WIDTH
            || v.y + v.h > OSK_ORIGIN_Y + OSK_HEIGHT)
            all_in = 0;
        if (v.kind == OSK_KEY_PAGE && v.unit == 4)
            has_numpad = 1;
    }
    CHECK(all_in, "every key rectangle is inside the device window");
    CHECK(has_numpad, "the home row has a numpad button");

    CHECK(tap(&k, '0') && tap(&k, '1'), "digits are on the letter page");
    {
        char s[OSK_MAX_TEXT + 1];

        text_ascii(&k, s, sizeof s);
        CHECK(strcmp(s, "01") == 0, "upper-page digits insert 0 then 1");
    }

    osk_reset(&k);
    CHECK(tap_page(&k, 2), "switched to the others page");
    has_slash = tap(&k, '/');
    CHECK(has_slash, "CHAR_SLASH is on the others page");
    {
        char s[OSK_MAX_TEXT + 1];

        text_ascii(&k, s, sizeof s);
        CHECK(strcmp(s, "/") == 0, "a slash types as U+002F");
    }

    osk_reset(&k);
    CHECK(tap_page(&k, 4), "switched to the numpad");
    CHECK(osk_page(&k) == 4, "the page is 4");
    CHECK(tap(&k, '7'), "numpad digits are reachable");
    CHECK(tap_page(&k, 0), "and the home row can leave the numpad");
    CHECK(osk_page(&k) == 0, "back on upper");
}

static void test_movement(void)
{
    osk_state k;
    osk_key_view v;

    printf("cursor movement wraps within a row and skips empty rows:\n");
    osk_reset(&k);
    osk_move(&k, OSK_LEFT);
    osk_get_key(&k, k.cursor, &v);
    CHECK(v.kind == OSK_KEY_CHAR && v.label[0] == '.',
          "LEFT from A wraps to the period at the row end");

    osk_reset(&k);
    osk_move(&k, OSK_DOWN);
    osk_get_key(&k, k.cursor, &v);
    CHECK(v.kind == OSK_KEY_CHAR && v.label[0] == 'K',
          "DOWN from A lands on K");

    osk_reset(&k);
    osk_move(&k, OSK_UP);
    osk_get_key(&k, k.cursor, &v);
    CHECK(v.kind == OSK_KEY_PAGE && v.unit == 0,
          "UP from A lands on the ABC button");
    osk_move(&k, OSK_RIGHT);
    osk_move(&k, OSK_RIGHT);
    osk_move(&k, OSK_RIGHT);
    osk_move(&k, OSK_RIGHT);
    osk_move(&k, OSK_RIGHT);
    osk_get_key(&k, k.cursor, &v);
    CHECK(v.kind == OSK_KEY_ENTER, "RIGHT along the home row reaches OK");
}

static void test_typing(void)
{
    osk_state k;
    char s[OSK_MAX_TEXT + 1];

    printf("typing by pen tap assembles the line, pages choose case:\n");
    osk_reset(&k);
    CHECK(tap_page(&k, 1), "switched to lower");
    CHECK(tap(&k, 'h') && tap(&k, 'i'), "tapped h then i");
    text_ascii(&k, s, sizeof s);
    CHECK(strcmp(s, "hi") == 0, "lower page -> \"hi\"");

    CHECK(tap_page(&k, 0), "switched to upper");
    CHECK(tap(&k, 'a') && tap(&k, 'b'), "tapped A then B");
    text_ascii(&k, s, sizeof s);
    CHECK(strcmp(s, "hiAB") == 0, "upper page -> \"hiAB\"");

    tap_action(&k, OSK_KEY_SPACE);
    tap(&k, '5');
    text_ascii(&k, s, sizeof s);
    CHECK(strcmp(s, "hiAB 5") == 0, "space and a digit append literally");
}

static void test_editing_and_commit(void)
{
    osk_state k;
    char s[OSK_MAX_TEXT + 1];

    printf("backspace deletes and OK commits:\n");
    osk_reset(&k);
    tap_page(&k, 1);
    tap(&k, 'a');
    tap(&k, 'b');
    tap(&k, 'c');
    osk_backspace(&k);
    text_ascii(&k, s, sizeof s);
    CHECK(strcmp(s, "ab") == 0, "B-button backspace drops the last unit");

    tap_action(&k, OSK_KEY_BACKSPACE);
    text_ascii(&k, s, sizeof s);
    CHECK(strcmp(s, "a") == 0, "the DEL key deletes too");

    CHECK(!osk_committed(&k), "not committed before OK");
    tap_action(&k, OSK_KEY_ENTER);
    CHECK(osk_committed(&k), "OK commits the line");
    osk_reset(&k);
    CHECK(!osk_committed(&k) && osk_text_len(&k) == 0,
          "reset clears commit and text");
}

static int first_char_index(const osk_state *k)
{
    int i, n = osk_key_count(k);

    for (i = 0; i < n; i++) {
        osk_key_view v;

        osk_get_key(k, i, &v);
        if (v.kind == OSK_KEY_CHAR)
            return i;
    }
    return 0;
}

static void test_pen_miss_and_cap(void)
{
    osk_state k;
    int i;

    printf("a pen miss is a no-op and the buffer will not overflow:\n");
    osk_reset(&k);
    CHECK(osk_pen(&k, 2, 2) == 0, "a tap above the grid hits nothing");
    CHECK(osk_text_len(&k) == 0, "and inserts nothing");

    osk_reset(&k);
    for (i = 0; i < OSK_MAX_TEXT + 8; i++) {
        k.cursor = first_char_index(&k);
        osk_activate(&k);
    }
    CHECK(osk_text_len(&k) == OSK_MAX_TEXT, "the buffer caps at OSK_MAX_TEXT");
}

static void test_utf16le_and_bridge(void)
{
    osk_state k;
    uint8_t bytes[2 * OSK_MAX_TEXT];
    size_t need;

    printf("the output is UTF-16LE and the charcode bridge round-trips it:\n");
    osk_reset(&k);
    tap_page(&k, 1);
    tap(&k, 'a');
    tap(&k, 'b');
    need = osk_text_utf16le(&k, bytes, sizeof bytes);
    CHECK(need == 4, "two units -> four bytes");
    CHECK(bytes[0] == 'a' && bytes[1] == 0x00 && bytes[2] == 'b' && bytes[3] == 0x00,
          "little-endian, low byte first");

    {
        uint8_t small[2];
        size_t n = osk_text_utf16le(&k, small, sizeof small);

        CHECK(n == 4 && n > sizeof small,
              "the full size is returned so truncation is visible");
        CHECK(small[0] == 'a' && small[1] == 0x00, "what fits is still written");
    }

    {
        mmo_charcode cc[OSK_MAX_TEXT + 1];
        uint8_t back[2 * OSK_MAX_TEXT];
        mmo_charcode_result r1, r2;

        r1 = mmo_utf16le_to_charcode(bytes, need, cc, sizeof cc / sizeof cc[0]);
        CHECK(r1.unmapped == 0 && r1.written == 2, "both glyphs map into charcode");
        r2 = mmo_charcode_to_utf16le(cc, back, sizeof back);
        CHECK(r2.written == 2 && memcmp(back, bytes, 4) == 0,
              "charcode -> UTF-16LE reproduces the typed bytes");
    }
}

static void test_caret_and_host(void)
{
    osk_state k;
    char s[OSK_MAX_TEXT + 1];

    printf("the host keyboard writes the same buffer, including a mid-line edit:\n");
    osk_reset(&k);
    osk_feed(&k, OPENMMO_TEXT_UNIT, 'h');
    osk_feed(&k, OPENMMO_TEXT_UNIT, 'i');
    CHECK(osk_caret(&k) == 2 && osk_text_len(&k) == 2, "typed units land at the caret");

    osk_feed(&k, OPENMMO_TEXT_LEFT, 0);
    osk_feed(&k, OPENMMO_TEXT_UNIT, 'a');
    text_ascii(&k, s, sizeof s);
    CHECK(strcmp(s, "hai") == 0 && osk_caret(&k) == 2, "LEFT then a unit inserts in the middle");

    osk_feed(&k, OPENMMO_TEXT_HOME, 0);
    osk_feed(&k, OPENMMO_TEXT_DELETE, 0);
    text_ascii(&k, s, sizeof s);
    CHECK(strcmp(s, "ai") == 0 && osk_caret(&k) == 0, "HOME+DELETE drops the first unit");

    osk_feed(&k, OPENMMO_TEXT_END, 0);
    osk_feed(&k, OPENMMO_TEXT_BACKSPACE, 0);
    text_ascii(&k, s, sizeof s);
    CHECK(strcmp(s, "a") == 0, "END+BACKSPACE drops the last unit");

    CHECK(!osk_committed(&k) && !osk_cancelled(&k), "edits do not commit or cancel");
    osk_feed(&k, OPENMMO_TEXT_COMMIT, 0);
    CHECK(osk_committed(&k), "COMMIT commits");
    osk_reset(&k);
    osk_feed(&k, OPENMMO_TEXT_CANCEL, 0);
    CHECK(osk_cancelled(&k) && osk_text_len(&k) == 0, "CANCEL flags an empty abandon");

    osk_reset(&k);
    osk_set_max(&k, 3);
    osk_feed(&k, OPENMMO_TEXT_UNIT, 'a');
    osk_feed(&k, OPENMMO_TEXT_UNIT, 'b');
    osk_feed(&k, OPENMMO_TEXT_UNIT, 'c');
    osk_feed(&k, OPENMMO_TEXT_UNIT, 'd');
    CHECK(osk_text_len(&k) == 3, "a host unit past the cap is ignored");
    osk_reset(&k);
    CHECK(k.max == 3, "reset keeps the cap");
}

int osk_tests_run(void)
{
    failures = 0;
    test_layout();
    test_movement();
    test_typing();
    test_editing_and_commit();
    test_pen_miss_and_cap();
    test_utf16le_and_bridge();
    test_caret_and_host();

    if (failures)
        printf("osk: %d check(s) FAILED\n", failures);
    else
        printf("osk: all checks passed\n");
    return failures;
}
