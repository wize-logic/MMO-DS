/* Whether a move past Shadow Force is one a fill put in. */
#include "move_port.h"

static int g_table = MMO_MOVE_TABLE_ROM_MEMBERS;
static int g_anim = MMO_MOVE_ANIM_ROM_MEMBERS;
static int g_seq = MMO_MOVE_SEQ_ROM_MEMBERS;

void mmo_move_port_set_members(int table, int anim, int seq)
{
    g_table = table < MMO_MOVE_TABLE_ROM_MEMBERS ? MMO_MOVE_TABLE_ROM_MEMBERS : table;
    g_anim = anim < MMO_MOVE_ANIM_ROM_MEMBERS ? MMO_MOVE_ANIM_ROM_MEMBERS : anim;
    g_seq = seq < MMO_MOVE_SEQ_ROM_MEMBERS ? MMO_MOVE_SEQ_ROM_MEMBERS : seq;
}

int mmo_move_port_table_members(void)
{
    return g_table;
}

int mmo_move_port_max(void)
{
    if (g_table >= MMO_MOVE_PORT_MEMBERS && g_anim >= MMO_MOVE_PORT_MEMBERS
        && g_seq >= MMO_MOVE_PORT_MEMBERS)
        return MMO_MOVE_PORT_LAST;
    return MMO_MOVE_LAST_SHIPPED;
}

int mmo_move_port_is_served(int move)
{
    return move >= 1 && move <= mmo_move_port_max();
}

int mmo_move_port_script_id(int move)
{
    if (move >= 0 && move <= MMO_MOVE_LAST_SHIPPED)
        return move;
    if (mmo_move_port_is_served(move))
        return move;
    return MMO_MOVE_PLACEHOLDER;
}
