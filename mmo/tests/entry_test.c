/* Which text-entry path a surface uses. */
#include "entry.h"
#include "game.h"
#include "text_channel.h"

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

static void test_paths(void)
{
    printf("a name, a chat line and a search box are the field:\n");
    CHECK(mmo_entry_path_for(MMO_ENTRY_NAME) == MMO_ENTRY_FIELD,
          "a creator name is the field");
    CHECK(mmo_entry_path_for(MMO_ENTRY_CHAT) == MMO_ENTRY_FIELD,
          "a chat line is the re-hosted engine keyboard");
    CHECK(mmo_entry_path_for(MMO_ENTRY_SEARCH) == MMO_ENTRY_FIELD,
          "a search box is the field");
    CHECK(mmo_entry_path_for(MMO_ENTRY_ENGINE_NAME) == MMO_ENTRY_NAMING_SCREEN,
          "an engine-native name stays on the naming screen");
}

static void test_caps(void)
{
    printf("each surface stops where its store stops:\n");
    CHECK(mmo_entry_max_units(MMO_ENTRY_NAME) == MMO_CHAR_NAME_MAX,
          "a creator name is the wire's 32");
    CHECK(MMO_CHAR_NAME_MAX == 32, "CreateCharacter is still VARCHAR(32)");
    CHECK(mmo_entry_max_units(MMO_ENTRY_CHAT) == OSK_MAX_TEXT,
          "a chat line is the field's own cap");
    CHECK(mmo_entry_max_units(MMO_ENTRY_SEARCH) == OSK_MAX_TEXT,
          "a search box is the field's own cap");
    CHECK(mmo_entry_max_units(MMO_ENTRY_ENGINE_NAME) == MMO_ENTRY_NAMING_MAX,
          "an engine name is the naming screen's 19");
    CHECK(MMO_ENTRY_NAMING_MAX == 19, "nameInputRaw[20] leaves one slot for EOS");
}

static void test_hands(void)
{
    printf("the field wants both hands; the naming screen wants neither:\n");
    CHECK(mmo_entry_wants_host(MMO_ENTRY_NAME) && mmo_entry_wants_osk(MMO_ENTRY_NAME),
          "a creator name takes the host keyboard and the grid");
    CHECK(mmo_entry_wants_host(MMO_ENTRY_CHAT)
              && mmo_entry_wants_osk(MMO_ENTRY_CHAT),
          "a chat line takes the host keyboard and the grid");
    CHECK(!mmo_entry_wants_host(MMO_ENTRY_ENGINE_NAME)
              && !mmo_entry_wants_osk(MMO_ENTRY_ENGINE_NAME),
          "the naming screen reads the pad itself");
}

static void test_open(void)
{
    osk_state k;

    printf("opening a field caps it; opening a naming-screen surface refuses:\n");
    memset(&k, 0, sizeof k);
    CHECK(mmo_entry_open(&k, MMO_ENTRY_NAME) == 1, "a creator name opens");
    CHECK(k.max == MMO_CHAR_NAME_MAX, "and is capped at 32");

    /* Fill past 32 via the host hand: the 33rd unit must not land. */
    {
        int i;

        for (i = 0; i < 40; i++)
            osk_feed(&k, OPENMMO_TEXT_UNIT, (unsigned)('a' + (i % 26)));
        CHECK(osk_text_len(&k) == MMO_CHAR_NAME_MAX, "a name stops at 32");
    }

    memset(&k, 0xaa, sizeof k);
    CHECK(mmo_entry_open(&k, MMO_ENTRY_ENGINE_NAME) == 0,
          "an engine name is refused");
    {
        unsigned char *p = (unsigned char *)&k;
        size_t i;
        int dirty = 0;

        for (i = 0; i < sizeof k; i++) {
            if (p[i] != 0xaa) {
                dirty = 1;
                break;
            }
        }
        CHECK(!dirty, "and the widget is not touched");
    }
}

int entry_tests_run(void)
{
    failures = 0;
    test_paths();
    test_caps();
    test_hands();
    test_open();

    if (failures)
        printf("entry: %d check(s) FAILED\n", failures);
    else
        printf("entry: all checks passed\n");
    return failures;
}
