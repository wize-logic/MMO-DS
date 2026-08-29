/* The server<->engine id translation. */
#include <stdio.h>

#include "idmap.h"

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

/*
 * Capture the call's result and its reason string in that order, the reason is an out-
 * parameter of the call, so it must be read *after* the call returns, not evaluated as a
 * sibling argument (argument evaluation order is unsequenced).
 */
#define CK_MAP(expr, want, msg)                                                 \
    do {                                                                        \
        const char *why_ = "unset";                                            \
        u16 got_ = (expr);                                                      \
        CHECK(got_ == (u16)(want) && why_ == 0, msg);                          \
    } while (0)

#define CK_TRAP(expr, msg)                                                      \
    do {                                                                        \
        const char *why_ = "unset";                                            \
        u16 got_ = (expr);                                                      \
        CHECK(got_ == MMO_ID_NONE && why_ != 0, msg);                          \
    } while (0)

int idmap_tests_run(void)
{
    failures = 0;

    printf("species are the National Dex number, identity within what the engine draws:\n");
    CK_MAP(mmo_id_species_from_server(1, &why_), 1, "Bulbasaur (#1) maps to itself");
    CK_MAP(mmo_id_species_from_server(151, &why_), 151, "Mew (#151) maps to itself");
    CK_MAP(mmo_id_species_from_server(493, &why_), 493, "Arceus (#493) maps to itself");
    CK_TRAP(mmo_id_species_from_server(0, &why_), "species 0 traps");
    CK_TRAP(mmo_id_species_from_server(494, &why_), "a dex number past Arceus traps");
    CK_MAP(mmo_id_species_to_server(386, &why_), 386, "engine->server species is identity too");
    CK_TRAP(mmo_id_species_to_server(0, &why_), "engine species 0 traps back");

    printf("moves are one numbering across both worlds, up to the last one the engine has:\n");
    CK_MAP(mmo_id_move_from_server(1, &why_), 1, "Pound (#1) maps to itself");
    CK_MAP(mmo_id_move_from_server(354, &why_), 354, "Psycho Boost (#354, last Gen-3) maps");
    CK_MAP(mmo_id_move_from_server(458, &why_), 458, "Double Hit (#458) maps, as the server has it");
    CK_MAP(mmo_id_move_from_server(467, &why_), 467, "Shadow Force (#467, the last) maps");
    CK_TRAP(mmo_id_move_from_server(0, &why_), "move 0 traps");
    CK_TRAP(mmo_id_move_from_server(468, &why_), "a server move past the last one traps");
    CK_MAP(mmo_id_move_to_server(354, &why_), 354, "engine move sends as itself");
    CK_MAP(mmo_id_move_to_server(459, &why_), 459, "Roar of Time sends too, now the server has it");
    CK_TRAP(mmo_id_move_to_server(468, &why_), "a move past the last platinum move traps");

    printf("items share the Gen-5 numbering; the wire id is region*1000 + that index:\n");
    CK_MAP(mmo_id_item_from_server(5001, &why_), 1, "Master Ball (5001) -> engine 1");
    CK_MAP(mmo_id_item_from_server(5013, &why_), 13, "5013 is a Dusk Ball on both sides");
    CK_MAP(mmo_id_item_from_server(5017, &why_), 17, "Potion is 5017, and engine 17");
    CHECK(17 != 13, "Potion's engine id is not its gba id (13 is a Dusk Ball)");
    CK_MAP(mmo_id_item_from_server(5018, &why_), 18, "Antidote (5018) -> engine 18");
    CK_MAP(mmo_id_item_from_server(5137, &why_), 137,
        "a mail slot maps though the two worlds fill it with different mail");
    CK_MAP(mmo_id_item_from_server(5467, &why_), 467, "the last item the engine draws maps");
    CK_TRAP(mmo_id_item_from_server(5426, &why_), "an HM slot the server's table blanked traps");
    CK_TRAP(mmo_id_item_from_server(5113, &why_), "an engine id with no item behind it traps");
    CK_TRAP(mmo_id_item_from_server(5468, &why_), "an item past what the engine draws traps");
    CK_TRAP(mmo_id_item_from_server(5000, &why_), "index 0 (no item) traps");
    CK_TRAP(mmo_id_item_from_server(13, &why_), "an item id with no region block traps");
    CK_TRAP(mmo_id_item_from_server(6013, &why_), "an item outside region 5 traps");

    printf("engine->server items are the same shared set, read the other way:\n");
    CK_MAP(mmo_id_item_to_server(17, &why_), 5017, "engine Potion (17) -> wire 5017");
    CK_MAP(mmo_id_item_to_server(1, &why_), 5001, "engine Master Ball (1) -> wire 5001");
    CK_TRAP(mmo_id_item_to_server(0, &why_), "engine item 0 traps back");
    CK_TRAP(mmo_id_item_to_server(426, &why_), "an engine HM the server has no item for traps");
    CK_TRAP(mmo_id_item_to_server(9999, &why_), "an engine item with no server id traps");

    /*
     * The oracle here is the engine's own build output, not this table: the generated enum
     * gives TEXT_BANK_TWINLEAF_TOWN 554 and that bank's generated header gives
     * TwinleafTown_Text_BigThud 0, so the pair (554, 0) has to come back out of the id the
     * server mints for it.
     */
    printf("a text id splits into the bank and message the engine's loader takes:\n");
    {
        u16 bank_ = 0xFFFF, entry_ = 0xFFFF;
        u32 wire_ = 0;
        const char *why_ = "unset";

        CHECK(mmo_id_text_from_server(0x322A0000u, &bank_, &entry_, &why_) == 0 && bank_ == 554
                && entry_ == 0 && why_ == 0,
            "Twinleaf Town's first line is bank 554, message 0");
        CHECK(mmo_id_text_from_server(0x322A000Eu, &bank_, &entry_, &why_) == 0 && bank_ == 554
                && entry_ == 14,
            "its last line (of 15) resolves");
        CHECK(mmo_id_text_from_server(0x322A000Fu, &bank_, &entry_, &why_) == -1 && why_ != 0,
            "one message past the end of that bank traps");
        CHECK(mmo_id_text_from_server(0x32D40000u, &bank_, &entry_, &why_) == -1 && why_ != 0,
            "bank 724, one past the end of the archive, traps");
        CHECK(mmo_id_text_from_server(0x30000000u, &bank_, &entry_, &why_) == -1 && why_ != 0,
            "a bank the ROM build assembles traps rather than guessing its length");
        CHECK(mmo_id_text_from_server(0x122A0000u, &bank_, &entry_, &why_) == -1 && why_ != 0,
            "a Hoenn text id is not this engine's string and traps");

        CHECK(mmo_id_text_to_server(554, 0, &wire_, &why_) == 0 && wire_ == 0x322A0000u
                && why_ == 0,
            "the same pair sends back as the id it came from");
        CHECK(mmo_id_text_to_server(554, 15, &wire_, &why_) == -1 && why_ != 0,
            "a message the bank does not have traps on the way out");
        CHECK(mmo_id_text_to_server(0, 0, &wire_, &why_) == -1 && why_ != 0,
            "so does a bank whose length the engine's source never states");
    }

    /* The oracle is the server's own split (NewGameStart / LoadEntity) and
     * the engine's map_headers.txt: Twinleaf bedroom is header 415, town
     * is 411, the last real header is 592. Header 0 is a real map, so a
     * trap is -1, not MMO_ID_NONE. */
    printf("a DS map id is the platinum header split across bank and map:\n");
    {
        const char *why_ = "unset";

        CHECK(mmo_id_map_header_from_server(3, 1, 159, &why_) == 415 && why_ == 0,
            "Twinleaf bedroom (bank 1 map 159) is header 415");
        CHECK(mmo_id_map_header_from_server(3, 1, 155, &why_) == 411 && why_ == 0,
            "Twinleaf Town (bank 1 map 155) is header 411");
        CHECK(mmo_id_map_header_from_server(3, 0, 3, &why_) == 3 && why_ == 0,
            "header 3 is bank 0 map 3");
        CHECK(mmo_id_map_header_from_server(3, 0, 0, &why_) == 0 && why_ == 0,
            "header 0 is a real map, not a trap");
        CHECK(mmo_id_map_header_from_server(3, 2, 80, &why_) == 592 && why_ == 0,
            "the last real header (592) maps");
        /*
         * Past the last header the image carries is no longer a trap: a runtime package
         * appends headers (the hub does, and so does a ported map) and the game's own getters
         * answer for them, so refusing here would refuse a map the engine can draw.
         */
        CHECK(mmo_id_map_header_from_server(3, 2, 81, &why_) == 593 && why_ == 0,
            "a header past the image's own last one is carried, not refused");
        CHECK(mmo_id_map_header_from_server(3, 2, 82, &why_) == 594 && why_ == 0,
            "and an appended map's header (594) addresses as bank 2 map 82");
        CHECK(mmo_id_map_header_from_server(3, 256, 0, &why_) == -1 && why_ != 0,
            "what a byte cannot hold is still refused");
        CHECK(mmo_id_map_header_from_server(0, 4, 1, &why_) == -1 && why_ != 0,
            "a Kanto bank/map is not a platinum header and traps");
        CHECK(mmo_id_map_header_from_server(3, -1, 0, &why_) == -1 && why_ != 0,
            "a bank that is not a byte traps");
    }

    if (failures)
        printf("idmap: %d check(s) FAILED\n", failures);
    else
        printf("idmap: all checks passed\n");
    return failures;
}
