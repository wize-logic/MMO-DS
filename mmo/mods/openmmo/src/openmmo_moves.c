/* What the engine asks about a move this game did not ship. */

#include <stdio.h>
#include <string.h>

#include "constants/moves.h"

#include "message_util.h"
#include "move_table.h"
#include "narc.h"
#include "pc_modfs.h"
#include "string_gf.h"

#include "../../../include/charcode.h"
#include "../../../include/move_port.h"

static void bind_once(void)
{
    static int bound;
    unsigned table, anim, seq;

    if (bound)
        return;
    bound = 1;
    table = pc_modfs_narc_file_count("poketool/waza/pl_waza_tbl.narc",
        (unsigned)MMO_MOVE_TABLE_ROM_MEMBERS);
    anim = pc_modfs_narc_file_count("wazaeffect/we.arc",
        (unsigned)MMO_MOVE_ANIM_ROM_MEMBERS);
    seq = pc_modfs_narc_file_count("battle/skill/waza_seq.narc",
        (unsigned)MMO_MOVE_SEQ_ROM_MEMBERS);
    mmo_move_port_set_members((int)table, (int)anim, (int)seq);
    printf("openmmo: move overlay pl_waza_tbl=%u we=%u waza_seq=%u, moves to %d\n",
        table, anim, seq, mmo_move_port_max());
}

/* The last move the engine may be handed: 559 with a fill, 467 without. */
int openmmo_move_max(void)
{
    bind_once();
    return mmo_move_port_max();
}

int openmmo_move_served(unsigned move)
{
    bind_once();
    return mmo_move_port_is_served((int)move);
}

/* The member a script archive is read at for a move: the move itself when this
 * game ships it or a fill serves it, the engine's own empty placeholder
 * otherwise. */
int openmmo_move_script_id(int move)
{
    bind_once();
    return mmo_move_port_script_id(move);
}

/* MOVE_DATA() past the ai table's last entry. `table` is the ai copy itself, so
 * everything this game ships is still read from where the engine put it. */
struct MoveTable *openmmo_move_data(struct MoveTable *table, int move)
{
    static MoveTable ported[MMO_MOVE_PORT_LAST + 1 - MMO_MOVE_PORT_FIRST];
    static unsigned char loaded[MMO_MOVE_PORT_LAST + 1 - MMO_MOVE_PORT_FIRST];
    int index;

    if (move >= 0 && move < MAX_MOVES)
        return &table[move];
    if (!openmmo_move_served((unsigned)move))
        return &table[MOVE_NONE];
    index = move - MMO_MOVE_PORT_FIRST;
    if (!loaded[index]) {
        NARC_ReadWholeMemberByIndexPair(&ported[index], NARC_INDEX_POKETOOL__WAZA__PL_WAZA_TBL, move);
        loaded[index] = 1;
    }
    return &ported[index];
}

/* What the engine actually reads for a ported move, through its own getters:
 * the table entry the battle and the summary screen read, and the name out of
 * the bank. tests/sprite_oracle_test.sh does not hold these; the fill's own
 * --check does, and this is the running binary agreeing with it. */
void openmmo_moves_dump_ported(int move)
{
    String *name;
    charcode_t chars[32];
    char utf8[64];

    if (!openmmo_move_served((unsigned)move) || move <= MMO_MOVE_LAST_SHIPPED)
        return;
    name = MessageUtil_MoveName((u32)move, HEAP_ID_SYSTEM);
    memset(chars, 0, sizeof chars);
    String_ToChars(name, chars, sizeof chars / sizeof chars[0]);
    String_Free(name);
    mmo_charcode_to_utf8((const mmo_charcode *)chars, utf8, sizeof utf8);
    printf("ported move %d type %u class %u power %u acc %u pp %u effect %u name %s\n", move,
        MoveTable_LoadParam(move, MOVEATTRIBUTE_TYPE),
        MoveTable_LoadParam(move, MOVEATTRIBUTE_CLASS),
        MoveTable_LoadParam(move, MOVEATTRIBUTE_POWER),
        MoveTable_LoadParam(move, MOVEATTRIBUTE_ACCURACY),
        MoveTable_LoadParam(move, MOVEATTRIBUTE_PP),
        MoveTable_LoadParam(move, MOVEATTRIBUTE_EFFECT),
        utf8);
}
