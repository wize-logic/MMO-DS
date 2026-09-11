#ifndef OPENMMO_SPECIES_PORT_H
#define OPENMMO_SPECIES_PORT_H
/*
 * The species this game did not ship, once a fill has put them in its own
 * archives.
 */
#include "mmo.h"

/* What each archive holds before a fill. A live count at or below its own means
 * no fill has happened, and every question below is refused. */
#define MMO_PORTED_SPECIES_ROM_MEMBERS 508
#define MMO_PORTED_ICON_ROM_MEMBERS    547

/* Two of the species a fill appends collide with ids this engine already spends. */
#define MMO_PORTED_EGG_ID            494
#define MMO_PORTED_BAD_EGG_ID        495
#define MMO_PORTED_VICTINI_ENGINE_ID 650
#define MMO_PORTED_SNIVY_ENGINE_ID   651
int mmo_species_port_engine_id(int wire);
int mmo_species_port_wire_id(int engine);

int mmo_species_port_members(void);
void mmo_species_port_set_members(int members);

/* The last species this client can answer for. Without a fill that is the one
 * before the first appended id, which is to say none of them. */
int mmo_species_port_max(void);

/* Whether this id is one a fill is meant to serve at all. Cheap, and it does
 * Not say the fill happened; the locators below do. */
int mmo_species_port_is_ported(int species);

/* 1 when this build's tables hold engine id `engine`, the game's own species
 * always, a ported one only once the fill served it. The bound a seat asks
 * before an id becomes a Pokemon; idmap.h's range says "is a species", this
 * says "is in this build". */
int mmo_species_port_live(int engine);

/* The member of pl_personal, wotbl and evo that holds this species. One
 * function for all three because all three held 508 members before the fill and
 * are appended in step; a fill that wrote only some of them is not one this
 * client's tool produced. */
int mmo_species_port_member(int species, int *member, const char **why);

/* The member of pl_poke_icon that holds this species' icon, and which of the
 * three icon palettes it is drawn with. */
int mmo_species_port_icon(int species, int *member, int *palette, const char **why);

/* The message index of this species' name in the species name bank. The bank
 * keeps its two egg names at 494 and 495, so the appended run starts after
 * them and the index is not the species id. */
int mmo_species_port_name(int species, int *entry, const char **why);

#endif /* OPENMMO_SPECIES_PORT_H */
