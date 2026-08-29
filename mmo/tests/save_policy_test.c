/* The save-block ownership table. */
#include <stdio.h>

#include "save_policy.h"

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

/* The engine's SAVE_TABLE_ENTRY_MAX, read out of
 * include/constants/savedata/save_table.h. This is the external oracle: if the
 * engine grows or shrinks its save table, this number moves and the check below
 * catches that our mirror did not. */
#define ENGINE_SAVE_TABLE_ENTRY_MAX 38

/* The blocks Phase 1 named as state the client legitimately keeps. Everything
 * else must not be LOCAL. */
static int is_expected_local(int id)
{
    return id == MMO_SAVE_ENTRY_SYSTEM || id == MMO_SAVE_ENTRY_POKETCH ||
        id == MMO_SAVE_ENTRY_CHATOT;
}

int save_policy_tests_run(void)
{
    int id;
    int local = 0, server = 0, service = 0;

    failures = 0;
    printf("save-policy block ownership:\n");

    printf("the mirror still matches the engine's save table:\n");
    CHECK(MMO_SAVE_ENTRY_MAX == ENGINE_SAVE_TABLE_ENTRY_MAX,
        "enum count equals engine SAVE_TABLE_ENTRY_MAX");

    printf("every block classifies, and only the named blocks are local:\n");
    for (id = 0; id < MMO_SAVE_ENTRY_MAX; id++) {
        mmo_save_class c = mmo_save_class_of(id);
        int ok = (c == MMO_SAVE_LOCAL || c == MMO_SAVE_SERVER ||
            c == MMO_SAVE_SERVICE);
        if (!ok) {
            printf("  FAIL %s has no valid class\n", mmo_save_entry_name(id));
            failures++;
        }
        switch (c) {
        case MMO_SAVE_LOCAL:
            local++;
            break;
        case MMO_SAVE_SERVER:
            server++;
            break;
        case MMO_SAVE_SERVICE:
            service++;
            break;
        }
        /* The load-bearing invariant: LOCAL is exactly the Phase-1 set. */
        if (is_expected_local(id)) {
            CHECK(c == MMO_SAVE_LOCAL, mmo_save_entry_name(id));
        } else {
            CHECK(c != MMO_SAVE_LOCAL, mmo_save_entry_name(id));
        }
    }
    CHECK(local + server + service == MMO_SAVE_ENTRY_MAX,
        "the three classes partition all blocks");
    CHECK(local == 3, "exactly three blocks are client-local");

    printf("the derived predicates agree with the class:\n");
    for (id = 0; id < MMO_SAVE_ENTRY_MAX; id++) {
        mmo_save_class c = mmo_save_class_of(id);
        CHECK(mmo_save_persist_locally(id) == (c == MMO_SAVE_LOCAL),
            "persist-locally iff local");
        CHECK(mmo_save_seat_from_server(id) == (c == MMO_SAVE_SERVER),
            "seat-from-server iff server");
    }

    printf("named server-owned and service blocks land where Phase 1 put them:\n");
    CHECK(mmo_save_class_of(MMO_SAVE_ENTRY_PLAYER) == MMO_SAVE_SERVER,
        "PLAYER (money/badges/name) is server-owned");
    CHECK(mmo_save_class_of(MMO_SAVE_ENTRY_PARTY) == MMO_SAVE_SERVER,
        "PARTY is server-owned");
    CHECK(mmo_save_class_of(MMO_SAVE_ENTRY_BAG) == MMO_SAVE_SERVER,
        "BAG is server-owned");
    CHECK(mmo_save_class_of(MMO_SAVE_ENTRY_VARS_FLAGS) == MMO_SAVE_SERVER,
        "VARS_FLAGS (story progression) is server-owned");
    CHECK(mmo_save_class_of(MMO_SAVE_ENTRY_FIELD_PLAYER_STATE) == MMO_SAVE_SERVER,
        "FIELD_PLAYER_STATE (position) is server-owned");
    CHECK(mmo_save_class_of(MMO_SAVE_ENTRY_PC_BOXES) == MMO_SAVE_SERVER,
        "PC_BOXES is server-owned");
    CHECK(mmo_save_class_of(MMO_SAVE_ENTRY_GLOBAL_TRADE) == MMO_SAVE_SERVICE,
        "GLOBAL_TRADE (GTS) is a networked service");
    CHECK(mmo_save_class_of(MMO_SAVE_ENTRY_MYSTERY_GIFT) == MMO_SAVE_SERVICE,
        "MYSTERY_GIFT is a networked service");
    CHECK(mmo_save_class_of(MMO_SAVE_ENTRY_WIFI_LIST) == MMO_SAVE_SERVICE,
        "WIFI_LIST is a networked service");

    printf("an out-of-range id fails safe, never persisted:\n");
    CHECK(!mmo_save_id_valid(-1) && !mmo_save_id_valid(MMO_SAVE_ENTRY_MAX),
        "ids outside the table are rejected");
    CHECK(!mmo_save_persist_locally(MMO_SAVE_ENTRY_MAX),
        "an unknown block is not persisted locally");
    CHECK(!mmo_save_persist_locally(-1),
        "a negative id is not persisted locally");

    if (failures) {
        printf("save-policy: %d check(s) FAILED\n", failures);
    } else {
        printf("save-policy: all checks passed\n");
    }
    return failures;
}
