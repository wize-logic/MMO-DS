/* The type tables, bounds-checked. */
#include "display.h"

#include "display_data.gen.h"
#include "display_overlay.gen.h"
#include "display_types.gen.h"

static int trap(const char **why, const char *msg)
{
    if (why)
        *why = msg;
    return MMO_TYPE_INVALID;
}

static int ok(const char **why, int value)
{
    if (why)
        *why = 0;
    return value;
}

static int in_range(int type)
{
    return type >= 0 && type < MMO_TYPE_COUNT;
}

/* The one message worth spelling out, because the id it refuses is the one a
 * reader will assume was an oversight. It is not: see display.h. */
static const char *const NO_EIGHTEENTH =
    "type: id past the eighteen slots this tree defines, the modern 18th type "
    "(Fairy) is in neither the engine, the official client nor the server's tables, "
    "so there is nothing here to draw it from";

int mmo_display_type_count(void)
{
    return MMO_TYPE_COUNT;
}

const char *mmo_display_type_name(int type, const char **why)
{
    if (!in_range(type)) {
        trap(why, NO_EIGHTEENTH);
        return 0;
    }
    ok(why, type);
    return MMO_TYPE_NAME[type];
}

int mmo_display_type_matchup(int attack, int defend, const char **why)
{
    if (!in_range(attack) || !in_range(defend))
        return trap(why, NO_EIGHTEENTH);
    return ok(why, MMO_TYPE_MATCHUP[attack][defend]);
}

int mmo_display_type_matchup_dual(int attack, int defend1, int defend2, const char **why)
{
    int first, second;

    if (!in_range(attack) || !in_range(defend1))
        return trap(why, NO_EIGHTEENTH);
    if (defend2 != MMO_TYPE_NO_TYPE && !in_range(defend2))
        return trap(why, NO_EIGHTEENTH);

    first = MMO_TYPE_MATCHUP[attack][defend1];
    if (defend2 == MMO_TYPE_NO_TYPE || defend2 == defend1)
        return ok(why, first);

    /* Both factors are tenths, so the product carries one factor of ten too
     * many; dividing it back out is exact for every value the chart holds
     * (0, 5, 10, 20, every product is a multiple of ten). */
    second = MMO_TYPE_MATCHUP[attack][defend2];
    return ok(why, first * second / MMO_TYPE_NEUTRAL);
}

int mmo_display_type_foresight_clears(int attack, int defend)
{
    int i;

    for (i = 0; i < MMO_TYPE_FORESIGHT_IMMUNITY_COUNT; i++) {
        if (MMO_TYPE_FORESIGHT_IMMUNITY[i][0] == attack
            && MMO_TYPE_FORESIGHT_IMMUNITY[i][1] == defend)
            return 1;
    }
    return 0;
}

int mmo_display_type_from_wire(int wire, const char **why)
{
    if (wire == MMO_TYPE_WIRE_MYSTERY)
        return ok(why, MMO_TYPE_MYSTERY);
    if (wire == MMO_TYPE_WIRE_NONE)
        return ok(why, MMO_TYPE_NO_TYPE);
    if (wire < 0 || wire >= MMO_TYPE_WIRE_NONE)
        return trap(why, NO_EIGHTEENTH);
    /* Below `???`'s slot the two numberings agree; above it the compact one is
     * short by exactly the slot `???` occupies. */
    return ok(why, wire < MMO_TYPE_MYSTERY ? wire : wire + 1);
}

int mmo_display_type_to_wire(int type, const char **why)
{
    if (type == MMO_TYPE_NO_TYPE)
        return ok(why, MMO_TYPE_WIRE_NONE);
    if (!in_range(type))
        return trap(why, NO_EIGHTEENTH);
    if (type == MMO_TYPE_MYSTERY)
        return ok(why, MMO_TYPE_WIRE_MYSTERY);
    return ok(why, type < MMO_TYPE_MYSTERY ? type : type - 1);
}

int mmo_display_effect_of(int tenths)
{
    if (tenths <= 0)
        return MMO_EFFECT_IMMUNE;
    if (tenths < MMO_TYPE_NEUTRAL)
        return MMO_EFFECT_RESISTED;
    if (tenths == MMO_TYPE_NEUTRAL)
        return MMO_EFFECT_NEUTRAL;
    return MMO_EFFECT_SUPER;
}

