/* A 0x33 becomes the commands the engine scene already draws. */

#include "battle_anim.h"
#include "idmap.h"

#include <string.h>

static void emit(mmo_battle_anim *out, int cap, int *n, const mmo_battle_anim *a)
{
    if (*n < cap && out)
        out[*n] = *a;
    (*n)++;
}

static void emit_print(mmo_battle_anim *out, int cap, int *n,
                       const mmo_battle_move_event *ev)
{
    mmo_battle_anim a;

    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_PRINT;
    a.command = MMO_BTLCMD_PRINT_ATTACK_MESSAGE;
    a.id = (u16)ev->source_move;
    a.source = ev->source_entity;
    a.hp = -1;
    emit(out, cap, n, &a);
}

static void emit_move(mmo_battle_anim *out, int cap, int *n,
                      const mmo_battle_move_event *ev)
{
    const char *why = NULL;
    u16 move = mmo_id_move_from_server((u16)ev->source_move, &why);
    mmo_battle_anim a;
    u32 target = ev->n_targets > 0 ? ev->target[0].entity_id : 0;

    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_MOVE;
    a.command = MMO_BTLCMD_SET_MOVE_ANIMATION;
    a.anim_mode = 0;
    a.source = ev->source_entity;
    a.target = target;
    a.hp = -1;
    if (move == MMO_ID_NONE) {
        a.id = MMO_BTL_ANIM_FALLBACK_MOVE;
        a.fallback = 1;
        a.why = why;
    } else {
        a.id = move;
    }
    emit(out, cap, n, &a);
}

static void emit_status(mmo_battle_anim *out, int cap, int *n,
                        u32 source, u32 target, int stages)
{
    mmo_battle_anim a;

    if (stages == 0)
        return;
    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_STATUS;
    a.command = MMO_BTLCMD_SET_MOVE_ANIMATION;
    a.anim_mode = 1;
    a.id = stages > 0 ? MMO_BTL_SUBANIM_STAT_BOOST
                      : MMO_BTL_SUBANIM_STAT_DROP;
    a.source = source;
    a.target = target;
    a.hp = -1;
    emit(out, cap, n, &a);
}

static void emit_flicker(mmo_battle_anim *out, int cap, int *n,
                         u32 source, u32 target)
{
    mmo_battle_anim a;

    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_FLICKER;
    a.command = MMO_BTLCMD_FLICKER_BATTLER;
    a.source = source;
    a.target = target;
    a.hp = -1;
    emit(out, cap, n, &a);
}

static void emit_hp(mmo_battle_anim *out, int cap, int *n,
                    u32 source, u32 target, int hp)
{
    mmo_battle_anim a;

    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_HP;
    a.command = MMO_BTLCMD_UPDATE_HP_GAUGE;
    a.source = source;
    a.target = target;
    a.hp = hp;
    emit(out, cap, n, &a);
}

static void emit_faint(mmo_battle_anim *out, int cap, int *n,
                       u32 source, u32 target)
{
    mmo_battle_anim a;

    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_FAINT;
    a.command = MMO_BTLCMD_PLAY_FAINTING_SEQUENCE;
    a.source = source;
    a.target = target;
    a.hp = -1;
    emit(out, cap, n, &a);
}

int mmo_battle_map_move(const mmo_battle_move_event *ev,
                        mmo_battle_anim *out, int cap)
{
    int n = 0;
    int t, s;
    int fainted[MMO_BATTLE_MOVE_TARGETS];

    if (!ev)
        return -1;
    if (cap < 0)
        cap = 0;

    emit_print(out, cap, &n, ev);
    emit_move(out, cap, &n, ev);

    memset(fainted, 0, sizeof fainted);
    for (t = 0; t < ev->n_targets; t++) {
        const mmo_battle_effect_target *tg = &ev->target[t];
        for (s = 0; s < tg->n_subs; s++) {
            const mmo_battle_sub_event *sub = &tg->sub[s];
            switch (sub->type) {
            case MMO_BATTLE_SUB_HP:
                emit_flicker(out, cap, &n, ev->source_entity, tg->entity_id);
                emit_hp(out, cap, &n, ev->source_entity, tg->entity_id,
                        sub->hp);
                if (sub->hp < 1 && !fainted[t]) {
                    emit_faint(out, cap, &n, ev->source_entity, tg->entity_id);
                    fainted[t] = 1;
                }
                break;
            case MMO_BATTLE_SUB_STAT:
                emit_status(out, cap, &n, ev->source_entity, tg->entity_id,
                            sub->stages);
                break;
            case MMO_BATTLE_SUB_FAINT:
                if (!fainted[t]) {
                    emit_faint(out, cap, &n, ev->source_entity, tg->entity_id);
                    fainted[t] = 1;
                }
                break;
            case MMO_BATTLE_SUB_EFFECT:
            case MMO_BATTLE_SUB_FAIL:
                break;
            default:
                break;
            }
        }
    }
    return n;
}

