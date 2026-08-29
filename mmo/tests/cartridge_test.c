/* The slot table, and what a player is told about an image. */
#include <stdio.h>
#include <string.h>

#include "cartridge.h"
#include "imports.h"

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

int cartridge_tests_run(void);

int cartridge_tests_run(void)
{
    const MmoCartridge *c;
    char buf[192];
    int n, i, read = 0;

    failures = 0;
    printf("a cartridge is a slot, and every image gets an answer:\n");

    CHECK(mmo_cartridge_count() > 0, "the registry has rows");

    /* Every row is reachable by its own code and by nothing else's. */
    for (i = 0; i < mmo_cartridge_count(); i++) {
        const MmoCartridge *row = mmo_cartridge_at(i);
        if (!row || mmo_cartridge_by_code(row->code) != row) {
            printf("  FAIL row %d is not found by its own code\n", i);
            failures++;
            break;
        }
        if (row->status == MMO_CART_READ)
            read++;
    }
    CHECK(i == mmo_cartridge_count(), "every row is found by its own code");
    CHECK(read > 0, "at least one cartridge has been read here");

    c = mmo_cartridge_by_code("IPKE");
    CHECK(c && strcmp(c->slot, "heartgold") == 0 && c->status == MMO_CART_READ,
          "IPKE is the heartgold slot and it has been read");
    CHECK(c && mmo_cartridge_serves(c, "pokemon"),
          "heartgold serves pokemon");
    CHECK(c && !mmo_cartridge_serves(c, "map"),
          "heartgold does not serve map");
    /* Whole entries only: a prefix of a kind is not that kind. */
    CHECK(c && !mmo_cartridge_serves(c, "poke"),
          "a prefix of a kind does not answer for it");
    CHECK(mmo_cartridge_refusal("IPKE", buf, sizeof buf) < 0,
          "a read cartridge earns no refusal");

    c = mmo_cartridge_by_code("CPUE");
    CHECK(c && c->status == MMO_CART_HOST, "CPUE is the host image");
    CHECK(c && !mmo_cartridge_serves(c, "pokemon"),
          "the host image serves nothing to a fill");
    n = mmo_cartridge_refusal("CPUE", buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "own image") != NULL,
          "the player's own image is refused as a source, by name");

    /*
     * Diamond was refused as read-but-not-drawn until its sheets were measured to scramble the
     * other way round. It fills now, and the direction it scrambles in is a property of the
     * row rather than something a fill rediscovers.
     */
    c = mmo_cartridge_by_code("ADAE");
    CHECK(c && c->status == MMO_CART_READ && mmo_cartridge_serves(c, "pokemon"),
          "Diamond fills a package, now that its scramble is measured");
    CHECK(mmo_cartridge_refusal("ADAE", buf, sizeof buf) < 0,
          "and earns no refusal, the way any read cartridge does not");
    CHECK(c && c->scramble != 0 && c->scramble != mmo_cartridge_by_code("CPUE")->scramble,
          "and its row says it scrambles the other way from this game's");

    n = mmo_cartridge_refusal("IRBO", buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "Black") != NULL,
          "Black is refused for its layout, naming the slot");

    /*
     * A twin serves what its pair serves, and that is measured rather than assumed: Pearl's
     * pokegra and SoulSilver's three archives were opened here and are byte-identical to
     * Diamond's and HeartGold's.
     */
    c = mmo_cartridge_by_code("APAE");
    CHECK(c && c->status == MMO_CART_READ && mmo_cartridge_serves(c, "pokemon"),
          "Pearl fills a package, the way Diamond does");
    CHECK(c && c->scramble == mmo_cartridge_by_code("ADAE")->scramble,
          "and scrambles the way its twin does, not the way this game does");
    c = mmo_cartridge_by_code("IPGE");
    CHECK(c && c->status == MMO_CART_READ
            && mmo_cartridge_serves(c, "pokemon")
            && mmo_cartridge_serves(c, "item_icon")
            && mmo_cartridge_serves(c, "trainer"),
          "SoulSilver serves all three kinds HeartGold does");
    CHECK(c && c->scramble == mmo_cartridge_by_code("IPKE")->scramble,
          "and scrambles the way its twin does");

    /* The point of the table: a build we have never opened is still ours to
     * name. IRAO is listed unread; IPKD is not listed at all and shares its
     * first three characters with HeartGold. */
    n = mmo_cartridge_refusal("IRAO", buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "White") != NULL
              && strstr(buf, "not a cartridge") == NULL,
          "an unread build names its slot instead of being a stranger");

    c = mmo_cartridge_slot_of("IPKD");
    CHECK(c && strcmp(c->slot, "heartgold") == 0,
          "an unlisted build resolves to the slot its first three name");
    CHECK(mmo_cartridge_by_code("IPKD") == NULL,
          "and it is not a row, so nothing claims it was read");
    n = mmo_cartridge_refusal("IPKD", buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "Heart Gold") != NULL
              && strstr(buf, "german") != NULL,
          "the refusal names the slot and the language the code carries");

    /* A sentence a player reads says the name on the box, not the identifier
     * the shell matches on. */
    for (i = 0; i < mmo_cartridge_count(); i++) {
        const MmoCartridge *row = mmo_cartridge_at(i);
        if (!row->name || row->name[0] == '\0'
                || row->name[0] < 'A' || row->name[0] > 'Z') {
            printf("  FAIL %s has no display name\n", row->code);
            failures++;
            break;
        }
    }
    CHECK(i == mmo_cartridge_count(), "every slot has a name a player reads");

    CHECK(mmo_cartridge_slot_of("ZZZZ") == NULL,
          "a code sharing nothing with a slot resolves to no slot");
    n = mmo_cartridge_refusal("ZZZZ", buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "not a cartridge") != NULL,
          "and that one is told it is not a cartridge we know");

    CHECK(mmo_cartridge_language('E') != NULL
              && strcmp(mmo_cartridge_language('E'), "english") == 0,
          "the fourth character is where the language comes from");
    CHECK(mmo_cartridge_language('!') == NULL,
          "a region character the registry does not define has no language");

    /* A scramble direction is only ever one the engine's own tool defines, or
     * zero for an image nobody measured. A stray value would send a fill
     * through a re-encode nothing can undo. */
    for (i = 0; i < mmo_cartridge_count(); i++) {
        int m = mmo_cartridge_at(i)->scramble;
        if (m != 0 && m != 1 && m != 2) {
            printf("  FAIL %s claims scramble mode %d\n",
                   mmo_cartridge_at(i)->code, m);
            failures++;
            break;
        }
    }
    CHECK(i == mmo_cartridge_count(),
          "every scramble mode is one the engine's own tool defines");

    /* A refusal is a sentence a screen has to hold. Nothing here may need
     * more room than the region refusals already do. */
    for (i = 0; i < mmo_cartridge_count(); i++) {
        const MmoCartridge *row = mmo_cartridge_at(i);
        n = mmo_cartridge_refusal(row->code, buf, sizeof buf);
        if (row->status != MMO_CART_READ && (n <= 0 || n >= 128)) {
            printf("  FAIL %s has no refusal that fits in 128 bytes (%d)\n",
                   row->code, n);
            failures++;
            break;
        }
    }
    CHECK(i == mmo_cartridge_count(),
          "every refusable row has a refusal a screen can hold");

    /* A caller with no room gets nothing rather than a truncated sentence it
     * might print as if it were whole. */
    CHECK(mmo_cartridge_refusal("ZZZZ", NULL, 0) < 0,
          "a refusal with nowhere to go is refused, not written");

    /* What an unfilled package costs, over a struct rather than a directory:
     * the sentence has to name the cartridge and the content, because a count
     * of missing members is not something a player can act on. */
    {
        MmoImportPackage pkg;
        memset(&pkg, 0, sizeof pkg);
        snprintf(pkg.code, sizeof pkg.code, "IPKE");
        pkg.lines = 4;
        pkg.kinds = 2;
        snprintf(pkg.kind[0], sizeof pkg.kind[0], "pokemon");
        pkg.per_kind[0] = 3;
        snprintf(pkg.kind[1], sizeof pkg.kind[1], "item_icon");
        pkg.per_kind[1] = 1;

        n = mmo_imports_shortfall(&pkg, NULL, buf, sizeof buf);
        CHECK(n > 0 && strstr(buf, "Heart Gold") != NULL,
              "an unfilled package names the cartridge that would fill it");
        CHECK(n > 0 && strstr(buf, "3 species") != NULL
                  && strstr(buf, "1 item icon") != NULL,
              "and what the lines were going to bring, in a player's words");
        CHECK(strstr(buf, "pokemons") == NULL,
              "not by pluralising the recipe's own identifier");

        pkg.filled = 1;
        CHECK(mmo_imports_shortfall(&pkg, NULL, buf, sizeof buf) < 0,
              "a filled package costs nothing and says nothing");

        /* A player whose cartridge is already in the folder is told what to
         * do with it, not what they are missing. */
        pkg.filled = 0;
        n = mmo_imports_shortfall(&pkg, "mygame.nds", buf, sizeof buf);
        CHECK(n > 0 && strstr(buf, "mygame.nds") != NULL
                  && strstr(buf, "Import it") != NULL,
              "a cartridge already in the folder turns the gap into a step");

        /* A recipe naming two cartridges has no single answer, and says so
         * rather than naming whichever line came first. */
        pkg.filled = 0;
        pkg.code[0] = '\0';
        n = mmo_imports_shortfall(&pkg, NULL, buf, sizeof buf);
        CHECK(n > 0 && strstr(buf, "nobody has added") != NULL,
              "a package with no single cartridge still says what is missing");
    }

    return failures;
}
