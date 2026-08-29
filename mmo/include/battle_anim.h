#ifndef OPENMMO_BATTLE_ANIM_H
#define OPENMMO_BATTLE_ANIM_H
/*
 * Map a server battle event onto the commands the engine scene already knows
 * how to draw.
 */

#include "game.h"

/* The engine's BATTLE_COMMAND_* values this mapping produces. The numbers
 * are the enum's own, pinned here so this header does not pull the engine
 * in. A lab fight's first byte of each scene message is the check. */
#define MMO_BATTLE_COMMAND_COUNT           67

#define MMO_BTLCMD_SHOW_POKEMON            4
#define MMO_BTLCMD_RETURN_POKEMON          5
#define MMO_BTLCMD_OPEN_CAPTURE_BALL       6
#define MMO_BTLCMD_SHOW_PARTY_MENU         18
#define MMO_BTLCMD_PRINT_ATTACK_MESSAGE    20
#define MMO_BTLCMD_SET_MOVE_ANIMATION      22
#define MMO_BTLCMD_FLICKER_BATTLER         23
#define MMO_BTLCMD_UPDATE_HP_GAUGE         24
#define MMO_BTLCMD_UPDATE_EXP_GAUGE        25
#define MMO_BTLCMD_PLAY_FAINTING_SEQUENCE  26
#define MMO_BTLCMD_PLAY_LEVEL_UP_ANIMATION 35

/* WE_SUB member ids from generated/battle_sub_animations.txt, used when
 * anim_mode is 1. STAT_BOOST / STAT_DROP are what a local fight plays after
 * a stage-changing move. */
#define MMO_BTL_SUBANIM_STAT_BOOST  12
#define MMO_BTL_SUBANIM_STAT_DROP   13

/* The engine's own placeholder for a move it cannot look up. */
#define MMO_BTL_ANIM_FALLBACK_MOVE  1   /* MOVE_POUND */

/* SET_MOVE_ANIMATION is a MoveAnimation; FLICKER is an int command;
 * UPDATE_HP_GAUGE is an HPGaugeUpdateMessage; FAINT is a
 * FaintingSequenceMessage. Sizes measured from the same fight. */
#define MMO_BTL_ANIM_MOVE_SIZE     88
#define MMO_BTL_ANIM_FLICKER_SIZE   4
#define MMO_BTL_ANIM_HP_SIZE       20
#define MMO_BTL_ANIM_FAINT_SIZE    48
#define MMO_BTL_ANIM_SHOW_SIZE    116
#define MMO_BTL_ANIM_RETURN_SIZE   44
#define MMO_BTL_ANIM_CATCH_SIZE     4
#define MMO_BTL_ANIM_PARTY_SIZE    40
#define MMO_BTL_ANIM_EXP_SIZE      16
#define MMO_BTL_ANIM_LEVEL_SIZE     4
#define MMO_BTL_ANIM_PRINT_SIZE     4

#define MMO_BATTLE_ANIM_MAX 16

typedef enum {
    MMO_ANIM_MOVE = 0,    /* SET_MOVE_ANIMATION, anim_mode 0, id is a move */
    MMO_ANIM_STATUS,      /* SET_MOVE_ANIMATION, anim_mode 1, id is WE_SUB */
    MMO_ANIM_FLICKER,     /* FLICKER_BATTLER */
    MMO_ANIM_HP,          /* UPDATE_HP_GAUGE */
    MMO_ANIM_FAINT,       /* PLAY_FAINTING_SEQUENCE */
    MMO_ANIM_SHOW,        /* SHOW_POKEMON: a switch-in */
    MMO_ANIM_RETURN,      /* RETURN_POKEMON: the mon leaving */
    MMO_ANIM_CATCH,       /* OPEN_CAPTURE_BALL */
    MMO_ANIM_PARTY,       /* SHOW_PARTY_MENU: forced replacement */
    MMO_ANIM_EXP,         /* UPDATE_EXP_GAUGE */
    MMO_ANIM_LEVEL,       /* PLAY_LEVEL_UP_ANIMATION */
    MMO_ANIM_PRINT        /* PRINT_ATTACK_MESSAGE */
} mmo_anim_kind;

typedef struct {
    mmo_anim_kind kind;
    u8  command;          /* MMO_BTLCMD_* */
    u8  anim_mode;        /* 0 = move NARC, 1 = WE_SUB */
    u8  fallback;         /* 1 = the Pound placeholder */
    u16 id;               /* move or secondaryAnimID */
    u32 source;           /* entity that used the move; 0 if none */
    u32 target;           /* entity the command is aimed at; 0 if none */
    int hp;               /* resulting hp for MMO_ANIM_HP; -1 otherwise */
    const char *why;      /* set when fallback is 1; NULL otherwise */
} mmo_battle_anim;

/*
 * Map a move event onto the command sequence a local fight would emit for the same outcome.
 * Writes at most `cap` entries and returns how many the event produced, more than `cap` means
 * the tail was dropped.
 */
int mmo_battle_map_move(const mmo_battle_move_event *ev,
                        mmo_battle_anim *out, int cap);

/*
 * A switch-in (0x35) is RETURN of the outgoing slot then SHOW of the incoming mon. A catch
 * throw (0x37 sub-kind 4) is OPEN_CAPTURE_BALL.
 */
int mmo_battle_map_switch(const mmo_battle_switch_in *sw,
                          mmo_battle_anim *out, int cap);
int mmo_battle_map_catch(int item, u32 entity_id,
                         mmo_battle_anim *out, int cap);
int mmo_battle_map_switch_prompt(mmo_battle_anim *out, int cap);
int mmo_battle_map_reward(u32 entity_id, int xp, int leveled,
                          mmo_battle_anim *out, int cap);

/* The engine command's short name (BATTLE_COMMAND_* without the prefix).
 * `mmo_battle_command_name` returns NULL off 0..66; `mmo_battle_anim_name`
 * returns "?" for one this tree has no name for. */
int mmo_battle_command_count(void);
const char *mmo_battle_command_name(int command);
const char *mmo_battle_anim_name(u8 command);

/*
 * Write the scene message for one mapped command. Only the fields this mapping owns are
 * filled: opcode, move / secondary id, anim_mode, the resulting hp.
 */
int mmo_battle_anim_write(const mmo_battle_anim *a, u8 *out, size_t cap);

#endif /* OPENMMO_BATTLE_ANIM_H */