int mmo_battle_map_switch(const mmo_battle_switch_in *sw,
                          mmo_battle_anim *out, int cap)
{
    int n = 0;
    mmo_battle_anim a;

    if (!sw)
        return -1;
    if (cap < 0)
        cap = 0;

    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_RETURN;
    a.command = MMO_BTLCMD_RETURN_POKEMON;
    a.hp = -1;
    emit(out, cap, &n, &a);

    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_SHOW;
    a.command = MMO_BTLCMD_SHOW_POKEMON;
    a.source = sw->entity_id;
    a.id = (u16)(sw->species > 0 ? sw->species : 0);
    a.hp = sw->hp;
    emit(out, cap, &n, &a);
    return n;
}

int mmo_battle_map_catch(int item, u32 entity_id,
                         mmo_battle_anim *out, int cap)
{
    int n = 0;
    mmo_battle_anim a;

    if (cap < 0)
        cap = 0;
    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_CATCH;
    a.command = MMO_BTLCMD_OPEN_CAPTURE_BALL;
    a.source = entity_id;
    a.id = item > 0 ? (u16)item : 0;
    a.hp = -1;
    emit(out, cap, &n, &a);
    return n;
}

int mmo_battle_map_switch_prompt(mmo_battle_anim *out, int cap)
{
    int n = 0;
    mmo_battle_anim a;

    if (cap < 0)
        cap = 0;
    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_PARTY;
    a.command = MMO_BTLCMD_SHOW_PARTY_MENU;
    a.hp = -1;
    emit(out, cap, &n, &a);
    return n;
}

int mmo_battle_map_reward(u32 entity_id, int xp, int leveled,
                          mmo_battle_anim *out, int cap)
{
    int n = 0;
    mmo_battle_anim a;

    if (cap < 0)
        cap = 0;
    memset(&a, 0, sizeof a);
    a.kind = MMO_ANIM_EXP;
    a.command = MMO_BTLCMD_UPDATE_EXP_GAUGE;
    a.source = entity_id;
    a.hp = xp;
    emit(out, cap, &n, &a);
    if (leveled) {
        memset(&a, 0, sizeof a);
        a.kind = MMO_ANIM_LEVEL;
        a.command = MMO_BTLCMD_PLAY_LEVEL_UP_ANIMATION;
        a.source = entity_id;
        a.hp = -1;
        emit(out, cap, &n, &a);
    }
    return n;
}

static const char *const kCommandName[MMO_BATTLE_COMMAND_COUNT] = {
    "NONE",
    "SETUP_UI",
    "SET_ENCOUNTER",
    "SHOW_ENCOUNTER",
    "SHOW_POKEMON",
    "RETURN_POKEMON",
    "OPEN_CAPTURE_BALL",
    "DELETE_POKEMON",
    "SET_TRAINER_ENCOUNTER",
    "THROW_TRAINER_BALL",
    "SLIDE_TRAINER_OUT",
    "SLIDE_TRAINER_IN",
    "SLIDE_HEALTHBOX_IN",
    "SLIDE_HEALTHBOX_OUT",
    "SET_COMMAND_SELECTION",
    "SHOW_MOVE_SELECT_MENU",
    "SHOW_TARGET_SELECT_MENU",
    "SHOW_BAG_MENU",
    "SHOW_PARTY_MENU",
    "SHOW_YES_NO_MENU",
    "PRINT_ATTACK_MESSAGE",
    "PRINT_MESSAGE",
    "SET_MOVE_ANIMATION",
    "FLICKER_BATTLER",
    "UPDATE_HP_GAUGE",
    "UPDATE_EXP_GAUGE",
    "PLAY_FAINTING_SEQUENCE",
    "PLAY_SOUND",
    "FADE_OUT",
    "TOGGLE_VANISH",
    "SET_STATUS_ICON",
    "PRINT_TRAINER_MESSAGE",
    "PRINT_RECALL_MESSAGE",
    "PRINT_SEND_OUT_MESSAGE",
    "PRINT_BATTLE_START_MESSAGE",
    "PRINT_LEAD_MON_MESSAGE",
    "PLAY_LEVEL_UP_ANIMATION",
    "SET_ALERT_MESSAGE",
    "REFRESH_HP_GAUGE",
    "UPDATE_PARTY_MON",
    "SLIDE_IN_PANEL",
    "STOP_GAUGE_ANIMATION",
    "REFRESH_PARTY_STATUS",
    "FORGET_MOVE",
    "SET_MOSAIC",
    "CHANGE_WEATHER_FORM",
    "UPDATE_BG",
    "CLEAR_TOUCH_SCREEN",
    "SHOW_BATTLE_START_PARTY_GAUGE",
    "HIDE_BATTLE_START_PARTY_GAUGE",
    "SHOW_PARTY_GAUGE",
    "HIDE_PARTY_GAUGE",
    "LOAD_PARTY_GAUGE_GRAPHICS",
    "FREE_PARTY_GAUGE_GRAPHICS",
    "INCREMENT_RECORD",
    "PRINT_LINK_WAIT_MESSAGE",
    "RESTORE_SPRITE",
    "SPRITE_TO_OAM",
    "OAM_TO_SPRITE",
    "PRINT_RESULT_MESSAGE",
    "PRINT_ESCAPE_MESSAGE",
    "PRINT_FORFEIT_MESSAGE",
    "REFRESH_SPRITE",
    "FLY_MOVE_HIT_SOUND_EFFECT",
    "PLAY_MUSIC",
    "SUBMIT_RESULT",
    "CLEAR_MESSAGE_BOX",
};

