/* Where a fill put the species this game did not ship. */
#include "species_port.h"

#include "species_port.gen.h"

/* The generated bases and the ones a caller compiles against are two spellings
 * of one number, and the whole point of emitting them from the fill tool is
 * that they stay equal. This is that. */
typedef char species_port_species_base_agrees[
    (MMO_PORTED_SPECIES_BASE == MMO_PORTED_SPECIES_ROM_MEMBERS) ? 1 : -1];
typedef char species_port_icon_base_agrees[
    (MMO_PORTED_ICON_BASE == MMO_PORTED_ICON_ROM_MEMBERS) ? 1 : -1];
typedef char species_port_run_agrees[
    ((int)sizeof MMO_PORTED_ICON_PALETTE
     == MMO_PORTED_LAST - MMO_PORTED_FIRST + 1) ? 1 : -1];

static int g_members = MMO_PORTED_SPECIES_ROM_MEMBERS;

static const char *const NOT_PORTED =
    "species: id is not one a fill appends, this game answers for everything it "
    "shipped with, 494 and 495 are its eggs, and nothing is appended past the "
    "two engine-side ids Victini and Snivy live at";

static const char *const NOT_FILLED =
    "species: no fill has put this species in the archives, the table knows "
    "which member it would be and the live archive does not reach it, which is a "
    "package that was never filled rather than a species that cannot exist";

static const char *const NO_OUT =
    "species: no output slot given";

static int trap(const char **why, const char *msg)
{
    if (why)
        *why = msg;
    return -1;
}

static int ok(const char **why)
{
    if (why)
        *why = 0;
    return 0;
}

int mmo_species_port_members(void)
{
    return g_members;
}

void mmo_species_port_set_members(int members)
{
    /* Never below the image's own count: a live count that came back short
     * would take away species this game ships with. */
    if (members < MMO_PORTED_SPECIES_ROM_MEMBERS)
        members = MMO_PORTED_SPECIES_ROM_MEMBERS;
    g_members = members;
}

int mmo_species_port_max(void)
{
    int filled = g_members - MMO_PORTED_SPECIES_BASE;

    if (filled <= 0)
        return MMO_PORTED_FIRST - 1;
    if (filled > MMO_PORTED_LAST - MMO_PORTED_FIRST + 1)
        return MMO_PORTED_LAST;
    return MMO_PORTED_FIRST + filled - 1;
}

int mmo_species_port_engine_id(int wire)
{
    if (wire == MMO_PORTED_EGG_ID)
        return MMO_PORTED_VICTINI_ENGINE_ID;
    if (wire == MMO_PORTED_BAD_EGG_ID)
        return MMO_PORTED_SNIVY_ENGINE_ID;
    return wire;
}

int mmo_species_port_wire_id(int engine)
{
    if (engine == MMO_PORTED_VICTINI_ENGINE_ID)
        return MMO_PORTED_EGG_ID;
    if (engine == MMO_PORTED_SNIVY_ENGINE_ID)
        return MMO_PORTED_BAD_EGG_ID;
    return engine;
}

/* Where a species sits in the appended run: the fill writes Black's order,
 * Victini first, so the two that live past Genesect inside the engine are
 * still the first two members. 494 and 495 themselves are the eggs and are
 * not ported. */
static int fill_index(int species)
{
    if (species == MMO_PORTED_VICTINI_ENGINE_ID || species == MMO_PORTED_SNIVY_ENGINE_ID)
        return species - MMO_PORTED_VICTINI_ENGINE_ID;
    if (species > MMO_PORTED_BAD_EGG_ID && species <= MMO_PORTED_LAST)
        return species - MMO_PORTED_FIRST;
    return -1;
}

int mmo_species_port_is_ported(int species)
{
    return fill_index(species) >= 0;
}


/* The one bound every locator below asks, so a fill is served or refused in one
 * place rather than three that could disagree. */
static int served(int species, const char **why)
{
    int index = fill_index(species);

    if (index < 0)
        return trap(why, NOT_PORTED);
    if (index >= g_members - MMO_PORTED_SPECIES_BASE)
        return trap(why, NOT_FILLED);
    return ok(why);
}

/*
 * Whether every table read this build makes for engine id `engine` stays inside what it holds:
 * the game's own species always, a ported one only once the fill has put it there. idmap.h's
 * range check says a number is a species; this says this build can hold one.
 */
int mmo_species_port_live(int engine)
{
    if (engine < 1)
        return 0;
    if (fill_index(engine) >= 0)
        return served(engine, 0) == 0;
    /* The image's own run, eggs included; forms live behind these ids. */
    return engine <= MMO_PORTED_BAD_EGG_ID;
}

int mmo_species_port_member(int species, int *member, const char **why)
{
    if (member == 0)
        return trap(why, NO_OUT);
    if (served(species, why) != 0)
        return -1;
    *member = MMO_PORTED_SPECIES_BASE + fill_index(species);
    return 0;
}

int mmo_species_port_icon(int species, int *member, int *palette, const char **why)
{
    if (member == 0 || palette == 0)
        return trap(why, NO_OUT);
    if (served(species, why) != 0)
        return -1;
    *member = MMO_PORTED_ICON_BASE + fill_index(species);
    *palette = MMO_PORTED_ICON_PALETTE[fill_index(species)];
    return 0;
}

int mmo_species_port_name(int species, int *entry, const char **why)
{
    if (entry == 0)
        return trap(why, NO_OUT);
    if (served(species, why) != 0)
        return -1;
    *entry = MMO_PORTED_NAME_BASE + fill_index(species);
    return 0;
}
