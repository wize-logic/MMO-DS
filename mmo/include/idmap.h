#ifndef OPENMMO_IDMAP_H
#define OPENMMO_IDMAP_H
/* Translate ids between the MonMMO server and the pokeplatinum engine. */
#include "mmo.h"

#define MMO_ID_NONE 0

/* The bounds these functions enforce, each tagged to the engine constant it
 * mirrors so a later engine bump is a one-line change here. */
#define MMO_SPECIES_MAX      493   /* SPECIES_ARCEUS: last species platinum draws */
#define MMO_MOVE_MAX         467   /* MOVE_SHADOW_FORCE: last move either side has */
/* The last header the image itself carries is MAP_HEADER_ROCK_PEAK_RUINS at 592. */
#define MMO_MAP_HEADER_MAX   0xFFFF
#define MMO_ITEM_REGION      5     /* item wire ids arrive in the region-5 block */
#define MMO_ITEM_REGION_MUL  1000  /* wire id = region*1000 + Gen-5 item index */

/* The text id layout, shared with the server's TextId. Sinnoh is the region the
 * engine draws, so it is the only one whose strings this client can resolve. */
#define MMO_TEXT_REGION        3   /* Region.SINNOH's wire value */
#define MMO_TEXT_REGION_SHIFT  28
#define MMO_TEXT_BANK_SHIFT    16
#define MMO_TEXT_BANK_MASK     0xFFFu
#define MMO_TEXT_ENTRY_MASK    0xFFFFu

u16 mmo_id_species_from_server(u16 dex,  const char **why);
u16 mmo_id_species_to_server(u16 ds,     const char **why);
u16 mmo_id_move_from_server(u16 move,    const char **why);
u16 mmo_id_move_to_server(u16 ds,        const char **why);
u16 mmo_id_item_from_server(u16 wire,    const char **why);
u16 mmo_id_item_to_server(u16 ds,        const char **why);

/* Text is a pair, not a scalar, so these report through out-params and return 0
 * on success or -1 on a trap. The trap contract above is otherwise unchanged:
 * *why points at a static reason and the out-params are left alone. */
int mmo_id_text_from_server(u32 wire, u16 *bank, u16 *entry, const char **why);
int mmo_id_text_to_server(u16 bank, u16 entry, u32 *wire,    const char **why);

/* A DS map id is the platinum header split across the two bytes the packet
 * has room for. Returns that header (0 .. MMO_MAP_HEADER_MAX), or -1 on a
 * trap, header 0 is a real map, so this cannot use MMO_ID_NONE. */
int mmo_id_map_header_from_server(int region, int bank, int map, const char **why);

#endif /* OPENMMO_IDMAP_H */
