#ifndef OPENMMO_DISPLAY_H
#define OPENMMO_DISPLAY_H
/* The tables a screen reads, as opposed to the rules a server runs. */
#include "mmo.h"

/*
 * The tables themselves are src/display_types.gen.h, which this deliberately does not pull in:
 * a caller wants the answers below, not eighteen rows of multipliers in its translation unit.
 */

/* Never a type id. Returned on a trap, alongside the reason. */
#define MMO_TYPE_INVALID (-1)

/* Not a trap: the official client's wire numbering has a value for "this field
 * holds no type", which the engine, where every species carries two type
 * slots and a mono-typed one repeats itself, has no id for. A caller that
 * meets this draws nothing, it does not fail. */
#define MMO_TYPE_NO_TYPE (-2)

/*
 * The official client numbers types twice: once as the engine does (its own enum ordinal, which
 * is what indexes its effectiveness rows) and once compactly, with `???` pulled out to -1 so
 * the seventeen real types are 0..16 and NONE takes 17.
 */
#define MMO_TYPE_WIRE_MYSTERY (-1) /* the compact id the official client gives `???` */
#define MMO_TYPE_WIRE_NONE    17   /* the compact id the official client gives "no type" */

int mmo_display_type_count(void);

/* The engine's own on-screen spelling, or a trap. Uppercase, as the engine's
 * text bank stores it; the ??? slot is spelled "???". */
const char *mmo_display_type_name(int type, const char **why);

/* The multiplier in tenths: 0 immune, 5 resisted, 10 neutral, 20 super
 * effective. Traps on either id. This is the chart and only the chart, 
 * abilities, items, weather and Foresight are the server's rules. */
int mmo_display_type_matchup(int attack, int defend, const char **why);

/* The same against a two-type defender, multiplied out and still in tenths, so
 * 40 is a quadruple and 0 an immunity. Pass MMO_TYPE_NO_TYPE for `defend2` when
 * the defender has one type; passing the same id twice is the engine's own way
 * of saying it and gives the same answer. */
int mmo_display_type_matchup_dual(int attack, int defend1, int defend2, const char **why);

/* Whether this cell is one of the two immunities Foresight clears. The rules
 * are the server's, a caller draws with this, it does not decide with it. */
int mmo_display_type_foresight_clears(int attack, int defend);

/* the official client's compact wire id to an engine type id, and back. `wire` 17 answers
 * MMO_TYPE_NO_TYPE with no reason set; anything the client cannot place traps. */
int mmo_display_type_from_wire(int wire, const char **why);
int mmo_display_type_to_wire(int type, const char **why);

/* The four buckets a screen actually colours by. Derived from a multiplier in
 * tenths, so a quadruple and a double are both SUPER, the number is what the
 * caller prints, this is what it tints. */
enum mmo_display_effect {
    MMO_EFFECT_IMMUNE = 0,
    MMO_EFFECT_RESISTED,
    MMO_EFFECT_NEUTRAL,
    MMO_EFFECT_SUPER
};
int mmo_display_effect_of(int tenths);

/* the official client's catalogue bases, measured from the official client: each ROM bank is copied
 * to the base and data/strings/strings_en.xml then wins at the same id. */
#define MMO_STR_MOVE_NAME     110000
#define MMO_STR_MOVE_DESC     120000
#define MMO_STR_ABILITY_NAME  210000
#define MMO_STR_ABILITY_DESC  220000
#define MMO_STR_TYPE_NAME     230000

/* The last id each engine table holds. Overlay ids may sit past these;
 * mmo_display_move_type and mmo_display_species_typing do not, because
 * nothing here has a type for a move or species the engine never listed. */
#define MMO_DISPLAY_ABILITY_LAST  123  /* BAD_DREAMS */
#define MMO_DISPLAY_MOVE_LAST     467  /* SHADOW_FORCE */
#define MMO_DISPLAY_SPECIES_LAST  493  /* ARCEUS */

/* A catalogue lookup. Overlay first, then the engine table that fills that
 * base; anything else traps. {STRING_N} placeholders are carried verbatim. */
const char *mmo_display_string(int id, const char **why);

const char *mmo_display_ability_name(int id, const char **why);
const char *mmo_display_ability_desc(int id, const char **why);
const char *mmo_display_move_name(int id, const char **why);
const char *mmo_display_move_desc(int id, const char **why);

/* The engine type id of a move the engine has. Overlay-only moves (Trick-
 * or-Treat and the rest of the 1000-block) have a name and no type here. */
int mmo_display_move_type(int id, const char **why);

/* The two type slots the engine stores. A mono-typed species repeats itself.
 * type1/type2 may be NULL. Returns 0, or MMO_TYPE_INVALID on a trap. */
int mmo_display_species_typing(int species, int *type1, int *type2, const char **why);

#endif /* OPENMMO_DISPLAY_H */