int mmo_battle_command_count(void)
{
    return MMO_BATTLE_COMMAND_COUNT;
}

const char *mmo_battle_command_name(int command)
{
    if (command < 0 || command >= MMO_BATTLE_COMMAND_COUNT)
        return NULL;
    return kCommandName[command];
}

const char *mmo_battle_anim_name(u8 command)
{
    const char *s = mmo_battle_command_name((int)command);

    return s ? s : "?";
}

int mmo_battle_anim_write(const mmo_battle_anim *a, u8 *out, size_t cap)
{
    size_t n;

    if (!a || !out)
        return -1;
    switch (a->kind) {
    case MMO_ANIM_MOVE:
    case MMO_ANIM_STATUS:
        n = MMO_BTL_ANIM_MOVE_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_SET_MOVE_ANIMATION;
        out[2] = (u8)(a->id & 0xFF);
        out[3] = (u8)(a->id >> 8);
        /* animMode at offset 76, secondaryAnimID at 80, measured. */
        if (a->anim_mode) {
            out[76] = 1;
            out[80] = (u8)(a->id & 0xFF);
            out[81] = (u8)(a->id >> 8);
            out[2] = 0;
            out[3] = 0;
        }
        return (int)n;
    case MMO_ANIM_FLICKER:
        n = MMO_BTL_ANIM_FLICKER_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_FLICKER_BATTLER;
        return (int)n;
    case MMO_ANIM_HP:
        n = MMO_BTL_ANIM_HP_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_UPDATE_HP_GAUGE;
        out[2] = (u8)(a->hp & 0xFF);
        out[3] = (u8)((a->hp >> 8) & 0xFF);
        return (int)n;
    case MMO_ANIM_FAINT:
        n = MMO_BTL_ANIM_FAINT_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_PLAY_FAINTING_SEQUENCE;
        return (int)n;
    case MMO_ANIM_SHOW:
        n = MMO_BTL_ANIM_SHOW_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_SHOW_POKEMON;
        out[2] = (u8)(a->id & 0xFF);
        out[3] = (u8)(a->id >> 8);
        return (int)n;
    case MMO_ANIM_RETURN:
        n = MMO_BTL_ANIM_RETURN_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_RETURN_POKEMON;
        return (int)n;
    case MMO_ANIM_CATCH:
        n = MMO_BTL_ANIM_CATCH_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_OPEN_CAPTURE_BALL;
        out[2] = (u8)(a->id & 0xFF);
        out[3] = (u8)(a->id >> 8);
        return (int)n;
    case MMO_ANIM_PARTY:
        n = MMO_BTL_ANIM_PARTY_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_SHOW_PARTY_MENU;
        return (int)n;
    case MMO_ANIM_EXP:
        n = MMO_BTL_ANIM_EXP_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_UPDATE_EXP_GAUGE;
        /* gainedExp at offset 8, the number 0x79 names. */
        out[8] = (u8)(a->hp & 0xFF);
        out[9] = (u8)((a->hp >> 8) & 0xFF);
        out[10] = (u8)((a->hp >> 16) & 0xFF);
        out[11] = (u8)((a->hp >> 24) & 0xFF);
        return (int)n;
    case MMO_ANIM_LEVEL:
        n = MMO_BTL_ANIM_LEVEL_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_PLAY_LEVEL_UP_ANIMATION;
        return (int)n;
    case MMO_ANIM_PRINT: {
        const char *why = NULL;
        u16 move;

        n = MMO_BTL_ANIM_PRINT_SIZE;
        if (cap < n)
            return -1;
        memset(out, 0, n);
        out[0] = MMO_BTLCMD_PRINT_ATTACK_MESSAGE;
        move = mmo_id_move_from_server(a->id, &why);
        if (move != MMO_ID_NONE) {
            out[2] = (u8)(move & 0xFF);
            out[3] = (u8)(move >> 8);
        }
        return (int)n;
    }
    default:
        return -1;
    }
}
