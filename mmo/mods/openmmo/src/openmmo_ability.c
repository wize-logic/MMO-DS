/* What an ability Gen 5 invented does in this game's battle. */
#include "constants/battle.h"
#include "constants/pokemon.h"
#include "generated/abilities.h"
#include "generated/pokemon_types.h"

#include "../include/openmmo_ability_ids.h"

/* Whether an ability turns the target's off for one hit, the way Mold Breaker
 * does. Gen 5 gave the two cover legendaries a spelling each of the same rule;
 * the engine's own check is a bare `!= ABILITY_MOLD_BREAKER`, so this is what
 * the patched one asks instead. */
int openmmo_ability_breaks_mould(int ability)
{
    return ability == ABILITY_MOLD_BREAKER
        || ability == OPENMMO_ABILITY_TURBOBLAZE
        || ability == OPENMMO_ABILITY_TERAVOLT;
}

/* Sand Rush, beside Swift Swim and Chlorophyll. The caller has already decided
 * that the weather is being felt (Cloud Nine and Air Lock turn the whole block
 * off), so this answers only about the ability and the sand. */
int openmmo_ability_speed_doubled_in_sand(int ability)
{
    return ability == OPENMMO_ABILITY_SAND_RUSH;
}

/* Prankster: a status move goes a priority band earlier. `is_status` is the
 * move's class, which the caller has; a move with power is untouched. */
int openmmo_ability_priority_bonus(int ability, int is_status)
{
    if (ability == OPENMMO_ABILITY_PRANKSTER && is_status)
        return 1;
    return 0;
}

/*
 * The percentage an ability puts on the attack stat, applied the way the engine applies
 * Hustle's and Guts': `stat * percent / 100`.
 */
int openmmo_ability_attack_percent(int ability, int cur_hp, int max_hp)
{
    if (ability == OPENMMO_ABILITY_DEFEATIST && max_hp > 0 && cur_hp * 2 <= max_hp)
        return 50;
    return 100;
}

/* The percentage an ability puts on the move's power. */
int openmmo_ability_power_percent(int ability, int move_type, int has_secondary, int in_sand)
{
    if (ability == OPENMMO_ABILITY_SHEER_FORCE && has_secondary)
        return 130;
    if (ability == OPENMMO_ABILITY_SAND_FORCE && in_sand
        && (move_type == TYPE_ROCK || move_type == TYPE_GROUND || move_type == TYPE_STEEL))
        return 130;
    return 100;
}

/* Whether Sheer Force has spent the rider it was paid for. The power and the
 * rider are decided at two different sites, so both ask. */
int openmmo_ability_spends_secondary(int ability)
{
    return ability == OPENMMO_ABILITY_SHEER_FORCE;
}

/*
 * The percentage the two sides' abilities put on the hit rate, applied the way the engine
 * applies Compound Eyes' and Hustle's. Victory Star is the user's own accuracy in a single
 * battle, it raises its allies' too, and there are no allies here.
 */
int openmmo_ability_accuracy_percent(int attacker_ability, int defender_ability, int is_status)
{
    int percent = 100;

    if (attacker_ability == OPENMMO_ABILITY_VICTORY_STAR)
        percent = percent * 110 / 100;
    if (defender_ability == OPENMMO_ABILITY_WONDER_SKIN && is_status)
        percent = percent * 50 / 100;
    return percent;
}

/* An ability answered as one of this game's own, for the on-hit switch alone. */
int openmmo_ability_on_hit_as(int ability)
{
    if (ability == OPENMMO_ABILITY_IRON_BARBS)
        return ABILITY_ROUGH_SKIN;
    return ability;
}

/* Big Pecks, beside Keen Eye and Hyper Cutter: one stat the foe cannot lower.
 * The engine's own message for those two reads the ability off the monster, so
 * this one names itself correctly as soon as the ability name bank is filled
 * (tools/portabilities.py), with nothing to add here. */
int openmmo_ability_blocks_stat_drop(int ability, int stat)
{
    return ability == OPENMMO_ABILITY_BIG_PECKS && stat == BATTLE_STAT_DEFENSE;
}