static const char *overlay_find(int id)
{
    int lo = 0, hi = MMO_DISPLAY_OVERLAY_COUNT - 1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int got = MMO_DISPLAY_OVERLAY[mid].id;

        if (got == id)
            return MMO_DISPLAY_OVERLAY[mid].text;
        if (got < id)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

static const char *NO_STRING =
    "string: id is not in the official client overlay and is not a ROM-bank slot "
    "this table fills";
static const char *NO_ABILITY =
    "ability: id has no name in the engine's 124 or the official client overlay";
static const char *NO_MOVE =
    "move: id has no name in the engine's 467 or the official client overlay";
static const char *NO_MOVE_TYPE =
    "move: id has no type in the engine or the server's tables";
static const char *NO_SPECIES =
    "species: id has no typing in the engine or the server's tables";

static const char *bank_text(int id)
{
    if (id >= MMO_STR_ABILITY_NAME && id < MMO_STR_ABILITY_NAME + MMO_DISPLAY_ABILITY_COUNT)
        return MMO_DISPLAY_ABILITY_NAME[id - MMO_STR_ABILITY_NAME];
    if (id >= MMO_STR_ABILITY_DESC && id < MMO_STR_ABILITY_DESC + MMO_DISPLAY_ABILITY_COUNT)
        return MMO_DISPLAY_ABILITY_DESC[id - MMO_STR_ABILITY_DESC];
    if (id >= MMO_STR_MOVE_NAME && id < MMO_STR_MOVE_NAME + MMO_DISPLAY_MOVE_COUNT)
        return MMO_DISPLAY_MOVE_NAME[id - MMO_STR_MOVE_NAME];
    if (id >= MMO_STR_MOVE_DESC && id < MMO_STR_MOVE_DESC + MMO_DISPLAY_MOVE_COUNT)
        return MMO_DISPLAY_MOVE_DESC[id - MMO_STR_MOVE_DESC];
    if (id >= MMO_STR_TYPE_NAME && id < MMO_STR_TYPE_NAME + MMO_TYPE_COUNT)
        return MMO_TYPE_NAME[id - MMO_STR_TYPE_NAME];
    return 0;
}

const char *mmo_display_string(int id, const char **why)
{
    const char *text;

    text = overlay_find(id);
    if (text) {
        ok(why, 0);
        return text;
    }
    text = bank_text(id);
    if (text) {
        ok(why, 0);
        return text;
    }
    trap(why, NO_STRING);
    return 0;
}

const char *mmo_display_ability_name(int id, const char **why)
{
    const char *text;

    if (id < 0) {
        trap(why, NO_ABILITY);
        return 0;
    }
    text = mmo_display_string(MMO_STR_ABILITY_NAME + id, why);
    if (text)
        return text;
    trap(why, NO_ABILITY);
    return 0;
}

const char *mmo_display_ability_desc(int id, const char **why)
{
    const char *text;

    if (id < 0) {
        trap(why, NO_ABILITY);
        return 0;
    }
    text = mmo_display_string(MMO_STR_ABILITY_DESC + id, why);
    if (text)
        return text;
    trap(why, NO_ABILITY);
    return 0;
}

const char *mmo_display_move_name(int id, const char **why)
{
    const char *text;

    if (id <= 0) {
        trap(why, NO_MOVE);
        return 0;
    }
    text = mmo_display_string(MMO_STR_MOVE_NAME + id, why);
    if (text)
        return text;
    trap(why, NO_MOVE);
    return 0;
}

const char *mmo_display_move_desc(int id, const char **why)
{
    const char *text;

    if (id <= 0) {
        trap(why, NO_MOVE);
        return 0;
    }
    text = mmo_display_string(MMO_STR_MOVE_DESC + id, why);
    if (text)
        return text;
    trap(why, NO_MOVE);
    return 0;
}

int mmo_display_move_type(int id, const char **why)
{
    if (id <= 0 || id >= MMO_DISPLAY_MOVE_COUNT)
        return trap(why, NO_MOVE_TYPE);
    return ok(why, MMO_DISPLAY_MOVE_TYPE[id]);
}

int mmo_display_species_typing(int species, int *type1, int *type2, const char **why)
{
    if (species <= 0 || species >= MMO_DISPLAY_SPECIES_COUNT
        || MMO_DISPLAY_SPECIES_TYPE1[species] < 0)
        return trap(why, NO_SPECIES);
    if (type1)
        *type1 = MMO_DISPLAY_SPECIES_TYPE1[species];
    if (type2)
        *type2 = MMO_DISPLAY_SPECIES_TYPE2[species];
    return ok(why, 0);
}
