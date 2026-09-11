#ifndef OPENMMO_MOVE_PORT_H
#define OPENMMO_MOVE_PORT_H

/* Where a fill put the moves this game did not ship. */

#define MMO_MOVE_LAST_SHIPPED       467   /* MOVE_SHADOW_FORCE */
#define MMO_MOVE_PORT_FIRST         468   /* MOVE_HONE_CLAWS, on both wires */
#define MMO_MOVE_PORT_LAST          559   /* MOVE_FUSION_BOLT */
#define MMO_MOVE_PLACEHOLDER        468   /* the engine's own empty script, in the ROM */

#define MMO_MOVE_TABLE_ROM_MEMBERS  471   /* poketool/waza/pl_waza_tbl.narc */
#define MMO_MOVE_ANIM_ROM_MEMBERS   501   /* wazaeffect/we.arc */
#define MMO_MOVE_SEQ_ROM_MEMBERS    501   /* battle/skill/waza_seq.narc */
#define MMO_MOVE_PORT_MEMBERS       (MMO_MOVE_PORT_LAST + 1)

/* The live counts, as the fused plugin measured them at boot. Never below the
 * cartridge's own. */
void mmo_move_port_set_members(int table, int anim, int seq);
int mmo_move_port_table_members(void);

/* The last move the client will hand the engine: 559 when all three archives
 * reach 560, 467 otherwise. A fill is all three or none. */
int mmo_move_port_max(void);
int mmo_move_port_is_served(int move);

/* The member the two script archives should be read at for a move: the move
 * itself when this game ships it or a fill serves it, the placeholder otherwise.
 * The engine's script loaders ask this before they read. */
int mmo_move_port_script_id(int move);

#endif
