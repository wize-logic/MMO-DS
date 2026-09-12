/* The client's entry points inside the engine process. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <armrec_rt.h> /* armrec_mem_init, the guest map, claimed before we allocate */
#include <nitro.h> /* OS_GetInitArena*, the bounds of the emulated DS main RAM */

#include "pc_video.h"
#include "pc_gpu2d.h"

#include "savedata.h"
#include "system.h"  /* gSystem: the engine's already-sampled pad and pen */

/* The field-avatar API used to draw remote players. All read-only engine headers;
 * the mod compiles against them with the game's own include path and ABI. */
#include "field/field_system.h" /* FieldSystem: mapObjMan, playerAvatar, task */
#include "field_system.h"       /* FieldSystem_HasChildProcess */
#include "field_task.h"         /* FieldSystem_IsRunningFieldMap */
#include "overlay005/ov5_021DFB54.h" /* PlayerAvatar_SetTransitionState/RequestChangeState */
#include "player_avatar.h"      /* PlayerAvatar_New/Delete/TryFace/GetMapObject/GetXPos */
#include "player_move.h"        /* PlayerAvatar_SetMapObjMovement/GetMovementActionAnimCode */
#include "map_header_data.h"  /* MapHeaderData_GetNumObjectEvents */
#include "map_object.h"         /* MapObject_SetLocalID, MapObjMan_LocalMapObjByIndex, _Delete */
#include "map_object_move.h"    /* MapObject_GetDxFromDir / GetDzFromDir */
#include "map_tile_behavior.h"  /* TileBehavior_IsDoor / IsWarpEntrance* */
#include "terrain_collision_manager.h"
#include "unk_020655F4.h"       /* MovementAction_GetDirFromAction */
#include "generated/map_headers.h"      /* MAP_HEADER_UNDERGROUND */
#include "generated/movement_actions.h" /* enum MovementAction */
#include "generated/movement_types.h"   /* MOVEMENT_TYPE_NONE */
#include "constants/field/map.h"        /* MAP_OBJECT_TILE_SIZE */
#include "constants/map_object.h"       /* DIR_* */
#include "constants/player_avatar.h"    /* PLAYER_AVATAR_WALKING/SURFING */
#include "generated/object_events_gfx.h" /* OBJ_EVENT_GFX_* for the appearance probe */

#include "savedata_misc.h"
#include "string_gf.h"
#include "save_player.h"
#include "trainer_info.h"
#include "vars_flags.h"

#include "field_overworld_state.h"
#include "game_start.h"
#include "location.h"
#include "main.h"
#include "constants/colors.h"
#include "overlay_manager.h"
#include "screen_fade.h"

#include "../../../include/appearance.h"
#include "../../../include/charcode.h"
#include "../../../include/chatwin.h"
#include "../../../include/client.h"
#include "../../../include/platform.h"
#include "../../../include/creator.h"
#include "../../../include/endpoint.h"
#include "../../../include/entity.h"
#include "../../../include/follower.h"
#include "../../../include/entry.h"
#include "../../../include/hud_channel.h"
#include "../../../include/idmap.h"
#include "../../../include/login.h"
#include "../../../include/osk.h"
#include "../../../include/status_channel.h"
#include "../../../include/text_channel.h"

void openmmo_hud_open(void);
int openmmo_hud_windowed(void);
void openmmo_hud_publish_plates(void);
void openmmo_hud_publish_now(openmmo_client *c, uint32_t flags,
                             const char *reason, const char *compose,
                             int composing, const char *compose_to,
                             uint32_t send_type);
unsigned openmmo_hud_take_cmds(uint32_t *dst, unsigned max);
const char *openmmo_hud_cmd_name(int32_t arg);
/* One player action by name, the same switch the guest's
 * own tap-a-player menu runs. */
void openmmo_player_do(int act, const char *name);

/* The two lines Discord is shown, where the player is and
 * who they are, when the front door turned it on. */
void openmmo_presence_tick(FieldSystem *fs, openmmo_client *c);
void openmmo_presence_shutdown(void);

/* The rival's default name. Platinum's own, and the same constant the server
 * answered `bufferRivalName` with while it ran the cutscenes. */
#define OPENMMO_RIVAL_NAME "Barry"

/* mmo/mods/openmmo/src/openmmo_script.c, the local script VM's own state:
 * the server's seat written into VarsFlags, and what the VM writes reported
 * back. No header of its own; three rows are cheaper than a fourth file. */
void openmmo_script_state_reset(void);
/* openmmo_follow_talk.c. An A press on the tile the follower is standing on:
 * 1 when the talk took it. */
int openmmo_follow_talk_try(void *fieldSystemVoid);

/* openmmo_follow.c. fs is a FieldSystem*; fieldReady is this file's own
 * field_ready_for_peers, so there is one definition of settled in the build. */
void openmmo_follow_tick(void *fieldSystemVoid, int fieldReady);
void openmmo_follow_reset(void);
void openmmo_script_state_tick(void *fieldSystemVoid, openmmo_client *c);
void openmmo_script_state_flush(void *fieldSystemVoid, openmmo_client *c);

/* The first monster in a save this build has no tables for,
 * and a sentence naming it and the package it needs. 0 when there is none. */
int openmmo_offline_save_unsupported(SaveData *save, char *why, size_t cap);

FS_EXTERN_OVERLAY(game_start);

typedef char openmmo_boot_charcode_width_check[sizeof(mmo_charcode) == sizeof(charcode_t) ? 1 : -1];

/* The port's one supported accessor for the live field system (added by
 * pc/patches/src/field_system.c.patch); NULL when no map is loaded. */
extern FieldSystem *pc_lab_field_system(void);
extern int openmmo_encounter_start(FieldSystem *fs, const openmmo_client *c,
                                   int foe_species, int foe_level);
extern void openmmo_encounter_poll(FieldSystem *fs, openmmo_client *c);
extern void openmmo_party_field_sync(FieldSystem *fs, const openmmo_client *c);
extern void openmmo_party_report_if_touched(FieldSystem *fs, openmmo_client *c);
extern void openmmo_pc_field_sync(FieldSystem *fs, const openmmo_client *c);
extern void openmmo_pc_reconcile_tick(FieldSystem *fs, openmmo_client *c);
extern int openmmo_encounter_scene_up(void);

/* An A press on a person whose script stages a wild fight. */
extern int openmmo_static_press_try(FieldSystem *fs);

/* A peer's name projected onto the world and painted over it. */
extern void openmmo_label_set(int slot, const char *name);
extern const char *openmmo_label_text(int slot);
extern void openmmo_label_clear(int slot);
extern void openmmo_label_clear_all(void);
extern void openmmo_label_draw_for(int slot, const MapObject *obj);
extern void openmmo_label_frame(uint64_t frame);
extern void openmmo_label_hold(uint64_t frame);

/* The engine bag and shop are displays of server state. */
extern void openmmo_bag_attach(openmmo_client *c);
extern void openmmo_bag_mark_dirty(void);
extern void openmmo_bag_sync(SaveData *save, int bag_app_open);
extern void openmmo_bag_aim_cursor(FieldSystem *fs);
extern void openmmo_shop_mark_pending(void);
extern void openmmo_apps_request(int screen);   /* openmmo_apps.c */
extern void openmmo_apps_pump(FieldSystem *fs);
extern int openmmo_shop_try_open(FieldSystem *fs);
extern void openmmo_bag_try_open(FieldSystem *fs);

/* The engine message box is a display of a server box. */
extern void openmmo_dialog_attach(openmmo_client *c);
extern void openmmo_dialog_mark_pending(void);
extern int openmmo_dialog_try_open(FieldSystem *fs);
extern int openmmo_dialog_ask_local(FieldSystem *fs, const char *utf8,
                                    void (*cb)(int yes));

/* PvP is the engine's own link battle, with the server as the
 * wire between the two engines. */
extern int openmmo_link_battle_open(FieldSystem *fs, openmmo_client *c);
extern void openmmo_link_battle_poll(FieldSystem *fs, openmmo_client *c);
extern void openmmo_link_battle_closed(openmmo_client *c);

/* Play server movement sequences on map objects. */
typedef MapObject *(*openmmo_move_resolve)(FieldSystem *fs, s64 entity_id,
                                           int is_self, int slot);
extern void openmmo_script_move_attach(openmmo_client *c);
extern void openmmo_script_move_mark_pending(void);
extern void openmmo_script_move_pump(FieldSystem *fs,
                                     openmmo_move_resolve resolve);
extern int openmmo_script_move_holds(s64 entity_id);

/* The headless log and the upper compose strip (host
 * framebuffer). Windowed, chat is the viewer's own box off the hud page. */
extern void openmmo_chat_draw(const mmo_chatwin *w, const mmo_chatwin_line *log,
                              int n, const osk_state *osk);

/* Leave the glyph table hostile, under OPENMMO_GLYPH_CLOBBER.
 * Host-surface text (HUD, keyboard) is the ROM font, not a second 5x7. */
extern void openmmo_glyph_clobber(void);
extern int openmmo_font_ready(void);
extern int openmmo_font_cell_h(void);
extern int openmmo_font_draw_utf8(uint32_t *surf, int x, int y, const char *s,
                                  uint32_t fg);
extern int openmmo_font_width_utf8(const char *s);

/* The vanilla device is switched on only where the story
 * says it was handed over; the window hides the second screen either way. */
extern void openmmo_poketch_seat(SaveData *save, const openmmo_script_state *st);
extern void openmmo_poketch_report(FieldSystem *fs);

/* The engine mail app is a display of the server mailbox. */
extern void openmmo_mail_attach(openmmo_client *c);
extern void openmmo_mail_request(s64 mail_id);
extern void openmmo_mail_window_cmd(unsigned arg);
extern int openmmo_mail_try_open(FieldSystem *fs);
extern void openmmo_mail_pump(FieldSystem *fs);

/* The communication club's join list is the link. */
extern void openmmo_union_attach(openmmo_client *c);
extern void openmmo_union_mark_pending(void);
extern int openmmo_union_try_open(FieldSystem *fs);
extern void openmmo_trade_attach(openmmo_client *c);
extern void openmmo_contest_link_attach(openmmo_client *c);
extern void openmmo_trade_offer(const char *name);
extern void openmmo_trade_pump(FieldSystem *fs);
/* The link contest's relay. Pumped outside the settled gate on
 * purpose: a contest is a field task with an application over it,
 * so the field is never settled while one is running, and the
 * traffic that runs it has to keep moving through all of that. */
extern void openmmo_contest_link_pump(void);
/* The headless link-contest station: asks for a group, runs the contest the
 * seat starts, and holds the pen the acting round wants. Off unless
 * OPENMMO_LAB_CONTEST_LINK names one. mmo/mods/openmmo/src/openmmo_contest_lab.c */
extern void openmmo_contest_lab_attach(openmmo_client *c);
extern void openmmo_contest_lab_frame(FieldSystem *fs, int settled);
extern void openmmo_trade_tick(void);
extern void openmmo_gtl_attach(openmmo_client *c);
extern void openmmo_gtl_mark_pending(void);
extern int openmmo_gtl_local_command(const char *line);
extern int openmmo_gtl_try_open(FieldSystem *fs);
extern void openmmo_gtl_window_cmd(unsigned arg);
extern void openmmo_gtl_tick(void);
extern void openmmo_player_attach(openmmo_client *c);
extern int openmmo_player_try_open(FieldSystem *fs, const char *name);
extern void openmmo_widget_attach(openmmo_client *c);
extern void openmmo_widget_mark_pending(void);
extern int openmmo_widget_try_open(FieldSystem *fs);
extern void openmmo_travel_attach(openmmo_client *c);
extern void openmmo_travel_tick(FieldSystem *fs, int seated);
extern void openmmo_cries_debug_tick(int settled);
extern void openmmo_poly_overflow_tick(void); /* openmmo_debug.c */

/* SELECT is the tester's door, warps to the Contest Hall
 * and the Underground, and the story flags either of them reads. */
extern void openmmo_debug_attach(openmmo_client *c);
extern void openmmo_underground_attach(openmmo_client *c);
extern void openmmo_fishing_attach(openmmo_client *c);           /* openmmo_fishing.c */
extern void openmmo_apricorn_attach(openmmo_client *c);          /* openmmo_apricorn.c */
extern void openmmo_apricorn_tick(FieldSystem *fs);
extern void openmmo_fieldmove_attach(openmmo_client *c);         /* openmmo_fieldmove.c */
extern void openmmo_card_attach(openmmo_client *c);              /* openmmo_card.c */
extern void openmmo_look_attach(openmmo_client *c);              /* openmmo_look.c */
extern int openmmo_look_body_gfx(int gfx, int gender);           /* openmmo_look.c */
extern int openmmo_look_state_gfx(int look, int playerState);    /* openmmo_look.c */
extern void openmmo_fieldmove_tick(FieldSystem *fs);
extern int openmmo_encounter_field_busy(const FieldSystem *fs);  /* openmmo_encounter.c */
extern void openmmo_encounter_defer(int foe_species, int foe_level);
extern int openmmo_encounter_deferred(void);
extern void openmmo_fishing_tick(FieldSystem *fs, openmmo_client *c);
extern int openmmo_fishing_take_battle(FieldSystem *fs, const openmmo_client *c,
                                       int foe_species, int foe_level);
extern void openmmo_debug_request_open(void);
extern int openmmo_debug_try_open(FieldSystem *fs);

/* The two engine answers it gives are static in that
 * file, so a field going away has to say so. */
extern void openmmo_underground_forget(void);
extern int openmmo_underground_active(void);
extern void openmmo_underground_tick(int latency_ms);
extern void openmmo_underground_note_step(int running);

/* The engine's own save, out to the file the front door
 * named, so a session can be carried on with no server. The ask is held until
 * the field is standing still, so the tick is what answers it. */
extern void openmmo_offline_export_request(const char *character);
extern int openmmo_offline_export_tick(openmmo_client *c);
/* A save read on the way out, and one offered on the way in.
 * Answers 1 on the frame the session should end on a save the server took. */
extern int openmmo_import_tick(openmmo_client *c);
/* The play-time clock, wound from the host's clock
 * because the port fires no timer for the engine's own counter. */
extern void openmmo_playtime_tick(void);

/* The engine's own sprite-archive answers, dumped for the
 * client's copy of that arithmetic to be checked against. Off unless asked for,
 * and asked before the client exists because it needs no session. */
extern void openmmo_sprite_dump_once(void);
extern void openmmo_sprite_bind_overlay(void);

static void appearance_dump_once(void)
{
    static int done;
    const char *mode = getenv("OPENMMO_APPEARANCE_DUMP");
    int state, gender;

    if (done || mode == NULL || mode[0] == '\0' || mode[0] == '0')
        return;
    done = 1;

    printf("appearance-dump: gender x state sprites\n");
    for (gender = 0; gender < 2; gender++) {
        for (state = 0; state <= 2; state++) {
            printf("appearance-dump: gender %d state %d gfx %d\n",
                   gender, state, Player_GetSpriteFromStateAndGender(state, gender));
        }
    }
    printf("appearance-dump: PLAYER_M %d PLAYER_F %d DP_M %d DP_F %d HIKER %d LASS %d\n",
           OBJ_EVENT_GFX_PLAYER_M, OBJ_EVENT_GFX_PLAYER_F,
           OBJ_EVENT_GFX_DP_PLAYER_M, OBJ_EVENT_GFX_DP_PLAYER_F,
           OBJ_EVENT_GFX_HIKER, OBJ_EVENT_GFX_LASS);
    fflush(stdout);
}

/* pc/patches/src/game_start.c.patch: the game's own new-game save init
 * (StartNewSave) behind a non-static wrapper. Reused rather than restated so
 * the start location, trainer id, appearance and the INIT_NEW_GAME flag script
 * are all correct by construction, the same path the port's save lab boots. */
extern void pc_lab_start_new_save(SaveData *saveData);
extern void pc_lab_start_map_change(FieldSystem *fieldSystem, const Location *location);

/*
 * The session lives for the whole process. The handle is a single pointer in the mod's own BSS,
 * not an engine named heap and not overlay-backed memory, so a map change or a battle
 * transition, which free field-scoped heaps, never touch it.
 */
static openmmo_client *g_client;

/* Set once a session has actually been started. The HUD draws only while this is
 * true, so a boot with no server configured is byte-for-byte the vanilla port, 
 * the overlay never perturbs the frame until there is a connection to report. */
static int g_started;
static int g_guild_log_asked;

/* The last failure/teardown message, shown under the status while the session is
 * FAILED or DISCONNECTED (2.10's typed teardown paths land here). */
static char g_message[128];

/* The page the window and the launcher read this session's state off (status_channel.h). */
static struct openmmo_status_shm *g_status;
static uint32_t g_status_flags;

/*
 * Whether the connection state is also painted into the guest frame. It is the no-field
 * surface, title, lobby, a dropped session.
 */
static int g_hud_on = 1;

/*
 * The on-screen keyboard, the client's free-text field (osk.c), drawn on the lower (touch)
 * screen and driven by the pad, the pen, and the host keyboard on the .text page.
 */
static osk_state g_osk;
static int       g_osk_on;
static mmo_entry_surface g_osk_surface = MMO_ENTRY_CHAT;

/* The character list / creator a person meets, drawn on the lobby
 * after the title. The title is only the splash. */
static mmo_creator g_creator;
static int         g_creator_osk;
static int         g_in_lobby;
static int         g_leave_lobby;
static int         g_leave_field;

/* Seat the avatar on the server's map. Set when JOINED names a tile;
 * title_exit (named character) or the lobby (the pick path) consumes it. */
static Location g_seat;
static int      g_have_seat;
static int      g_leave_title;

/* The chat window a joined session draws. Hidden on the title. */
static mmo_chatwin g_chat;
/* Set for as long as the engine keyboard is up for a chat line, so START does
 * not toggle underneath it and nothing of ours paints over it. */
static int         g_chat_entry;
/* A defocused line. Clicking out of the box, Escape and START all put the
 * field away without eating what was typed, the draft is poured back in
 * the next time the field opens. Only a send clears it. */
static uint16_t    g_chat_draft[OSK_MAX_TEXT];
static size_t      g_chat_draft_len;
static char        g_whisper_name[OPENMMO_ENTITY_NAME_MAX];
/* The channel a composed line goes out on, a wire MMO_CHAT_* the window's
 * active tab picks (OPENMMO_HUD_CMD_CHANNEL). NORMAL until told anything,
 * which is also every headless session. */
static int         g_chat_send_type = MMO_CHAT_NORMAL;
/* Who the Whispers channel replies to: the last player whispered at or heard
 * from. Empty until a whisper happens; a reply with nobody to go to is sent
 * with an empty target so the server's own "Whisper who?" lands in the log. */
static char        g_last_whisper[OPENMMO_ENTITY_NAME_MAX];

/* The window's typing page, attached when it exists. The window creates it;
 * we never do, a headless boot has no typist and must not leave a page
 * behind. g_text_tail is ours, so a client stopped in a debugger costs the
 * window nothing. */
static struct openmmo_text_shm *g_text;
static uint32_t                 g_text_tail;

/* OPENMMO_FAKE_ENTITY: inject a synthetic remote entity through the real render
 * glue so the avatar path can be seen without a second live player. Default off. */
static int g_fake_entity;

/* OPENMMO_FAKE_CROWD=N: the same injection, N at once, so the overworld's crowd
 * ceilings can be measured without N players. Default 0 = off. */
static int g_fake_crowd;

/*
 * OPENMMO_FAKE_FOLLOWERS: give the synthetic crowd a Pokemon each, one distinct species per
 * slot, so the peer-follower half and both of its culls can be driven without twelve players
 * carrying twelve different parties.
 */
static int g_fake_followers;

/* OPENMMO_FAKE_NAME: the name a synthetic peer is labelled with, numbered per
 * slot. NULL (unset) leaves the default "Player"; empty is an empty name, so
 * the label path's visible fallback can be driven without a server. Only
 * reaches the fake paths, a real peer's name comes off its spawn packet. */
static const char *g_fake_name;

/*
 * A server-started battle the window cannot present yet. The overworld stays up; this hold is
 * the visible state and the run intent is how it ends.
 */
static int g_battle_ran;
static int g_battle_hold_frames;
static int g_battle_wild;

/* OPENMMO_TESTBATTLE: after join, ask the server for a wild fight so a
 * headless boot can watch the hold. Sends twice, the second is the
 * proof the first was settled. Default off. */
static int g_testbattle;
static int g_testbattle_delay;
static int g_testbattle_round;

#define BATTLE_HOLD_RUN_FRAMES 120
#define TESTBATTLE_ASK_DELAY   60
#define TESTBATTLE_ROUNDS      2

/* How many fights OPENMMO_TESTBATTLE asks for. A numeric value in the env
 * var is the round count, so a probe that wants one fight and a settled
 * aftermath can say so; any other on-value keeps the two-round default. */
static int testbattle_rounds(void)
{
    const char *t = openmmo_dev_env("OPENMMO_TESTBATTLE");
    int n;

    if (t == NULL)
        return TESTBATTLE_ROUNDS;
    n = atoi(t);
    return n > 0 ? n : TESTBATTLE_ROUNDS;
}

/* Host-surface fill. The HUD and the no-device keyboard paint ROM-font glyphs
 * on top of these bars; the clip at PC_VIDEO_WIDTH is why they cannot reach a
 * wide frame's extra columns (ENGINE_LIMITS §10). */
static void fill_rect(uint32_t *surf, int x, int y, int w, int h, uint32_t color)
{
    int px, py;

    for (py = y; py < y + h; py++) {
        if (py < 0 || py >= PC_VIDEO_HEIGHT)
            continue;
        for (px = x; px < x + w; px++) {
            if (px < 0 || px >= PC_VIDEO_WIDTH)
                continue;
            surf[py * PC_VIDEO_WIDTH + px] = color;
        }
    }
}

/* A line of ROM-font text with a dark bar behind it, so it reads over any
 * picture. Quiet if FONT_SYSTEM is not loaded yet, NitroMain initialises it
 * before the first VBlank, and the HUD only paints after a session starts. */
static void draw_line(uint32_t *surf, int x, int y, const char *text,
                      uint32_t color)
{
    int w, h;

    if (text == NULL || !openmmo_font_ready())
        return;
    w = openmmo_font_width_utf8(text);
    h = openmmo_font_cell_h();
    fill_rect(surf, x - 1, y - 1, w + 2, h + 2, 0x00101018u);
    openmmo_font_draw_utf8(surf, x, y, text, color);
}

/* State colours: grey idle, amber in-flight, cyan authed, green in-world, red
 * failed, so the state reads at a glance before the word is even parsed. */
static uint32_t status_color(openmmo_status st)
{
    switch (st) {
    case OPENMMO_DISCONNECTED:    return 0x00808080u;
    case OPENMMO_AUTHED:          return 0x0040D0FFu;
    case OPENMMO_IN_GAME:         return 0x0040FF60u;
    case OPENMMO_FAILED:          return 0x00FF5050u;
    default:                      return 0x00FFD040u;
    }
}

/*
 * The renderer wrapper. pc_gpu2d_render fills the surfaces from guest state; this then paints
 * the HUD on top of the upper screen, before the frame is published or dumped.
 */
#define OPENMMO_RENDERER_NAME "openmmo-hud"

/* Which surface is the lower (touch) screen, the one the OSK draws into and the
 * one the pen coordinates address. pc_video_upper_engine() names the top; the
 * other engine is the bottom. */
static int lower_engine(void)
{
    return pc_video_upper_engine() == PC_VIDEO_MAIN ? PC_VIDEO_SUB : PC_VIDEO_MAIN;
}

/* Paint the keyboard and the line typed so far into the lower-screen surface.
 * Geometry and labels come straight from the widget, so the drawn cell and the
 * pen hit-test share one source of truth. */
static void draw_osk(void)
{
    uint32_t *surf = pc_video_surface(lower_engine());
    int i, n;
    char typed[OSK_MAX_TEXT + 1];
    size_t t, tn;

    if (surf == NULL)
        return;

    n = osk_key_count(&g_osk);
    for (i = 0; i < n; i++) {
        osk_key_view v;
        uint32_t bg, fg;

        osk_get_key(&g_osk, i, &v);
        bg = v.selected ? 0x00305888u : 0x00181820u;
        fg = v.selected ? 0x00FFFFFFu : 0x00B0B0C0u;
        fill_rect(surf, v.x, v.y, v.w, v.h, bg);
        if (openmmo_font_ready()) {
            int tw = openmmo_font_width_utf8(v.label);
            int th = openmmo_font_cell_h();
            int tx = v.x + (v.w - tw) / 2;
            int ty = v.y + (v.h - th) / 2;

            openmmo_font_draw_utf8(surf, tx, ty, v.label, fg);
        }
    }

    /* The typed line, low byte of each unit (the layout is ASCII). */
    tn = osk_text_len(&g_osk);
    if (tn > OSK_MAX_TEXT)
        tn = OSK_MAX_TEXT;
    for (t = 0; t < tn; t++)
        typed[t] = (char)(g_osk.text[t] & 0x7F);
    typed[tn] = '\0';
    draw_line(surf, 4, 8, tn ? typed : "TYPE...", 0x0040FF60u);
}

static int creator_live(void)
{
    return g_in_lobby && g_creator.step != MMO_CREATOR_HIDDEN;
}

/* Defined below, beside the avatar cache it walks. */
static void draw_labels(uint64_t frame);

/* --- the duel offer ------------------------------------------------------ * */
static char g_duel_name[OPENMMO_ENTITY_NAME_MAX];
static int g_duel_offered;
static int g_duel_asked;

/* OPENMMO_DUEL=<name>, and the frames to wait after a join before sending it. */
static const char *g_duel_target;
static int g_duel_delay = -1;

/* OPENMMO_TRADE_WITH=<name>: the trade's own env door, the same shape. */
static const char *g_trade_target;
static int g_trade_delay = -1;
#define DUEL_ASK_DELAY 120

static void duel_answered(int yes)
{
    g_duel_offered = 0;
    g_duel_asked = 0;
    if (g_client == NULL)
        return;
    printf("openmmo: duel %s\n", yes ? "accepted" : "declined");
    openmmo_client_reply_duel(g_client, yes);
}

static void drive_duel_offer(FieldSystem *fs)
{
    char line[OPENMMO_ENTITY_NAME_MAX + 40];

    if (!g_duel_offered || g_duel_asked || g_client == NULL)
        return;
    /* The server drops the offer on its own clock; if it has already gone,
     * so has the question. */
    if (openmmo_client_duel_pending(g_client) == NULL) {
        g_duel_offered = 0;
        return;
    }
    snprintf(line, sizeof line, "%s wants to battle!",
             g_duel_name[0] ? g_duel_name : "Someone");
    if (openmmo_dialog_ask_local(fs, line, duel_answered))
        g_duel_asked = 1;
}
static void draw_chat(void);

static void openmmo_render(uint64_t frame)
{
    pc_gpu2d_render(frame);

    /* An instrument, off unless asked for: the state a screen that printed in
     * another colour leaves the glyph table in. Everything of ours below sets
     * the table for itself, and this is what says so out loud. */
    openmmo_glyph_clobber();

    /* Before the HUD, so a status line drawn at the top of the screen is never
     * covered by a nameplate that happens to project under it, and after the
     * engine's renderer, so the 3D transform the labels are projected with is
     * the one this frame was drawn by. */
    draw_labels(frame);
    /* The window's own copy of those plates, published in the frame that
     * projected them. */
    openmmo_hud_publish_plates();

    if (g_osk_on || g_creator_osk
        || (mmo_chatwin_composing(&g_chat) && !openmmo_hud_windowed()))
        draw_osk();

    if (!openmmo_hud_windowed())
        draw_chat();
    if (g_client != NULL) {
        const openmmo_guild *g = openmmo_client_guild(g_client);

        /* Join seeds 0x80 silently, so EV_GUILD is not the only
         * chance to ask for the log the glance shows. One request
         * until a page arrives or we leave. */
        if (g->in_guild && !g->log_valid && !g_guild_log_asked) {
            g_guild_log_asked = 1;
            (void)openmmo_client_guild_log(g_client, 0);
        }
        if (!g->in_guild)
            g_guild_log_asked = 0;
    }

    if (g_in_lobby)
        return;

    /*
     * The NET line, wherever the session stands: the poketch app that used to carry it on the
     * field is gone with the poketch mods.
     */
    if (g_client != NULL && g_started && g_hud_on
        && !openmmo_hud_windowed()) {
        openmmo_status st = openmmo_client_status(g_client);
        uint32_t *surf = pc_video_surface(pc_video_upper_engine());
        char line[40];
        const char *name = openmmo_status_name(st);
        size_t k = strlen(name);

        if (k > sizeof line - 6)
            k = sizeof line - 6;
        memcpy(line, "NET: ", 5);
        memcpy(line + 5, name, k);
        line[5 + k] = '\0';
        draw_line(surf, 4, 4, line, status_color(st));

        if ((st == OPENMMO_FAILED || st == OPENMMO_DISCONNECTED) && g_message[0] != '\0')
            draw_line(surf, 4, 22, g_message, status_color(st));
        else if (st == OPENMMO_IN_GAME && g_client != NULL) {
            openmmo_battle_state battle = openmmo_client_battle_state(g_client);

            if (battle == OPENMMO_BATTLE_ENTERING)
                draw_line(surf, 4, 22, "BATTLE...", 0x00FFD040u);
            else if (battle == OPENMMO_BATTLE_ACTIVE
                     && !openmmo_encounter_scene_up())
                draw_line(surf, 4, 22,
                          g_battle_wild ? "IN BATTLE - B TO RUN"
                                        : "FIGHT A/X/Y/L  B FORFEIT",
                          0x00FF8040u);
        }
    }
}

void openmmo_save_session_prepare(void); /* openmmo_save.c */

/*
 * Runs before the engine's main(). Allocating the client here is the cheapest honest proof
 * that libopenmmo is linked, callable and ABI-compatible in this process, and it opens no
 * socket, so a boot with no server behaves like any other boot.
 */
__attribute__((constructor(101))) static void openmmo_announce(void)
{
    /* The launcher points stdout at the playthrough log; without this the
     * trace sits in a stdio block until the buffer happens to fill, and the
     * line that explains a crash is the one still buffered when it dies. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Before the guest map is claimed, which is the earliest failure this
     * program has and one a player has never been able to report: a window
     * that closed. The report lands in the logs folder beside the trace above
     * (platform.h). */
    mmo_plat_crash_install("client");

    /* The guest'S addresses are claimed before this file allocates anything. */
    /* The priority on this constructor and the release below are one mechanism. */
    if (mmo_plat_guest_release() != 0)
        printf("openmmo: the guest range was not held: %s\n",
               mmo_plat_guest_hold_status());

    if (armrec_mem_init() != 0) {
        printf("openmmo: the guest memory map could not be claimed: %s\n",
               armrec_mem_strerror());
        /*
         * That message names the address that was refused and not the thing holding it, which
         * is the whole of what makes this failure hard: it is the same line whether the
         * offender is a thread stack, a section or a DLL, and only the last of those has ever
         * been identified by reading.
         */
        printf("openmmo: the entry point: %s\n", mmo_plat_guest_hold_status());
        mmo_plat_map_report("the guest map was refused; who holds it:",
                            0x00010000UL, 0x0B000000UL);
        return;
    }

    openmmo_save_session_prepare();

    g_client = openmmo_client_new();

    if (g_client == NULL) {
        printf("openmmo: " MMO_CLIENT_VERSION " linked, but the client would not allocate\n");
        return;
    }

    printf("openmmo: %s %s in-process, status %s\n",
        MMO_CLIENT_NAME,
        MMO_CLIENT_VERSION,
        openmmo_status_name(openmmo_client_status(g_client)));
    /*
     * What this RUN was actually handed, because a display setting that does not seem to be
     * doing anything is a question the log could not answer: the port only names --hd3d when
     * the machine cannot keep up with it, so a run at the wrong internal resolution and a run
     * at the right one read the same.
     */
    {
        const char *hd = getenv("PC_HD3D");
        const char *asp = getenv("PC_ASPECT");
        const char *pace = getenv("PC_PACE");

        printf("openmmo: host display, hd3d %s, aspect %s, pace %s\n",
               hd != NULL && hd[0] != '\0' ? hd : "1 (unset)",
               asp != NULL && asp[0] != '\0' ? asp : "native (unset)",
               pace != NULL && pace[0] != '\0' ? pace : "1 (unset)");
    }
    openmmo_bag_attach(g_client);
    openmmo_dialog_attach(g_client);
    openmmo_script_move_attach(g_client);
    openmmo_mail_attach(g_client);
    openmmo_union_attach(g_client);
    openmmo_trade_attach(g_client);
    openmmo_contest_link_attach(g_client);
    openmmo_contest_lab_attach(g_client);
    openmmo_gtl_attach(g_client);
    openmmo_player_attach(g_client);
    openmmo_widget_attach(g_client);
    openmmo_travel_attach(g_client);
    openmmo_debug_attach(g_client);
    openmmo_underground_attach(g_client);
    openmmo_fishing_attach(g_client);
    openmmo_apricorn_attach(g_client);
    openmmo_fieldmove_attach(g_client);
    openmmo_card_attach(g_client);
    openmmo_look_attach(g_client);
}

/*
 * A session is wanted when OPENMMO_SESSION says so. That is the play path: the window joins,
 * the title says our name, and Continue / New Game is not offered, because a session has no
 * player save.
 */
static int session_configured(void)
{
    const char *s = getenv("OPENMMO_SESSION");

    return s != NULL && s[0] != '\0' && s[0] != '0';
}

/*
 * PLAY OFFLINE's front door is this game's character select, not Platinum's title (the owner's
 * ask, 2026-09-08: "exactly like the menu from OpenMMO, just without 'new character'").
 */
static int g_offline_lobby;

/* And which of the two the player picked, because they end in different engine
 * applications: a saved game continues, a new one runs the cartridge's own
 * opening. */
static int g_offline_new_game;

/* Whether the front door pressed PLAY OFFLINE. */
static int offline_row(void)
{
    const char *s = getenv("OPENMMO_OFFLINE");

    return s != NULL && s[0] != '\0' && s[0] != '0';
}

/* The list is seated, so leave the title for it without waiting to be asked. */
static void offline_lobby_taken(void)
{
    g_offline_lobby = 1;
    g_leave_lobby = 1;
}

/* One row: the character on the chip, or NEW GAME where there is none. */
static int offline_lobby_seat(SaveData *saveData)
{
    mmo_character_list list;
    TrainerInfo *info = NULL;
    char why[64];

    if (saveData == NULL || session_configured() || !offline_row())
        return 0;

    memset(&list, 0, sizeof list);
    if (SaveData_DataExists(saveData))
        info = SaveData_GetTrainerInfo(saveData);

    mmo_creator_reset(&g_creator);
    g_offline_new_game = 0;

    if (info == NULL) {
        /*
         * Nothing has been played or carried out here yet. One row, NEW GAME, and A on it runs
         * the cartridge's own opening, which is where the name and the gender are asked, so
         * the creator's four steps are not.
         */
        mmo_creator_direct_new(&g_creator, 1);
        mmo_creator_set_list(&g_creator, &list);
        offline_lobby_taken();
        printf("openmmo: offline character select: nothing saved here, so NEW GAME\n");
        return 1;
    }

    list.count = 1;
    list.held = 1;
    list.entry[0].id = (s64)TrainerInfo_ID(info);
    list.entry[0].gender = (int)TrainerInfo_Gender(info);
    list.entry[0].region = MMO_REGION_SINNOH;
    mmo_charcode_to_utf8((const mmo_charcode *)TrainerInfo_Name(info),
                         list.entry[0].name, sizeof list.entry[0].name);

    mmo_creator_fix_list(&g_creator, 1);
    mmo_creator_set_list(&g_creator, &list);
    offline_lobby_taken();
    printf("openmmo: offline character select: \"%s\" (%s)\n",
           list.entry[0].name[0] ? list.entry[0].name : "?",
           list.entry[0].gender == 1 ? "girl" : "boy");

    /*
     * The row is shown either way, "your character, and here is why it will not open" is a
     * better screen than an empty one, but a save naming a species the loaded packages
     * cannot draw is refused here rather than crashing later.
     */
    if (openmmo_offline_save_unsupported(saveData, why, sizeof why) != 0) {
        printf("openmmo: this save cannot be opened: %s\n", why);
        mmo_creator_refuse(&g_creator, why);
    }
    return 1;
}

/* Boot straight into a map, skipping the title and the Rowan intro. */
int openmmo_boot_into_world(void *saveDataVoid)
{
    const char *s = openmmo_dev_env("OPENMMO_BOOT_WORLD");

    if (s == NULL || s[0] == '\0' || s[0] == '0')
        return 0;

    pc_lab_start_new_save((SaveData *)saveDataVoid);
    openmmo_poketch_seat((SaveData *)saveDataVoid, NULL);
    printf("openmmo: booting straight into the overworld\n");
    return 1;
}

/* Which application NitroMain should enqueue. */
int openmmo_boot_kind(void *saveDataVoid)
{
    openmmo_sprite_bind_overlay();

    if (openmmo_boot_into_world(saveDataVoid))
        return 1;
    if (session_configured()) {
        printf("openmmo: session title\n");
        return 2;
    }
    if (offline_lobby_seat(saveDataVoid)) {
        printf("openmmo: offline title\n");
        return 2;
    }

    return 0;
}

/* Replace the title's PRESS START string with the game's name. No-op on a
 * plain boot, no session and no offline save, so the port's own title,
 * which is where NEW GAME lives, stays the port's. */
void openmmo_title_name(void *stringVoid)
{
    String *s = stringVoid;
    mmo_charcode buf[16];

    if ((!session_configured() && !g_offline_lobby) || s == NULL)
        return;
    mmo_utf8_to_charcode("OPENMMO", buf, sizeof buf / sizeof buf[0]);
    String_Clear(s);
    String_CopyChars(s, (const charcode_t *)buf);
}

/*
 * Stay on our title: do not walk Continue / New Game, do not clear the save, do not replay the
 * opening. A / Start, the clear-save combo and the opening replay all become the lobby
 * instead.
 */
#define OPENMMO_TITLE_START_MENU 0
#define OPENMMO_TITLE_CLEAR_SAVE 1
#define OPENMMO_TITLE_REPLAY     2

int openmmo_title_hold(int exit)
{
    if (!session_configured()) {
        if (!g_offline_lobby || exit == OPENMMO_TITLE_CLEAR_SAVE)
            return 0;
        if (exit == OPENMMO_TITLE_START_MENU)
            g_leave_lobby = 1;
        return 1;
    }
    if (!g_have_seat)
        g_leave_lobby = 1;
    return 1;
}

/* The join named a map and a tile. Title_Main asks this every frame;
 * once it is true the title fades out and Title_Exit seats the field
 * (named character) or the lobby (the pick path). */
static int g_seated;

/*
 * The pending seat came from a server warp (0xE_ MapTransition + LoadMap), not from a desync
 * correction.
 */
static int g_seat_reload;

/*
 * A STEP onto a tile the ENGINE WARPS from, an exit mat, a stair, an escalator, is
 * answered twice.
 */
static int g_local_landing_header = -1;
static int g_local_landing_frames;
#define LOCAL_LANDING_FRAMES 240
/* What the server said the player was riding when that report went out. While
 * this still matches, the report has not been answered and the mount the engine
 * has is newer than the one the server is talking about (apply_local_mount). */
static int g_landing_mount;
static int local_is_surfing(void);
/*
 * And the third order: the engine started its own transition and the server's answer arrives
 * while it is still in flight.
 */
static int g_engine_warp_header = -1;
static int g_engine_warp_frames;
/* The seat the engine's own transition is fulfilling. */
static int g_seat_engine;

/*
 * The join's character, written into the save the engine will show. Gender has to land before
 * FieldMapChange_CreateObjects reads it; the id is rewritten after GameStartNewSave rolls its
 * own.
 */
static char s_player_name[33];
static s64 s_player_id;
static int s_player_gender = -1;
static int s_pending_gfx = -1;
static int s_player_announced;
static int s_money_seat;

/*
 * The cash field is the server's while a session has seated it. Give/Take/Set in the engine
 * are the local shop, script and mint paths; they must not change the number the server just
 * sent.
 */
int openmmo_trainer_money_may_write(void)
{
    const openmmo_world_state *ws;

    if (s_money_seat)
        return 1;
    if (g_client == NULL)
        return 1;
    ws = openmmo_client_world_state(g_client);
    if (ws == NULL || !ws->valid)
        return 1;
    return 0;
}

/*
 * The gates Give/Take call. Both report the delta and let the write proceed, refusing was the
 * mart selling nothing and buying for free, the same lesson the bag gates learned the same
 * afternoon.
 */
static int money_report(int delta)
{
    const openmmo_shop *shop;

    if (openmmo_trainer_money_may_write())
        return 1;
    shop = openmmo_client_shop(g_client);
    if (shop == NULL || !shop->valid || !shop->open) {
        if (openmmo_client_send_money_delta(g_client, delta) == 0)
            printf("openmmo: money %s by %d, telling the server\n",
                   delta < 0 ? "spent" : "earned", delta < 0 ? -delta : delta);
    }
    return 1;
}

int openmmo_trainer_money_gate_give(u32 amount)
{
    return money_report((int)amount);
}

int openmmo_trainer_money_gate_take(u32 amount)
{
    return money_report(-(int)amount);
}

/*
 * Not static any more: openmmo_offline.c seats the OFFLINE wallet over the online one for the
 * length of the export save, because the two homes keep separate wallets and the image this
 * session writes is about to become the offline game.
 */
void openmmo_seat_trainer_money(TrainerInfo *info, s32 money)
{
    u32 value;

    if (info == NULL)
        return;
    value = money < 0 ? 0u : (u32)money;
    s_money_seat = 1;
    TrainerInfo_SetMoney(info, value);
    s_money_seat = 0;
    printf("openmmo: money %u\n", TrainerInfo_Money(info));
}

/* The rival's name, into the block the engine's own BufferRivalName reads. */
void openmmo_rival_name_default(SaveData *save)
{
    static int announced;
    MiscSaveBlock *misc;
    const charcode_t *have;
    mmo_charcode buf[TRAINER_NAME_LEN + 2];
    String *name;

    if (save == NULL)
        return;
    misc = SaveData_MiscSaveBlock(save);
    if (misc == NULL)
        return;
    have = MiscSaveBlock_RivalName(misc);
    if (have != NULL && have[0] != MMO_CHAR_EOS)
        return;
    name = String_Init(TRAINER_NAME_LEN + 2, HEAP_ID_APPLICATION);
    if (name == NULL)
        return;
    mmo_utf8_to_charcode(OPENMMO_RIVAL_NAME, buf,
                         (int)(sizeof buf / sizeof buf[0]));
    String_CopyChars(name, (const charcode_t *)buf);
    MiscSaveBlock_SetRivalName(misc, name);
    String_Free(name);
    if (!announced) {
        announced = 1;
        printf("openmmo: rival is %s\n", OPENMMO_RIVAL_NAME);
    }
}

/* The badges the server says this character has earned, into TrainerInfo. */
static void seat_badges(SaveData *save)
{
    const openmmo_world_state *ws = openmmo_client_world_state(g_client);
    TrainerInfo *info;
    int i, seated = 0;

    if (ws == NULL || !ws->valid || save == NULL)
        return;
    info = SaveData_GetTrainerInfo(save);
    if (info == NULL)
        return;
    for (i = 0; i < ws->badge_count && i < MMO_WS_BADGE_MAX; i++) {
        int id = ws->badges[i];

        if (id < 0 || id >= 8)
            continue;
        if (!TrainerInfo_HasBadge(info, id)) {
            TrainerInfo_SetBadge(info, id);
            seated++;
        }
    }
    if (seated > 0)
        printf("openmmo: %d badge(s) seated (%d earned)\n", seated, ws->badge_count);
}

static void seat_trainer(SaveData *save)
{
    const openmmo_world_state *ws;
    TrainerInfo *info;
    uint8_t utf16[66];
    mmo_charcode name[33];
    size_t n;

    if (save == NULL || g_client == NULL)
        return;
    ws = openmmo_client_world_state(g_client);
    if (ws == NULL || !ws->valid)
        return;
    if (ws->name[0] == '\0' && ws->character_id == 0)
        return;

    info = SaveData_GetTrainerInfo(save);
    if (info == NULL)
        return;

    if (ws->name[0] != '\0') {
        n = 0;
        while (ws->name[n] != '\0' && n < 32) {
            utf16[n * 2] = (uint8_t)ws->name[n];
            utf16[n * 2 + 1] = 0;
            n++;
        }
        mmo_utf16le_to_charcode(utf16, n * 2, name, 33);
        TrainerInfo_SetName(info, (const charcode_t *)name);
    }
    if (ws->gender == 0 || ws->gender == 1)
        TrainerInfo_SetGender(info, ws->gender);
    /*
     * The trainer id is deliberately not written here. It used to be the low half of the
     * character's entity id, and the server keeps a kind tag in exactly those bits, so every
     * player's card read IDNo 36864.
     */
    openmmo_seat_trainer_money(info, ws->money);
    openmmo_rival_name_default(save);

    if (!s_player_announced
        || s_player_id != ws->character_id
        || s_player_gender != ws->gender
        || strcmp(s_player_name, ws->name) != 0) {
        printf("openmmo: player is %s (%s) id %lld\n",
               ws->name[0] ? ws->name : "?",
               ws->gender ? "female" : "male",
               (long long)ws->character_id);
        snprintf(s_player_name, sizeof s_player_name, "%s", ws->name);
        s_player_id = ws->character_id;
        s_player_gender = ws->gender;
        s_player_announced = 1;
    }
}

int openmmo_title_leave(void)
{
    return g_leave_title || g_leave_lobby;
}

static void seat_and_enter_field(SaveData *saveData)
{
    Location *loc;

    if (saveData == NULL || !g_have_seat)
        return;

    pc_lab_start_new_save(saveData);
    openmmo_poketch_seat(saveData, openmmo_client_script_state(g_client));
    loc = FieldOverworldState_GetPlayerLocation(SaveData_GetFieldOverworldState(saveData));
    *loc = g_seat;
    /* Name and gender must land before GameStartNewSave: it derives the
     * Union Room appearance from gender, and FieldMapChange builds the
     * avatar from TrainerInfo_Gender. */
    seat_trainer(saveData);
    EnqueueApplication(FS_OVERLAY_ID(game_start), &gGameStartNewSaveAppTemplate);
    printf("openmmo: leaving %s for header %d at (%d,%d)\n",
           g_in_lobby ? "lobby" : "title",
           (int)g_seat.mapHeaderID, g_seat.x, g_seat.z);
}

BOOL openmmo_lobby_init(ApplicationManager *appMan, int *state);
BOOL openmmo_lobby_main(ApplicationManager *appMan, int *state);
BOOL openmmo_lobby_exit(ApplicationManager *appMan, int *state);

static const ApplicationManagerTemplate sLobbyApp = {
    openmmo_lobby_init,
    openmmo_lobby_main,
    openmmo_lobby_exit,
    FS_OVERLAY_ID_NONE
};

int openmmo_title_exit(int nextApp, void *saveDataVoid)
{
    SaveData *saveData = saveDataVoid;

    (void)nextApp;
    if (saveData == NULL)
        return 0;

    /* A named character joined while the title was still up: skip the
     * lobby and seat the field the way a CLI join does. */
    if (g_leave_title && g_have_seat) {
        seat_and_enter_field(saveData);
        g_leave_title = 0;
        return 1;
    }

    if (g_leave_lobby) {
        EnqueueApplication(FS_OVERLAY_ID_NONE, &sLobbyApp);
        printf("openmmo: leaving title for character select\n");
        g_leave_lobby = 0;
        return 1;
    }
    return 0;
}

static void creator_submit(void);

mmo_creator *openmmo_lobby_creator(void)
{
    return &g_creator;
}

void openmmo_lobby_commit(void)
{
    creator_submit();
}

int openmmo_lobby_ready_to_field(void)
{
    return g_leave_field && (g_have_seat || g_offline_lobby);
}

void openmmo_lobby_enter_field(SaveData *save)
{
    if (g_offline_lobby) {
        /*
         * Two templates, and the wrong one on either side is the whole of what this branch is
         * for.
         */
        if (g_offline_new_game) {
            EnqueueApplication(FS_OVERLAY_ID(game_start),
                               &gGameStartRowanIntroAppTemplate);
            printf("openmmo: leaving the lobby for a new offline game\n");
        } else {
            EnqueueApplication(FS_OVERLAY_ID(game_start),
                               &gGameStartLoadSaveAppTemplate);
            printf("openmmo: leaving the lobby for the offline save\n");
        }
        g_offline_new_game = 0;
    } else if (g_have_seat)
        seat_and_enter_field(save);
    g_in_lobby = 0;
    g_leave_field = 0;
}

void openmmo_lobby_set_active(int on)
{
    g_in_lobby = on;
}

/*
 * Hide the Platinum wordmark, the grayscale Pokemon logo, and GAME FREAK Presents. The PRESS
 * START layer carries the name we just set.
 */
int openmmo_title_present(void)
{
    return session_configured() || g_offline_lobby;
}

/* Whether this build was asked to talk to a server at all. The one predicate
 * that answers before anything has connected, which is what a caller deciding
 * between the engine's own behaviour and the server's needs, the vanilla port
 * boots through here too and must keep every one of its own answers. */
int openmmo_session_configured(void)
{
    return session_configured();
}

/* Whether the overworld may start a wild encounter on its own. */
int openmmo_local_encounters_enabled(void)
{
    const char *s = openmmo_dev_env("OPENMMO_LOCAL_ENCOUNTERS");

    if (s != NULL && s[0] != '\0')
        return s[0] != '0';

    return !session_configured();
}

/* The server has moved this session into a battle. The window does not
 * present the fight yet (the scene still needs a local emit to pace it),
 * so a step sent now is a walk the server will not take. */
static int session_in_battle(void)
{
    if (g_client == NULL || !g_started)
        return 0;
    return openmmo_client_battle_state(g_client) != OPENMMO_BATTLE_NONE;
}

/* A script owns the avatar. A step now is a leak the server snaps back. */
static int session_in_dialog(void)
{
    if (g_client == NULL || !g_started)
        return 0;
    return openmmo_client_in_dialog(g_client);
}

/*
 * Local warps, field-task scripts and on-frame cutscenes are the engine's, session or no
 * session.
 */
int openmmo_local_warps_enabled(void)
{
    const char *s = openmmo_dev_env("OPENMMO_LOCAL_WARPS");
    if (s != NULL && s[0] != '\0')
        return s[0] != '0';
    return 1;
}

int openmmo_local_field_scripts_enabled(void)
{
    const char *s = openmmo_dev_env("OPENMMO_LOCAL_SCRIPTS");
    if (s != NULL && s[0] != '\0')
        return s[0] != '0';
    return 1;
}

/*
 * Whether a trainer who sees the player may start a fight without the server. On, and it has
 * been since the party seat became faithful.
 */
int openmmo_local_trainer_battles_enabled(void)
{
    const char *s = openmmo_dev_env("OPENMMO_LOCAL_TRAINERS");
    if (s != NULL && s[0] != '\0')
        return s[0] != '0';
    return 1;
}

/*
 * The Underground's own scripts are comm scripts: the map's on-frame table reaches
 * ScrCmd_SetCommPlayerDir on its first frame, and every one of that family dereferences the
 * CommPlayerManager the DS comm boot allocates.
 */
static int script_would_be_underground(void)
{
    FieldSystem *fs = pc_lab_field_system();

    /* Asked of the MAP, not of the descent. */
    if (fs != NULL && fs->location != NULL
        && (int)fs->location->mapHeaderID == MAP_HEADER_UNDERGROUND)
        return 1;
    return openmmo_underground_active();
}

int openmmo_allow_local_warp(int header)
{
    if (openmmo_local_warps_enabled()) {
        printf("openmmo: local warp to header %d\n", header);
        /* The server is answering the step onto this tile at the same time;
         * from here until the landing, its answer is this warp, not another. */
        g_engine_warp_header = header;
        g_engine_warp_frames = LOCAL_LANDING_FRAMES;
        return 1;
    }
    printf("openmmo: refused local warp to header %d\n", header);
    return 0;
}

/* A local scene may move the player without the step hook hearing it; the
 * settle after any local script reports where it left us (see
 * report_scene_position below). */
static int g_scene_ran;

/*
 * The other half of the same settle: a local scene may also FIGHT, and the party it left
 * behind is the server's to record.
 */
static int g_scene_fought;

/* A map this client did not ship. The porter appends a ported header after the
 * engine's own last one, so 594 is the first (tools/portmap.py). Three places
 * ask: the sight scan, the script gate, and the A press. */
#define OPENMMO_PORTED_HEADER_FIRST 594

static int on_ported_map(const FieldSystem *fs)
{
    return fs != NULL && fs->location != NULL
        && fs->location->mapHeaderID >= OPENMMO_PORTED_HEADER_FIRST;
}

/* The same question for the engine's script commands, which have no field
 * system in hand (openmmo_card.c's badge read). */
int openmmo_on_ported_map(void)
{
    return on_ported_map(pc_lab_field_system());
}

/*
 * WHO PLAYS A TRAINER ENCOUNTER. Nobody is blocked any more, and the reason this hook exists
 * at all is worth keeping.
 */
int openmmo_trainer_sight_blocked(FieldSystem *fieldSystem)
{
    (void)fieldSystem;
    return 0;
}

int openmmo_allow_local_script(unsigned scriptID)
{
    if (script_would_be_underground()) {
        printf("openmmo: refused underground script %u\n", scriptID);
        return 0;
    }
    if (openmmo_local_field_scripts_enabled()) {
        /*
         * 3000..6999 used TO be refused here and are not any more. They are the engine's own
         * single- and double-battle ranges, where an object keeps a trainer number rather than
         * a script and the loader turns it into an entry of `scripts_battles`.
         */
        printf("openmmo: local script %u\n", scriptID);
        g_scene_ran = 1;
        g_scene_fought = 1;
        return 1;
    }
    printf("openmmo: refused local script %u\n", scriptID);
    return 0;
}

/* A-press on a sign or npc. Coord events also refuse a local script;
 * those must not send this. */
static int openmmo_remote_at(FieldSystem *fs, int x, int z, char *name, int cap);

/* Whether a miss is reported. An A press in the open field is frequent enough
 * that the line only earns its place while someone is asking why a plate did
 * not answer: OPENMMO_INTERACT_REPORT=1. */
static int interact_report(void)
{
    static int on = -1;

    if (on < 0) {
        const char *s = getenv("OPENMMO_INTERACT_REPORT");
        on = (s != NULL && s[0] != '\0' && s[0] != '0');
    }
    return on;
}

/*
 * The tile the player faces, asked about before either half's script path sees the A press: a
 * remote player standing there is a person rather than a script, and the menu they open is
 * this client's own.
 */
int openmmo_field_interact_peer(void)
{
    FieldSystem *fs;
    int px, pz, dir;
    char name[OPENMMO_ENTITY_NAME_MAX];

    if (g_client == NULL || !g_started)
        return 0;
    if (openmmo_client_in_dialog(g_client))
        return 0;
    fs = pc_lab_field_system();
    if (fs == NULL || fs->playerAvatar == NULL
        || !FieldSystem_IsRunningFieldMap(fs)
        || fs->task != NULL || FieldSystem_HasChildProcess(fs))
        return 0;

    px = PlayerAvatar_GetXPos(fs->playerAvatar);
    pz = PlayerAvatar_GetZPos(fs->playerAvatar);
    dir = PlayerAvatar_GetFacingDir(fs->playerAvatar);
    if (!openmmo_remote_at(fs, px + MapObject_GetDxFromDir(dir),
                           pz + MapObject_GetDzFromDir(dir),
                           name, (int)sizeof name)) {
        /* Whoever is on that tile, named. A press that does nothing is either
         * a tile with nobody on it or a person the engine did not build, and
         * only this line tells the two apart. */
        if (interact_report()) {
            MapObject *self = PlayerAvatar_GetMapObject(fs->playerAvatar);
            const MapObjectManager *man =
                self != NULL ? MapObject_MapObjectManager(self) : NULL;
            MapObject *faced = man != NULL
                ? sub_0206326C(man, px + MapObject_GetDxFromDir(dir),
                               pz + MapObject_GetDzFromDir(dir), 0)
                : NULL;

            printf("openmmo: A at (%d,%d) dir %d on header %d, no player"
                   " on that tile; engine object %s", px, pz, dir,
                   fs->location != NULL ? (int)fs->location->mapHeaderID : -1,
                   faced != NULL ? "yes" : "no");
            if (faced != NULL)
                printf(", script %u, trainer type %u",
                       (unsigned)MapObject_GetScript(faced),
                       (unsigned)MapObject_GetTrainerType(faced));
            printf("\n");
            /* And what the map thinks it has. */
            printf("openmmo:   the header names %u object event(s) and"
                   " %d sign(s)\n",
                   (unsigned)MapHeaderData_GetNumObjectEvents(fs),
                   MapHeaderData_GetNumBgEvents(fs));
        }
        return 0;
    }
    if (!openmmo_player_try_open(fs, name)) {
        printf("openmmo: A on \"%s\" and the player menu would not open\n",
               name);
        return 0;
    }
    printf("openmmo: player menu on \"%s\"\n", name);
    return 1;
}

void openmmo_on_field_interact(void)
{
    if (g_client == NULL || !g_started)
        return;
    if (openmmo_field_interact_peer())
        return;
    if (openmmo_client_in_dialog(g_client))
        return;
    /* The engine's own VM has the A press when it is running scripts; only a
     * server-scripted session sends the tile. */
    if (openmmo_local_field_scripts_enabled())
        return;
    openmmo_client_interact_tile(g_client);
}

/*
 * An A press on a ported map's trainer, from inside the engine's own interact path
 * (patches/src/overlay005/field_control.c.patch), once it has worked out who the player faces.
 */
/* A ported clerk's shelf, asked for. */
int openmmo_mart_ask(void)
{
    const FieldSystem *fs = pc_lab_field_system();

    if (g_client == NULL || !g_seated || !on_ported_map(fs))
        return 0;
    printf("openmmo: a ported clerk on header %d, the shelf is the"
           " server's\n", (int)fs->location->mapHeaderID);
    openmmo_shop_mark_pending();
    openmmo_client_interact_tile(g_client);
    return 1;
}

/* A static site's fight ended won, or with the Pokemon caught, on this
 * engine; the server is told before the RUN that closes its own instance. */
int openmmo_static_won_send(void)
{
    if (g_client == NULL || !g_seated)
        return 0;
    return openmmo_client_ug_talk_send(g_client, MMO_UG_TALK_STATIC_WON, 0,
                                       NULL, 0) == 0;
}

/* A static site's press, sent for openmmo_static.c: the same tile the mart
 * command sends, with no shop marked pending. Answers whether it went. */
int openmmo_static_press_send(void)
{
    if (g_client == NULL || !g_seated || openmmo_client_in_dialog(g_client))
        return 0;
    openmmo_client_interact_tile(g_client);
    return 1;
}

/* The engine's Strength push stays on this tile (walk-on-spot) and never
 * reaches PlayerAvatar_SetMovement, so the server would never hear it.
 * Report the same step a headless client sends when it walks into the rock. */
void openmmo_maybe_strength_push(void *playerAvatar, int dir, int playerEvent)
{
    PlayerAvatar *av = playerAvatar;
    MapObject *self, *obj;
    const MapObjectManager *man;
    int x, z;

    if (av == NULL || dir < 0)
        return;
    if ((playerEvent & PLAYER_EVENT_USED_STRENGTH) == 0)
        return;
    if (g_client == NULL || !g_seated)
        return;
    self = PlayerAvatar_GetMapObject(av);
    if (self == NULL)
        return;
    man = MapObject_MapObjectManager(self);
    x = PlayerAvatar_GetXPos(av) + MapObject_GetDxFromDir(dir);
    z = PlayerAvatar_GetZPos(av) + MapObject_GetDzFromDir(dir);
    obj = sub_0206326C(man, x, z, 0);
    if (obj == NULL)
        return;
    if ((int)MapObject_GetGraphicsID(obj) != OBJ_EVENT_GFX_STRENGTH_BOULDER)
        return;
    printf("openmmo: strength-push from (%d,%d) dir %d\n",
           PlayerAvatar_GetXPos(av), PlayerAvatar_GetZPos(av), dir);
    openmmo_client_send_move(g_client, PlayerAvatar_GetXPos(av),
                             PlayerAvatar_GetZPos(av), dir, 0);
}

/*
 * A ride that starts from the water is a field task with no script behind it, and neither
 * PlayerAvatar_SetMovement nor the end of a script ever fires for one, so the server keeps
 * the tile the ride started on.
 */
void openmmo_maybe_water_ride(void *playerAvatar)
{
    PlayerAvatar *av = playerAvatar;

    if (av == NULL || g_client == NULL || !g_seated)
        return;
    if (PlayerAvatar_GetPlayerState(av) != PLAYER_AVATAR_SURFING)
        return;
    printf("openmmo: a ride from (%d,%d) the step hook cannot see\n",
           PlayerAvatar_GetXPos(av), PlayerAvatar_GetZPos(av));
    g_scene_ran = 1;
}

/* Who takes an A press over the field, in the order the press is offered. */
int openmmo_field_interact_taken(FieldSystem *fs)
{
    if (openmmo_field_interact_peer()) {
        return 1;
    }

    /* Then your own Pokemon, and before the scripts of either half. */
    if (openmmo_follow_talk_try(fs)) {
        return 1;
    }

    if (!openmmo_local_field_scripts_enabled()) {
        openmmo_on_field_interact();
        return 1;
    }

    openmmo_static_press_try(fs);
    return 0;
}

/* A move the field made for the player rather than one they walked: a Strength
 * boulder going in, or a ride the step hook cannot see. Both are reported off
 * the same frame. */
void openmmo_forced_move_taken(void *playerAvatar, int dir, int playerEvent)
{
    openmmo_maybe_strength_push(playerAvatar, dir, playerEvent);
    openmmo_maybe_water_ride(playerAvatar);
}

/*
 * A warp walked into northward. This game's own maps have none, a north entrance is a door, so
 * the engine's warp chain never asked about the tile, and a visitor stood on HeartGold's
 * station stairs, or its Pokemon Center stairs, facing a wall.
 */
int openmmo_warp_north_tile(u8 tileBehavior)
{
    return TileBehavior_IsWarpEntranceNorth(tileBehavior) || TileBehavior_IsWarpNorth(tileBehavior);
}

int openmmo_warp_north_taken(u8 tileBehavior)
{
    return openmmo_warp_north_tile(tileBehavior) && openmmo_on_ported_map();
}

int openmmo_allow_on_frame_script(unsigned scriptID)
{
    static int logged;

    if (script_would_be_underground())
        return 0;
    if (openmmo_local_field_scripts_enabled()) {
        printf("openmmo: on-frame script %u\n", scriptID);
        g_scene_ran = 1;
        g_scene_fought = 1;
        return 1;
    }
    /* The var that would retire this table entry never moves, so the
     * engine asks every frame. Name it once. */
    if (!logged) {
        printf("openmmo: refused on-frame script %u\n", scriptID);
        logged = 1;
    }
    return 0;
}

/* A local script's GivePokemon succeeded (called from the ScrCmd patch). */
void openmmo_script_grant_report(int species, int level, int hp,
                                 int container, int slot, u32 seed,
                                 u32 iv_bits, int shiny,
                                 const void *nick_charcodes)
{
    const char *why = NULL;
    uint8_t nick[2 * 12];
    size_t nick_bytes = 0;
    u16 dex;

    if (g_client == NULL || !session_configured()
        || !openmmo_local_field_scripts_enabled())
        return;
    dex = mmo_id_species_to_server((u16)species, &why);
    if (dex == MMO_ID_NONE) {
        printf("openmmo: script gave species %d lv %d but it has no wire id"
               " (%s), the server was not told\n",
               species, level, why != NULL ? why : "untranslatable");
        return;
    }
    if (nick_charcodes != NULL) {
        mmo_charcode_result r = mmo_charcode_to_utf16le(
            (const mmo_charcode *)nick_charcodes, nick, sizeof nick);
        nick_bytes = r.written * 2;
    }
    printf("openmmo: grant species %d lv %d -> %s slot %d, telling the"
           " server (dex %u%s)\n", species, level,
           container == OPENMMO_CONTAINER_PARTY ? "party" : "pc", slot, dex,
           nick_bytes ? ", named" : "");
    openmmo_client_send_script_grant(g_client, (int)dex, level, hp,
                                     container, slot, seed, iv_bits, shiny,
                                     nick_bytes ? nick : NULL, nick_bytes);
}

void openmmo_script_gave_pokemon(int species, int level)
{
    extern void openmmo_pc_note_script_grant(FieldSystem *fs, u32 *seed,
                                             u32 *iv_bits, int *shiny);
    u32 seed, iv_bits;
    int shiny;

    /* Told to the box sync before it goes out, or the settle at the end of the
     * scene sweeps the party, finds a monster in no table of its own, and
     * grants the same starter a second time. It hands back the individual the
     * engine just built, so a gift is reported the way a capture is. */
    openmmo_pc_note_script_grant(pc_lab_field_system(), &seed, &iv_bits,
                                 &shiny);
    openmmo_script_grant_report(species, level, -1,
                                OPENMMO_CONTAINER_PARTY, -1, seed, iv_bits,
                                shiny, NULL);
}

/*
 * A local script gave the player the running shoes. They live in PlayerData, outside
 * VarsFlags, so the shadow diff never sees them, and the play path rolls a fresh save every
 * join, so without a row in the server's record a rejoin walked everywhere again.
 */
void openmmo_running_shoes_given(void)
{
    mmo_script_flag row;

    if (g_client == NULL || !session_configured()
        || !openmmo_local_field_scripts_enabled())
        return;
    row.id = MMO_SCRIPT_FLAG_RUNNING_SHOES;
    row.on = 1;
    if (openmmo_client_send_script_state(g_client, &row, 1, NULL, 0,
                                         NULL, 0) == 0)
        printf("openmmo: running shoes given, telling the server\n");
}

/*
 * PlayerAvatar_SetMovement is the funnel every pad-driven action goes through. FACE_* is the
 * standing idle, called every frame, ignore it.
 */
static int action_is_idle_face(int a)
{
    return a >= MOVEMENT_ACTION_FACE_NORTH && a <= MOVEMENT_ACTION_FACE_EAST;
}

static int action_is_on_spot(int a)
{
    if (a >= MOVEMENT_ACTION_WALK_ON_SPOT_SLOWER_NORTH
        && a <= MOVEMENT_ACTION_WALK_ON_SPOT_FASTER_EAST)
        return 1;
    if (a >= MOVEMENT_ACTION_JUMP_ON_SPOT_SLOW_NORTH
        && a <= MOVEMENT_ACTION_JUMP_ON_SPOT_FAST_EAST)
        return 1;
    return 0;
}

/* How many tiles one step action moves the avatar. */
static int action_step_tiles(int a)
{
    if (a >= MOVEMENT_ACTION_JUMP_FAR_NORTH && a <= MOVEMENT_ACTION_JUMP_FAR_EAST)
        return 2;
    if (a >= MOVEMENT_ACTION_JUMP_DISTORTION_WORLD_NORTH
        && a <= MOVEMENT_ACTION_JUMP_DISTORTION_WORLD_EAST)
        return 3;
    if (a == MOVEMENT_ACTION_JUMP_FARTHER_WEST || a == MOVEMENT_ACTION_JUMP_FARTHER_EAST)
        return 3;
    return 1;
}

static int action_is_step(int a)
{
    if (action_is_idle_face(a) || action_is_on_spot(a))
        return 0;
    if (a >= MOVEMENT_ACTION_WALK_SLOWER_NORTH && a <= MOVEMENT_ACTION_WALK_FASTER_EAST)
        return 1;
    if (a >= MOVEMENT_ACTION_JUMP_NEAR_FAST_NORTH && a <= MOVEMENT_ACTION_JUMP_FAR_EAST)
        return 1;
    if (a >= MOVEMENT_ACTION_WALK_SLIGHTLY_FAST_NORTH && a <= MOVEMENT_ACTION_RUN_EAST)
        return 1;
    if (a >= MOVEMENT_ACTION_JUMP_NEAR_SLOW_WEST && a <= MOVEMENT_ACTION_JUMP_FARTHER_EAST)
        return 1;
    if (a >= MOVEMENT_ACTION_WALK_EVER_SO_SLIGHTLY_FAST_NORTH
        && a <= MOVEMENT_ACTION_WALK_EVER_SO_SLIGHTLY_FAST_EAST)
        return 1;
    if (a >= MOVEMENT_ACTION_JUMP_DISTORTION_WORLD_NORTH
        && a <= MOVEMENT_ACTION_JUMP_DISTORTION_WORLD_EAST)
        return 1;
    if (a >= MOVEMENT_ACTION_105 && a <= MOVEMENT_ACTION_116)
        return 1;
    if (a >= MOVEMENT_ACTION_121 && a <= MOVEMENT_ACTION_132)
        return 1;
    return 0;
}

/* A door or warp-entrance bump stays on the tile (the engine treats it
 * as collision) but the server warps on the Movement packet, not a face.
 * Mirrors PlayerAvatar_WillWarp: warp-entrance underfoot in this
 * direction, or a door on the next tile. */
static int warp_bump(PlayerAvatar *av, int dir)
{
    MapObject *obj;
    FieldSystem *fs;
    u8 behavior;
    int x, z;

    if (av == NULL || dir == DIR_NONE)
        return 0;
    obj = PlayerAvatar_GetMapObject(av);
    if (obj == NULL)
        return 0;
    fs = MapObject_FieldSystem(obj);
    if (fs == NULL)
        return 0;

    x = PlayerAvatar_GetXPos(av);
    z = PlayerAvatar_GetZPos(av);
    behavior = TerrainCollisionManager_GetTileBehavior(fs, x, z);
    /* Entrance and arrow/stair warps both fire while standing here.
     * The next tile is often a wall, so this is an on-spot bump. */
    switch (dir) {
    case DIR_NORTH:
        if (TileBehavior_IsWarpEntranceNorth(behavior)
            || TileBehavior_IsWarpNorth(behavior))
            return 1;
        break;
    case DIR_SOUTH:
        if (TileBehavior_IsWarpEntranceSouth(behavior)
            || TileBehavior_IsWarpSouth(behavior))
            return 1;
        break;
    case DIR_WEST:
        if (TileBehavior_IsWarpEntranceWest(behavior)
            || TileBehavior_IsWarpWest(behavior)
            || TileBehavior_IsWarpStairsWest(behavior))
            return 1;
        break;
    case DIR_EAST:
        if (TileBehavior_IsWarpEntranceEast(behavior)
            || TileBehavior_IsWarpEast(behavior)
            || TileBehavior_IsWarpStairsEast(behavior))
            return 1;
        break;
    default:
        break;
    }

    x += MapObject_GetDxFromDir(dir);
    z += MapObject_GetDzFromDir(dir);
    behavior = TerrainCollisionManager_GetTileBehavior(fs, x, z);
    return TileBehavior_IsDoor(behavior) == TRUE;
}

/* A body that is not a player's sheet has no run cycle (a person's is four
 * walks): keep the run speed, use walk frames. The player's own two and a
 * composed look, a hero sheet laid out like the player's, with its run,
 * keep the run. */
int openmmo_remap_movement(void *playerAvatar, int movementAction)
{
    PlayerAvatar *av = playerAvatar;
    MapObject *obj;
    int gfx;

    if (av == NULL)
        return movementAction;
    obj = PlayerAvatar_GetMapObject(av);
    if (obj == NULL)
        return movementAction;
    gfx = (int)MapObject_GetGraphicsID(obj);
    if (gfx == MMO_APPEAR_GFX_PLAYER_M || gfx == MMO_APPEAR_GFX_PLAYER_F
        || mmo_appearance_look_of_gfx(gfx) >= 0)
        return movementAction;
    if (movementAction >= MOVEMENT_ACTION_RUN_NORTH
        && movementAction <= MOVEMENT_ACTION_RUN_EAST)
        return (int)MOVEMENT_ACTION_WALK_FAST_NORTH
            + (movementAction - (int)MOVEMENT_ACTION_RUN_NORTH);
    return movementAction;
}

/* Why a step was refused, when "there is a wall there" is not the answer. */
static void report_blocked_step(PlayerAvatar *av, int dir, int always)
{
    static int last_x = -1, last_z = -1, last_refused = -1;
    const MapObjectManager *man;
    MapObject *obj;
    FieldSystem *fs;
    VecFx32 pos;
    int x, z, d, surfing, refused = 0, explained = 0;

    obj = av != NULL ? PlayerAvatar_GetMapObject(av) : NULL;
    fs = obj != NULL ? MapObject_FieldSystem(obj) : NULL;
    if (fs == NULL)
        return;

    x = PlayerAvatar_GetXPos(av);
    z = PlayerAvatar_GetZPos(av);

    surfing = PlayerAvatar_GetPlayerState(av) == PLAYER_AVATAR_SURFING;
    for (d = DIR_NORTH; d <= DIR_EAST; d++) {
        int tx = x + MapObject_GetDxFromDir(d);
        int tz = z + MapObject_GetDzFromDir(d);
        u32 c = PlayerAvatar_CheckCollision(av, obj, d);

        /* A ledge and a distortion gap are steps, not refusals, and the
         * water under a surfer is the surface they are on. */
        if (c & (PLAYER_COLLISION_JUMP | PLAYER_COLLISION_JUMP_TWICE))
            continue;
        if (surfing && (c & ~(u32)PLAYER_COLLISION_WATER) == 0)
            continue;
        if (c == PLAYER_COLLISION_NONE)
            continue;

        refused |= 1 << d;
        /* Refused for something the player can see: a wall, a shoreline, or
         * a door the server is about to take them through. */
        if (TerrainCollisionManager_CheckCollision(fs, tx, tz) == TRUE
            || (c & (PLAYER_COLLISION_WATER | PLAYER_COLLISION_WARP)))
            explained |= 1 << d;
    }

    /* Every refusal has something behind it the player can see, and there is
     * a way off the tile: this is the game working. */
    if (!always && refused == explained && refused != 0x0f)
        return;
    if (!always && x == last_x && z == last_z && refused == last_refused)
        return;
    last_x = x;
    last_z = z;
    last_refused = refused;

    man = MapObject_MapObjectManager(obj);
    MapObject_GetPosPtr(obj, &pos);
    printf("openmmo: refused on header %d at (%d,%d) dir %d: refused %x, of"
           " which %x is in plain sight; standing at height %d (elevation"
           " %d)\n",
           fs->location != NULL ? (int)fs->location->mapHeaderID : -1,
           x, z, dir, refused, explained, (int)(pos.y / FX32_ONE),
           MapObject_GetY(obj));
    for (d = DIR_NORTH; d <= DIR_EAST; d++) {
        int tx = x + MapObject_GetDxFromDir(d);
        int tz = z + MapObject_GetDzFromDir(d);
        MapObject *on = man != NULL ? sub_0206326C(man, tx, tz, 1) : NULL;
        u8 src = CALCULATED_HEIGHT_SOURCE_NONE;
        fx32 ground = TerrainCollisionManager_GetHeight(
            fs, pos.y, tx * MAP_OBJECT_TILE_SIZE + MAP_OBJECT_TILE_SIZE / 2,
            tz * MAP_OBJECT_TILE_SIZE + MAP_OBJECT_TILE_SIZE / 2, &src);

        printf("openmmo:   dir %d (%d,%d) behaviour 0x%02x ground %d from %d%s\n",
               d, tx, tz, TerrainCollisionManager_GetTileBehavior(fs, tx, tz),
               (int)(ground / FX32_ONE), src,
               (on != NULL && on != obj) ? ", an object stands there" : "");
    }
    fflush(stdout);
}

void openmmo_player_set_movement(void *playerAvatar, void *mapObj, int movementAction,
                                 int speed)
{
    PlayerAvatar *av = playerAvatar;
    FieldSystem *fs;
    int x, z, dir, running, tiles, below;

    (void)mapObj;
    (void)speed;
    if (av == NULL || action_is_idle_face(movementAction))
        return;

    /*
     * Only the player this window belongs to. The patch hooks the engine's
     * PlayerAvatar_SetMovement, which every avatar goes through, and a remote player is a
     * PlayerAvatar here (apply_live_entities builds them with PlayerAvatar_New).
     */
    fs = pc_lab_field_system();
    if (fs == NULL || av != fs->playerAvatar)
        return;

    /* HeartGold's follower does not watch the player: the player's own step
     * code hands it the action and the tile to step onto (sub_0205D4B4 ->
     * ov01_02205990). This is that hand-off, once the engine has committed
     * the action (mods/openmmo/src/openmmo_follow_move.c). */
    {
        extern void openmmo_follow_player_moved(FieldSystem *fs, MapObject *player, int action);
        openmmo_follow_player_moved(fs, (MapObject *)mapObj, movementAction);
    }

    /*
     * OPENMMO_MOVE_REPORT=1: the action the engine committed and the state it came from, once
     * per change.
     */
    {
        static int on = -1;
        static int last_action = -1;

        if (on < 0)
            on = openmmo_dev_env("OPENMMO_MOVE_REPORT") != NULL;
        if (on && movementAction != last_action) {
            last_action = movementAction;
            printf("openmmo: move action %d in state %d\n", movementAction,
                   PlayerAvatar_GetPlayerState(av));
            fflush(stdout);
        }
    }

    /* A fight the window is only holding: the server already has the
     * player in battle, so a step or face now would walk a tile the
     * overworld is not on. The engine still animates; the wire does not. */
    if (session_in_battle())
        return;
    if (session_in_dialog())
        return;

    /*
     * Below ground the tiles are the cavern's own 480x480 grid, and the server still holds the
     * surface tile the descent left, it is not told about the trip, because the Underground
     * is one player's own place.
     */
    below = openmmo_underground_active();

    dir = MovementAction_GetDirFromAction((enum MovementAction)movementAction);
    if (dir == DIR_NONE)
        return;

    x = PlayerAvatar_GetXPos(av);
    z = PlayerAvatar_GetZPos(av);

    if (action_is_on_spot(movementAction)) {
        /* A wall bump is a face. A door or warp-entrance bump is the
         * step the server warps on, sending a face would leave the
         * player stuck on this side of a door the server owns. */
        if (!openmmo_local_warps_enabled() && warp_bump(av, dir)) {
            printf("openmmo: warp-bump from (%d,%d) dir %d\n", x, z, dir);
            if (g_client != NULL && g_seated && !below)
                openmmo_client_send_move(g_client, x, z, dir, 0);
            return;
        }
        printf("openmmo: face dir %d at (%d,%d)\n", dir, x, z);
        /* A refusal the geometry does not explain, or a tile with no way off
         * it: say so once, with the three things only the engine knows. The
         * SLOW variant on this range is the wall bump; FASTER is a turn. */
        if (movementAction >= MOVEMENT_ACTION_WALK_ON_SPOT_SLOW_NORTH
            && movementAction <= MOVEMENT_ACTION_WALK_ON_SPOT_SLOW_EAST)
            report_blocked_step(av, dir,
                                getenv("OPENMMO_INTERACT_REPORT") != NULL);
        /* OPENMMO_INTERACT_REPORT=1: what the tile underfoot and the one
         * faced are, for a warp that did not fire (a stair the engine's
         * transition table does not know is one that reads as a wall). */
        if (getenv("OPENMMO_INTERACT_REPORT") != NULL) {
            MapObject *o = PlayerAvatar_GetMapObject(av);
            FieldSystem *fs = o != NULL ? MapObject_FieldSystem(o) : NULL;

            if (fs != NULL)
                printf("openmmo: tile behaviour underfoot 0x%02x, ahead 0x%02x,"
                       " warp event underfoot %d\n",
                       TerrainCollisionManager_GetTileBehavior(fs, x, z),
                       TerrainCollisionManager_GetTileBehavior(
                           fs, x + MapObject_GetDxFromDir(dir),
                           z + MapObject_GetDzFromDir(dir)),
                       MapHeaderData_GetIndexOfWarpEventAtPos(fs, x, z));
        }
        if (g_client != NULL && g_seated && !below)
            openmmo_client_send_face(g_client, dir);
        return;
    }

    if (!action_is_step(movementAction))
        return;

    running = (movementAction >= MOVEMENT_ACTION_RUN_NORTH
               && movementAction <= MOVEMENT_ACTION_RUN_EAST);
    tiles = action_step_tiles(movementAction);
    printf("openmmo: step from (%d,%d) dir %d run=%d tiles=%d\n",
           x, z, dir, running, tiles);
    /* The Underground counts dwell off the comm slot's step timer, and nothing
     * else down there knows a step started (openmmo_underground.c). */
    openmmo_underground_note_step(running);
    if (g_client != NULL && g_seated && !below)
        openmmo_client_send_move_tiles(g_client, x, z, dir, running, tiles);
}

/*
 * Kick off a session if OPENMMO_SESSION asks for one. Unset is the default: no session starts
 * and the per-frame pump is a no-op on the idle client, so the game boots exactly as the
 * vanilla port does.
 */
/* Copy an environment string into a static buffer, or fall back to a default.
 * The caller reads the variable, so which kind of read a name gets is decided
 * beside the name; the client keeps the pointer for the session's lifetime, so
 * it cannot borrow the volatile getenv() return. */
static const char *env_or(const char *v, const char *fallback,
                          char *buf, size_t bufsz)
{
    if (v == NULL || v[0] == '\0')
        return fallback;
    size_t n = strlen(v);
    if (n >= bufsz)
        n = bufsz - 1;
    memcpy(buf, v, n);
    buf[n] = '\0';
    return buf;
}

/*
 * Open the page this session's state is published on, beside the frame page and named after
 * it. The game creates it because the game is the only party that knows: the window may not
 * exist yet and the launcher never sees the wire.
 */
static mmo_shm g_status_page = MMO_SHM_INIT;

static void status_page_open(void)
{
    const char *chan = getenv("PC_VIEW");
    char name[128];
    struct openmmo_status_shm *s;

    if (chan == NULL || chan[0] == '\0')
        return;
    snprintf(name, sizeof name, "%s%s", chan, OPENMMO_STATUS_SUFFIX);

    if (mmo_shm_create(&g_status_page, name, sizeof *s) != 0) {
        fprintf(stderr, "openmmo: no status page '%s' (%s); the game still "
                        "runs, the shell just cannot say how it is doing\n",
                name, mmo_plat_error());
        return;
    }

    /* Zeroed first, then the magic last: a reader that finds the page mid-setup
     * sees no magic and waits, rather than reading a previous session's words
     * out of a page this one has not filled in yet. */
    s = (struct openmmo_status_shm *)g_status_page.addr;
    memset(s, 0, sizeof *s);
    s->version = OPENMMO_STATUS_VERSION;
    s->writer = (uint32_t)mmo_plat_pid();
    __atomic_store_n(&s->magic, OPENMMO_STATUS_MAGIC, __ATOMIC_RELEASE);
    g_status = s;
}

/*
 * Publish the client's state when it has moved. Called every frame; a frame in which nothing
 * changed writes nothing, so a reader's `gen` counts transitions and not frames.
 */
static void status_page_publish(void)
{
    static uint32_t last_state = 0xFFFFFFFFu;
    static uint32_t last_flags = 0xFFFFFFFFu;
    static char last_reason[OPENMMO_STATUS_REASON];
    uint32_t st;
    const char *reason;

    if (g_status == NULL || g_client == NULL || !g_started)
        return;

    /* Not sticky, unlike the two flags above it: this one is where the player
     * is standing, and the window lays the screens out the other way round for
     * as long as it holds (see the flag's own comment). */
    if (openmmo_underground_active())
        g_status_flags |= OPENMMO_STATUS_F_UNDERGROUND;
    else
        g_status_flags &= ~(uint32_t)OPENMMO_STATUS_F_UNDERGROUND;

    st = (uint32_t)openmmo_client_status(g_client);
    if (st == OPENMMO_ST_AUTHED || st == OPENMMO_ST_REQUESTING_GAME ||
        st == OPENMMO_ST_JOINING_GAME || st == OPENMMO_ST_IN_GAME)
        g_status_flags |= OPENMMO_STATUS_F_WAS_LIVE;

    /* The message only means anything where it is shown: carrying the last
     * failure's words into a fresh connecting would caption a working connect
     * with the reason the previous one broke. */
    reason = (st == OPENMMO_ST_FAILED || st == OPENMMO_ST_DISCONNECTED)
                 ? g_message : "";

    if (st == last_state && g_status_flags == last_flags
        && strncmp(reason, last_reason, sizeof last_reason) == 0)
        return;
    last_state = st;
    last_flags = g_status_flags;
    snprintf(last_reason, sizeof last_reason, "%s", reason);
    openmmo_status_publish(g_status, st, g_status_flags, reason);
}

static void maybe_start_from_env(void)
{
    static char user[64];
    static char pass[64];
    static char character[64];

    if (!session_configured())
        return;

    /*
     * The account is whoever started this process, and nobody when nothing did. Two clients on
     * one machine hold two sessions by being started with two names.
     */
    openmmo_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.user = env_or(getenv("OPENMMO_USER"), "", user, sizeof(user));
    cfg.pass = env_or(getenv("OPENMMO_PASS"), "", pass, sizeof(pass));
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    /* This binary carries the engine's script VM, so say so: the server will
     * not start the same cutscene from its own corpus. A build told to leave
     * scripts to the server (OPENMMO_LOCAL_SCRIPTS=0) declares the opposite,
     * and the two halves cannot disagree because this is the one predicate. */
    cfg.local_scripts = openmmo_local_field_scripts_enabled();
    {
        const char *ch = getenv("OPENMMO_CHARACTER");

        if (ch != NULL && ch[0] != '\0') {
            cfg.select_name = env_or(getenv("OPENMMO_CHARACTER"), "",
                                     character, sizeof(character));
            cfg.select_how = OPENMMO_SELECT_NAME;
        } else {
            cfg.select_how = OPENMMO_SELECT_HOLD;
        }
    }

    if (openmmo_client_start(g_client, &cfg) != 0) {
        printf("openmmo: could not start a session\n");
    } else {
        /* Not where, the address is the build's, not this run's, and a log
         * a player can post is the last place to spell it out. */
        printf("openmmo: connecting as %s\n", cfg.user);
    }
    /* The connect is under way (or already FAILED); either is a state worth
     * showing, so the HUD is live from here on. */
    openmmo_script_state_reset();
    openmmo_follow_reset();
    g_started = 1;
    g_status_flags |= OPENMMO_STATUS_F_SESSION;
    status_page_open();
    status_page_publish();
    openmmo_hud_open();
}

/*
 * The rejoin campaign's whole memory. `character` is captured while the world is live; the
 * rest belongs to the session-over block in openmmo_mod_frame.
 */
enum {
    REJOIN_FIRST_WAIT_S = 2,   /* the radio is usually mid-roam; let it land */
    REJOIN_RETRY_S      = 5,
    REJOIN_GIVE_UP_S    = 180, /* the measured outages were under a minute */
};

static struct {
    int  active;
    int  gave_up;
    int  tries;
    long lost_s;          /* when the link died */
    long next_s;          /* when the next attempt is due */
    char character[33];   /* openmmo_world_state.name, same bound */
} g_rejoin;

/*
 * One attempt: the same start maybe_start_from_env performs, on the same client object,
 * adopt_config wipes every session block and keeps the PC allocation, its own comments calling
 * this a restart, with the one difference that the character is picked back by NAME, so the
 * lobby never appears over a field mid-rejoin.
 */
static void rejoin_attempt(void)
{
    static char user[64];
    static char pass[64];
    openmmo_config cfg;

    memset(&cfg, 0, sizeof cfg);
    cfg.user = env_or(getenv("OPENMMO_USER"), "", user, sizeof user);
    cfg.pass = env_or(getenv("OPENMMO_PASS"), "", pass, sizeof pass);
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    cfg.local_scripts = openmmo_local_field_scripts_enabled();
    if (g_rejoin.character[0] != '\0') {
        cfg.select_name = g_rejoin.character;
        cfg.select_how = OPENMMO_SELECT_NAME;
    } else {
        cfg.select_how = OPENMMO_SELECT_FIRST;
    }

    /* The stores rewind to their baselines the way a fresh session's do; the
     * seat that arrives on success re-seeds both, over an engine save that
     * already agrees with it. */
    openmmo_script_state_reset();
    openmmo_follow_reset();

    g_rejoin.tries++;
    if (openmmo_client_start(g_client, &cfg) != 0)
        fprintf(stderr, "openmmo: rejoin attempt %d did not connect\n",
                g_rejoin.tries);
    else
        fprintf(stderr, "openmmo: rejoin attempt %d\n", g_rejoin.tries);
}

/* How long a parting report may take to leave. A frame is 16 ms and the report
 * is one small packet, so this is the socket being wedged rather than the
 * report being large, and a quarter of a second is under the notice the
 * window is already showing. */
#define LEAVE_FLUSH_MS 250

/*
 * The last thing the server hears from this session: whatever the last seconds wrote, then the
 * hang-up.
 */
static void report_and_hang_up(void)
{
    if (g_client == NULL)
        return;
    openmmo_script_state_flush(pc_lab_field_system(), g_client);
    openmmo_client_flush(g_client, LEAVE_FLUSH_MS);
    openmmo_client_disconnect(g_client);
}

/* Whether the state this session is holding is still the state the server should keep. */
enum { LEAVE_SUPERSEDED = 0, LEAVE_REPORT = 1 };

/* The session ends here, on purpose, with the process. Continue Offline and a
 * landed save both leave this way: the launcher is what runs next, and it reads
 * what the session left on disk. The card is hung up before the exit so it does
 * not outlive the session that put it there. */
static void leave_session(const char *why, int report)
{
    printf("openmmo: %s\n", why);
    if (report)
        report_and_hang_up();
    else if (g_client != NULL)
        openmmo_client_disconnect(g_client);
    openmmo_presence_shutdown();
    fflush(NULL);
    exit(0);
}

/*
 * True if p lies inside the emulated DS main RAM, the region every engine named heap is
 * carved from. Checks both the main arena and the extended arena, since heap.c allocates
 * HEAP_ID_* from OS_AllocFromMainExArenaHi as well as the low arena.
 */
static int addr_in_engine_arena(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    const OSArenaId ids[2] = { OS_ARENA_MAIN, OS_ARENA_MAINEX };
    int i;

    for (i = 0; i < 2; i++) {
        uintptr_t lo = (uintptr_t)OS_GetInitArenaLo(ids[i]);
        uintptr_t hi = (uintptr_t)OS_GetInitArenaHi(ids[i]);
        if (lo && hi && a >= lo && a < hi)
            return 1;
    }
    return 0;
}

/*
 * Prove, once, at runtime, that client state lives off the engine's freeable memory, and
 * make the wrong answer fail here instead of at a corrupt read three frames after a map
 * change.
 */
static void openmmo_check_state_arena(void)
{
    const void *lo = OS_GetInitArenaLo(OS_ARENA_MAIN);
    const void *hi = OS_GetInitArenaHi(OS_ARENA_MAIN);

    if (g_client == NULL)
        return;

    if (addr_in_engine_arena(g_client)) {
        fprintf(stderr,
            "openmmo: FATAL: client state %p is inside the engine's freeable "
            "arena [%p,%p), a map change, battle or overlay swap will free it "
            "underneath the session. Client state must come from the process "
            "(libc) heap, never an engine named heap.\n",
            (void *)g_client, lo, hi);
        fflush(NULL);
        abort();
    }

    printf("openmmo: client state %p is off the engine arena "
           "(main [%p,%p)), outlives map, battle and overlay teardown\n",
           (void *)g_client, lo, hi);
}

/* Attach the window's typing page if it is already up. Retry each frame:
 * play.sh starts the game before the window, so the page is often late.
 * Never created here, a run with no window has no typist. */
static mmo_shm g_text_page = MMO_SHM_INIT;

static void text_page_attach(void)
{
    const char *chan = getenv("PC_VIEW");
    char name[128];
    struct openmmo_text_shm *t;

    if (g_text != NULL)
        return;
    if (chan == NULL || chan[0] == '\0')
        return;
    snprintf(name, sizeof name, "%s%s", chan, OPENMMO_TEXT_SUFFIX);
    if (mmo_shm_attach(&g_text_page, name, sizeof *t, 1) != 0)
        return;
    t = (struct openmmo_text_shm *)g_text_page.addr;
    if (t->magic != OPENMMO_TEXT_MAGIC || t->version != OPENMMO_TEXT_VERSION) {
        mmo_shm_close(&g_text_page);
        return;
    }
    g_text = t;
    g_text_tail = t->head;
    printf("openmmo: text page '%s' attached\n", name);
}

static void text_page_want(int on)
{
    if (g_text == NULL)
        return;
    g_text->want = on ? 1u : 0u;
}

/* Close the compose field the way an MMO does: the line survives as a
 * draft unless it was just sent, so a click on the world or an Escape
 * does not eat a half-typed sentence. */
static void chat_close_compose(int keep_draft)
{
    if (keep_draft) {
        g_chat_draft_len = osk_text_len(&g_osk);
        memcpy(g_chat_draft, g_osk.text,
               g_chat_draft_len * sizeof g_chat_draft[0]);
    } else
        g_chat_draft_len = 0;
    if (mmo_chatwin_composing(&g_chat))
        mmo_chatwin_toggle_compose(&g_chat);
    osk_reset(&g_osk);
}

static void apply_hud_cmds(void)
{
    uint32_t cmd[16];
    unsigned n, i;

    n = openmmo_hud_take_cmds(cmd, 16);
    for (i = 0; i < n; i++) {
        uint32_t kind = openmmo_hud_cmd_kind(cmd[i]);
        int32_t arg = openmmo_hud_cmd_arg(cmd[i]);

        if (kind == OPENMMO_HUD_CMD_COMPOSE) {
            if (arg) {
                mmo_chatwin_show(&g_chat);
                if (!mmo_chatwin_composing(&g_chat))
                    mmo_chatwin_toggle_compose(&g_chat);
                text_page_want(1);
            } else if (mmo_chatwin_composing(&g_chat)) {
                chat_close_compose(1);
                text_page_want(0);
            }
        } else if (kind == OPENMMO_HUD_CMD_CHANNEL) {
            /* The window's active tab. Anything unrecognised falls back to
             * the local channel rather than a byte the server never routes. */
            if (arg == MMO_CHAT_GLOBAL || arg == MMO_CHAT_TRADE ||
                arg == MMO_CHAT_WHISPER || arg == MMO_CHAT_BATTLE)
                g_chat_send_type = (int)arg;
            else
                g_chat_send_type = MMO_CHAT_NORMAL;
        } else if (kind == OPENMMO_HUD_CMD_SCROLL) {
            openmmo_event ev[OPENMMO_CHAT_LOG];
            int nlog = 0;

            if (g_client != NULL)
                nlog = openmmo_client_chat(g_client, ev, OPENMMO_CHAT_LOG);
            mmo_chatwin_scroll(&g_chat, arg, nlog);
        } else if (kind == OPENMMO_HUD_CMD_SCREEN) {
            /* Five of the window's HUD buttons name a screen this engine
             * already draws. openmmo_apps.c opens it on the next settled
             * field; nothing is drawn here for it. The trade link is ours,
             * not an engine app, so its id turns aside here. */
            if ((arg & 0xFFu) == OPENMMO_HUD_SCREEN_GTL)
                openmmo_gtl_mark_pending();
            else
                openmmo_apps_request(arg);
        } else if (kind == OPENMMO_HUD_CMD_MAIL) {
            /* The window's own Mail frame: page asks, opens, deletes and
             * the compose, resolved against the client store the page was
             * published from. */
            openmmo_mail_window_cmd(arg);
        } else if (kind == OPENMMO_HUD_CMD_GTL) {
            /* The window's own GTL frame: page asks, buys, take-backs and
             * the sell verb, resolved against the client store the page
             * was published from. */
            openmmo_gtl_window_cmd(arg);
        } else if (kind == OPENMMO_HUD_CMD_PLAYER) {
            /* The window's frames raise the official client's player menu; the pick runs
             * through the same action layer as the guest's own. The window
             * already confirmed anything official confirms. */
            char who[OPENMMO_HUD_NAME];

            snprintf(who, sizeof who, "%s", openmmo_hud_cmd_name(arg));
            openmmo_player_do(openmmo_hud_player_verb(arg), who);
        } else if (kind == OPENMMO_HUD_CMD_PARTY_MOVE) {
            /* The strip's drag: one wire move, party to party, and the
             * server's answer is the order the strip then draws. */
            int from = arg & 0xFF, to = (arg >> 8) & 0xFF;

            if (g_client != NULL &&
                openmmo_client_move_pokemon(g_client, OPENMMO_CONTAINER_PARTY,
                                            from, OPENMMO_CONTAINER_PARTY,
                                            to) == 0)
                printf("openmmo: party move %d -> %d\n", from, to);
            else
                printf("openmmo: party move %d -> %d would not send\n",
                       from, to);
        } else if (kind == OPENMMO_HUD_CMD_LOGOUT) {
            /*
             * Back to character select: disconnect politely, then become a fresh boot of the
             * same program (mmo_plat_exec_self, exec on POSIX, spawn-and-leave on Windows;
             * either way the shared pages are reopened by name and the attached window rides
             * straight through into the lobby's frames).
             */
            printf("openmmo: logout, back to character select\n");
            report_and_hang_up();
            mmo_plat_unsetenv("OPENMMO_CHARACTER");
            mmo_plat_unsetenv("OPENMMO_BOOT_WORLD");
            /* Hang up before the exec: the card must not outlive the session
             * that put it there, and the process that replaces this one opens
             * its own connection. */
            openmmo_presence_shutdown();
            fflush(NULL);
            mmo_plat_exec_self();
            /* Only reached on failure; a dead session must not keep
             * drawing a world nobody is connected to. */
            fprintf(stderr, "openmmo: logout exec failed; leaving\n");
            exit(0);
        } else if (kind == OPENMMO_HUD_CMD_EXPORT) {
            /* Carry on offline. The save itself waits for a field that is
             * standing still (openmmo_offline_export_tick, below), so this
             * only remembers who asked. */
            const openmmo_world_state *ws =
                (g_client != NULL) ? openmmo_client_world_state(g_client)
                                   : NULL;

            openmmo_offline_export_request(
                (ws != NULL && ws->valid) ? ws->name : NULL);
        }
    }
}

static void drive_text_page(void)
{
    uint32_t ev[32];
    uint32_t dropped = 0;
    unsigned n, i;

    if (g_text == NULL)
        return;
    n = openmmo_text_read(g_text, &g_text_tail, ev, 32, &dropped);
    if (dropped)
        fprintf(stderr, "openmmo: text page dropped %u event(s)\n",
                (unsigned)dropped);
    for (i = 0; i < n; i++)
        osk_feed(&g_osk, openmmo_text_kind(ev[i]), openmmo_text_unit(ev[i]));
}

/* The typed line, as UTF-8: the keyboard accumulates UTF-16 code units (the
 * text page carries them, osk.h holds them) and everything downstream of here,
 * the wire codec, the glyph bridge, the host panel, is UTF-8. A character
 * that does not fit whole is left out rather than cut in half. */
static void osk_line_utf8(const osk_state *k, char *dst, size_t cap)
{
    size_t i, n = 0, len;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (k == NULL)
        return;
    len = osk_text_len(k);
    for (i = 0; i < len;) {
        char seq[4];
        unsigned used = 1;
        unsigned w = openmmo_text_utf16_to_utf8(&k->text[i],
                                                (unsigned)(len - i), seq, &used);

        if (w == 0 || n + w + 1 > cap)
            break;
        memcpy(dst + n, seq, w);
        n += w;
        i += used;
    }
    dst[n] = '\0';
}

/*
 * Drive the on-screen keyboard from the engine's already-debounced input and from the host
 * keyboard on the .text page.
 */
static void drive_osk(void)
{
    u32 rep = gSystem.pressedKeysRepeatable;
    u32 hit = gSystem.pressedKeys;
    u32 eat;

    text_page_attach();
    if (mmo_entry_wants_host(g_osk_surface))
        text_page_want(1);
    drive_text_page();

    if (rep & PAD_KEY_UP)    osk_move(&g_osk, OSK_UP);
    if (rep & PAD_KEY_DOWN)  osk_move(&g_osk, OSK_DOWN);
    if (rep & PAD_KEY_LEFT)  osk_move(&g_osk, OSK_LEFT);
    if (rep & PAD_KEY_RIGHT) osk_move(&g_osk, OSK_RIGHT);
    if (hit & PAD_BUTTON_A)  osk_activate(&g_osk);
    if (hit & PAD_BUTTON_B)  osk_backspace(&g_osk);

    if (gSystem.touchPressed)
        osk_pen(&g_osk, gSystem.touchX, gSystem.touchY);

    /* The field stays up under this keyboard. Eat the pad and the pen
     * so a letter does not also walk the avatar. START is left for
     * drive_chat, which closes compose. */
    eat = PAD_KEY_UP | PAD_KEY_DOWN | PAD_KEY_LEFT | PAD_KEY_RIGHT
        | PAD_BUTTON_A | PAD_BUTTON_B;
    gSystem.pressedKeys &= ~eat;
    gSystem.pressedKeysRepeatable &= ~eat;
    gSystem.touchPressed = 0;

    if (osk_committed(&g_osk)) {
        char line[OSK_MAX_TEXT + 1];

        osk_line_utf8(&g_osk, line, sizeof line);
        printf("openmmo: keyboard committed \"%s\"\n", line);
        if (mmo_creator_needs_entry(&g_creator)) {
            if (!mmo_creator_set_name(&g_creator, line))
                printf("openmmo: character create: that name was not accepted\n");
        } else if (mmo_chatwin_composing(&g_chat) && line[0] == '\0') {
            /* Enter on an empty line puts the field away, the other half
             * of Enter opening it. */
            chat_close_compose(0);
        } else if (mmo_chatwin_composing(&g_chat) && line[0] == '/'
                   && openmmo_gtl_local_command(line)) {
            /* /gtl, /sell and /sellitem are the trade link's own verbs;
             * nothing goes up the wire for the line itself. */
        } else if (mmo_chatwin_composing(&g_chat)) {
            /*
             * Where the line goes: a prefilled whisper target wins (the player-menu Whisper),
             * then the window's active tab. The Whispers tab is a reply to the last
             * correspondent; with none, the empty target is sent so the server's own "Whisper
             * who?" answers in the log.
             */
            const char *wt = g_whisper_name[0] != '\0' ? g_whisper_name
                             : g_chat_send_type == MMO_CHAT_WHISPER
                                 ? g_last_whisper
                                 : NULL;
            int rc;

            if (g_client == NULL)
                rc = -1;
            else if (wt != NULL && line[0] != '/')
                rc = openmmo_client_send_whisper(g_client, wt, line);
            else if (g_chat_send_type == MMO_CHAT_BATTLE && line[0] != '/')
                rc = openmmo_client_send_battle_chat(g_client, line);
            else
                rc = openmmo_client_send_chat_mode(
                    g_client,
                    line[0] == '/' || g_chat_send_type == MMO_CHAT_WHISPER
                        || g_chat_send_type == MMO_CHAT_BATTLE
                        ? MMO_CHAT_NORMAL
                        : g_chat_send_type,
                    line);
            if (rc != 0)
                printf("openmmo: chat send failed\n");
            else if (wt != NULL && line[0] != '/') {
                if (wt[0] != '\0') {
                    printf("openmmo: whisper sent to %s \"%s\"\n", wt, line);
                    snprintf(g_last_whisper, sizeof g_last_whisper, "%s", wt);
                } else
                    printf("openmmo: whisper with no one to reply to\n");
            } else
                printf("openmmo: chat sent [%d] \"%s\"\n",
                       line[0] == '/' || g_chat_send_type == MMO_CHAT_WHISPER
                           ? MMO_CHAT_NORMAL
                           : g_chat_send_type,
                       line);
            g_whisper_name[0] = '\0';
            /* A sent line goes back to the world, the way every MMO's
             * Enter does; a line the wire refused stays as the draft. */
            chat_close_compose(rc != 0);
        }
        osk_reset(&g_osk);
    } else if (osk_cancelled(&g_osk)) {
        printf("openmmo: keyboard cancelled\n");
        g_whisper_name[0] = '\0';
        if (mmo_creator_needs_entry(&g_creator))
            mmo_creator_back(&g_creator);
        else if (mmo_chatwin_composing(&g_chat))
            chat_close_compose(1);
        osk_reset(&g_osk);
    }
}

static void creator_sync_osk(void)
{
    if (mmo_creator_needs_entry(&g_creator)) {
        if (!g_creator_osk) {
            g_osk_surface = MMO_ENTRY_NAME;
            mmo_entry_open(&g_osk, MMO_ENTRY_NAME);
            g_creator_osk = 1;
            printf("openmmo: character create: name field\n");
        }
    } else if (g_creator_osk) {
        text_page_want(0);
        g_creator_osk = 0;
    }
}

static void creator_submit(void)
{
    if (g_offline_lobby && mmo_creator_wants_new(&g_creator)) {
        /* The row past the end, offline: no server to make a character on and
         * no four steps to walk, because the cartridge's own opening asks the
         * name and the gender itself. */
        printf("openmmo: offline character select: starting a new game\n");
        s_pending_gfx = -1;
        g_offline_new_game = 1;
        g_leave_field = 1;
        mmo_creator_begin_wait(&g_creator);
        return;
    }
    if (mmo_creator_has_pick(&g_creator)) {
        int idx = mmo_creator_pick_index(&g_creator);
        const mmo_character_list *list;

        if (g_offline_lobby) {
            /* Nobody to ask. The pick is the answer offline, and what it
             * opens is the save that is already loaded. */
            printf("openmmo: offline character select: continuing \"%s\"\n",
                   g_creator.list.entry[idx].name[0]
                       ? g_creator.list.entry[idx].name : "?");
            s_pending_gfx = -1;
            g_leave_field = 1;
            mmo_creator_begin_wait(&g_creator);
            return;
        }
        list = openmmo_client_characters(g_client);
        printf("openmmo: character select: picked \"%s\"\n",
               (list && idx >= 0 && idx < list->held)
                   ? list->entry[idx].name : "?");
        s_pending_gfx = -1;
        if (g_client != NULL && openmmo_client_pick_character(g_client, idx) != 0)
            printf("openmmo: character select: pick failed\n");
        else
            mmo_creator_begin_wait(&g_creator);
        return;
    }
    if (mmo_creator_has_delete(&g_creator)) {
        int idx = mmo_creator_delete_index(&g_creator);
        const mmo_character_list *list = openmmo_client_characters(g_client);

        if (list == NULL || idx < 0 || idx >= list->held) {
            printf("openmmo: character delete: that row is gone\n");
            return;
        }
        printf("openmmo: character delete: \"%s\"\n",
               list->entry[idx].name[0] ? list->entry[idx].name : "?");
        if (g_client == NULL
            || openmmo_client_delete_character(g_client,
                                               list->entry[idx].id) != 0)
            printf("openmmo: character delete: send failed\n");
        else
            mmo_creator_begin_wait(&g_creator);
        return;
    }
    if (mmo_creator_ready(&g_creator)) {
        mmo_create_character req;
        const mmo_appearance *body = mmo_appearance_at(g_creator.appear);

        s_pending_gfx = (body != NULL) ? body->gfx : -1;
        if (mmo_creator_fill_create(&g_creator, &req) != 0) {
            printf("openmmo: character create: could not fill the request\n");
            return;
        }
        printf("openmmo: character create: \"%s\" %s %s\n",
               req.name,
               req.gender ? "girl" : "boy",
               mmo_region_name(req.starting_region));
        if (g_client != NULL
            && openmmo_client_create_character(g_client, &req) != 0)
            printf("openmmo: character create: send failed\n");
        else
            mmo_creator_begin_wait(&g_creator);
    }
}

static void drive_creator(void)
{
    u32 rep = gSystem.pressedKeysRepeatable;
    u32 hit = gSystem.pressedKeys;

    if (!creator_live() || g_creator.step == MMO_CREATOR_WAIT)
        return;
    if (mmo_creator_needs_entry(&g_creator))
        return;

    if (rep & PAD_KEY_UP)
        mmo_creator_move(&g_creator, MMO_CREATOR_UP);
    if (rep & PAD_KEY_DOWN)
        mmo_creator_move(&g_creator, MMO_CREATOR_DOWN);
    if (rep & PAD_KEY_LEFT)
        mmo_creator_move(&g_creator, MMO_CREATOR_LEFT);
    if (rep & PAD_KEY_RIGHT)
        mmo_creator_move(&g_creator, MMO_CREATOR_RIGHT);
    if (hit & PAD_BUTTON_B)
        mmo_creator_back(&g_creator);
    if (hit & PAD_BUTTON_A) {
        mmo_creator_confirm(&g_creator);
        if (mmo_creator_has_pick(&g_creator) || mmo_creator_ready(&g_creator)
            || mmo_creator_has_delete(&g_creator))
            creator_submit();
    }
}

/* Compose is the field: the same osk.c the creator uses, now holding
 * the engine's own key tables. The field stays up. START flips the
 * window; this just opens and closes the widget under it. */
static void chat_sync_entry(void)
{
    if (!mmo_chatwin_needs_entry(&g_chat)) {
        if (g_chat_entry) {
            text_page_want(0);
            osk_reset(&g_osk);
            g_chat_entry = 0;
            printf("openmmo: chat log\n");
        }
        return;
    }

    if (!g_chat_entry) {
        size_t i;

        g_osk_surface = MMO_ENTRY_CHAT;
        if (!mmo_entry_open(&g_osk, MMO_ENTRY_CHAT)) {
            mmo_chatwin_toggle_compose(&g_chat);
            return;
        }
        for (i = 0; i < g_chat_draft_len; i++)
            osk_insert(&g_osk, g_chat_draft[i]);
        g_chat_entry = 1;
        printf("openmmo: chat compose\n");
    }
}

void openmmo_player_compose_whisper(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return;
    snprintf(g_whisper_name, sizeof g_whisper_name, "%s", name);
    mmo_chatwin_show(&g_chat);
    if (!mmo_chatwin_composing(&g_chat))
        mmo_chatwin_toggle_compose(&g_chat);
    printf("openmmo: whisper compose %s\n", g_whisper_name);
}

/* START opens and closes compose. Measured: the field's own input
 * table does not read START (menu is X, registered item is Y), so
 * this is a free button. */
static void drive_chat(void)
{
    if (!mmo_chatwin_visible(&g_chat))
        return;
    if ((gSystem.pressedKeys & PAD_BUTTON_START) == 0)
        return;
    if (mmo_chatwin_composing(&g_chat))
        chat_close_compose(1);
    else
        mmo_chatwin_toggle_compose(&g_chat);
    gSystem.pressedKeys &= ~PAD_BUTTON_START;
    gSystem.pressedKeysRepeatable &= ~PAD_BUTTON_START;
}

/* SELECT opens the debug menu. Measured free: the field's own input table
 * reads X and Y only (src/overlay005/field_control.c:128, :132), and START is
 * taken by compose above. The press is only latched here, the menu is a
 * field task and one cannot be started from inside the frame's input pass. */
static void drive_debug(void)
{
    if ((gSystem.pressedKeys & PAD_BUTTON_SELECT) == 0)
        return;
    gSystem.pressedKeys &= ~PAD_BUTTON_SELECT;
    gSystem.pressedKeysRepeatable &= ~PAD_BUTTON_SELECT;
    openmmo_debug_request_open();
}

/* One delivered line, broken to fit the window rather than cut off at its edge. */
#define CHAT_WRAP_COLS MMO_CHATWIN_APP_COLS
#define CHAT_WRAP_ROWS (OPENMMO_CHAT_LOG * 4)

static int chat_wrap(char rows[][CHAT_WRAP_COLS + 1], int *types, int cap,
                     int at, int type, const char *s)
{
    while (*s != '\0') {
        int take, cut;

        while (*s == ' ')
            s++;
        if (*s == '\0')
            break;

        take = 0;
        while (s[take] != '\0' && take < CHAT_WRAP_COLS)
            take++;
        cut = take;
        if (s[take] != '\0') {
            int i;

            for (i = take; i > 0; i--) {
                if (s[i] == ' ') {
                    cut = i;
                    break;
                }
            }
        }

        if (at == cap) {
            /* Full: the oldest row goes, because the newest is the one
             * somebody is waiting to read. */
            memmove(rows[0], rows[1], (size_t)(cap - 1) * (CHAT_WRAP_COLS + 1));
            memmove(types, types + 1, (size_t)(cap - 1) * sizeof *types);
            at--;
        }
        memcpy(rows[at], s, (size_t)cut);
        rows[at][cut] = '\0';
        types[at] = type;
        at++;
        s += cut;
    }
    return at;
}

static void draw_chat(void)
{
    openmmo_event ev[OPENMMO_CHAT_LOG];
    mmo_chatwin_line line[CHAT_WRAP_ROWS];
    static char rows[CHAT_WRAP_ROWS][CHAT_WRAP_COLS + 1];
    static int types[CHAT_WRAP_ROWS];
    char text[256];
    int n, i, m = 0;

    if (g_client == NULL)
        return;
    n = openmmo_client_chat(g_client, ev, OPENMMO_CHAT_LOG);
    for (i = 0; i < n; i++) {
        mmo_chatwin_line one;

        one.type = ev[i].chat.type;
        one.sender = ev[i].chat.sender;
        one.text = ev[i].chat.text;
        mmo_chatwin_format(&one, text, sizeof text);
        m = chat_wrap(rows, types, CHAT_WRAP_ROWS, m, one.type, text);
    }
    for (i = 0; i < m; i++) {
        line[i].type = types[i];
        line[i].sender = "";
        line[i].text = rows[i];
    }
    if (!mmo_chatwin_visible(&g_chat))
        return;
    openmmo_chat_draw(&g_chat, line, m, &g_osk);
}

static void creator_take_list(void)
{
    const mmo_character_list *list;
    const char *err;

    if (g_client == NULL)
        return;
    list = openmmo_client_characters(g_client);
    mmo_creator_set_list(&g_creator, list);
    err = openmmo_client_create_error(g_client);
    if (err != NULL && err[0] != '\0') {
        printf("openmmo: character create: refused: %s\n", err);
        mmo_creator_refuse(&g_creator, err);
    }
    err = openmmo_client_delete_error(g_client);
    if (err != NULL && err[0] != '\0')
        printf("openmmo: character delete: refused\n");
    printf("openmmo: character select: %d character(s)\n",
           list ? list->held : 0);
    if (!g_have_seat && !g_in_lobby)
        g_leave_lobby = 1;
}

static const openmmo_party_mon *battle_active_party_mon(void)
{
    const openmmo_party *p;
    const openmmo_battle_field *f;
    int i, j;

    p = openmmo_client_party(g_client);
    if (p == NULL || !p->valid || p->count <= 0)
        return NULL;
    f = openmmo_client_battle_field(g_client);
    if (f != NULL && f->valid) {
        for (i = 0; i < f->n_mons; i++) {
            if (f->mon[i].faint == 1)
                continue;
            for (j = 0; j < p->count; j++) {
                if ((u32)p->mon[j].id == f->mon[i].entity_id)
                    return &p->mon[j];
            }
        }
    }
    return &p->mon[0];
}

static int send_battle_move_slot(int slot)
{
    const openmmo_party_mon *mon;
    mmo_battle_select sel;

    mon = battle_active_party_mon();
    if (mon == NULL || slot < 0 || slot > 3 || mon->move_id[slot] == 0)
        return -1;
    memset(&sel, 0, sizeof sel);
    sel.action = MMO_BATTLE_ACTION_MOVE;
    sel.move_or_item = (s16)mon->move_id[slot];
    if (openmmo_client_battle_select(g_client, &sel) != 0)
        return -1;
    printf("openmmo: battle move slot %d id %u\n", slot, mon->move_id[slot]);
    return 0;
}

/* Hold a server-started battle until the run intent leaves it. A wild fight
 * auto-runs after a short hold so grass cannot freeze a player. A trainer or
 * player fight waits: A/X/Y/L send the active monster's four moves, B runs
 * or forfeits. */
static void drive_battle_hold(void)
{
    u32 hit;
    int slot;

    if (!session_in_battle())
        return;
    if (openmmo_encounter_scene_up())
        return;
    /* A fight held for the field to settle is not one with no scene: the
     * server's first turn arrives before the shake ends, and running from it
     * here would answer a battle the player has not seen open. */
    if (openmmo_encounter_deferred())
        return;
    if (openmmo_client_battle_state(g_client) != OPENMMO_BATTLE_ACTIVE)
        return;
    if (g_battle_ran)
        return;

    if (g_battle_wild) {
        g_battle_hold_frames++;
        if ((gSystem.pressedKeys & PAD_BUTTON_B) == 0
            && g_battle_hold_frames < BATTLE_HOLD_RUN_FRAMES)
            return;
        if (openmmo_client_battle_run(g_client) != 0)
            return;
        g_battle_ran = 1;
        printf("openmmo: asked to run from the battle\n");
        return;
    }

    hit = gSystem.pressedKeys;
    if (hit & PAD_BUTTON_B) {
        if (openmmo_client_battle_run(g_client) != 0)
            return;
        g_battle_ran = 1;
        printf("openmmo: asked to forfeit the battle\n");
        return;
    }
    if (hit & PAD_BUTTON_A)
        slot = 0;
    else if (hit & PAD_BUTTON_X)
        slot = 1;
    else if (hit & PAD_BUTTON_Y)
        slot = 2;
    else if (hit & PAD_BUTTON_L)
        slot = 3;
    else
        return;
    if (send_battle_move_slot(slot) != 0)
        return;
    g_battle_ran = 1;
}

static int g_ask_shop;
static int g_ask_shop_delay;
static int g_ask_dialog;
static int g_ask_dialog_delay;
static int g_ask_yesno;
static int g_ask_yesno_delay;
static int g_ask_menu;
static int g_ask_menu_delay;
static int g_ask_move;
static int g_ask_move_delay;

static void drive_shop_cmd(void)
{
    if (!g_ask_shop || g_ask_shop_delay < 0)
        return;
    if (g_ask_shop_delay > 0) {
        g_ask_shop_delay--;
        return;
    }
    if (!g_seated)
        return;
    if (openmmo_client_send_chat(g_client, "/shop") != 0)
        return;
    g_ask_shop_delay = -1;
    printf("openmmo: sending /shop\n");
}

static void drive_dialog_cmd(void)
{
    if (!g_ask_dialog || g_ask_dialog_delay < 0)
        return;
    if (g_ask_dialog_delay > 0) {
        g_ask_dialog_delay--;
        return;
    }
    if (!g_seated)
        return;
    if (openmmo_client_send_chat(g_client, "/dialog") != 0)
        return;
    g_ask_dialog_delay = -1;
    printf("openmmo: sending /dialog\n");
}

static void drive_yesno_cmd(void)
{
    if (!g_ask_yesno || g_ask_yesno_delay < 0)
        return;
    if (g_ask_yesno_delay > 0) {
        g_ask_yesno_delay--;
        return;
    }
    if (!g_seated)
        return;
    if (openmmo_client_send_chat(g_client, "/yesno") != 0)
        return;
    g_ask_yesno_delay = -1;
    printf("openmmo: sending /yesno\n");
}

static void drive_menu_cmd(void)
{
    if (!g_ask_menu || g_ask_menu_delay < 0)
        return;
    if (g_ask_menu_delay > 0) {
        g_ask_menu_delay--;
        return;
    }
    if (!g_seated)
        return;
    if (openmmo_client_send_chat(g_client, "/menu") != 0)
        return;
    g_ask_menu_delay = -1;
    printf("openmmo: sending /menu\n");
}

static void drive_move_cmd(void)
{
    if (!g_ask_move || g_ask_move_delay < 0)
        return;
    if (g_ask_move_delay > 0) {
        g_ask_move_delay--;
        return;
    }
    if (!g_seated)
        return;
    if (openmmo_client_send_chat(g_client, "/move") != 0)
        return;
    g_ask_move_delay = -1;
    printf("openmmo: sending /move\n");
}

/* OPENMMO_DUEL=<name>: challenge that player once the session is seated. */
static void drive_duel_cmd(void)
{
    char line[OPENMMO_ENTITY_NAME_MAX + 16];

    if (g_duel_target == NULL || g_duel_delay < 0)
        return;
    if (g_duel_delay > 0) {
        g_duel_delay--;
        return;
    }
    if (!g_seated)
        return;
    snprintf(line, sizeof line, "/challenge %s", g_duel_target);
    if (openmmo_client_send_chat(g_client, line) != 0)
        return;
    g_duel_delay = -1;
    printf("openmmo: sending %s\n", line);
}

static void drive_trade_cmd(void)
{
    if (g_trade_target == NULL || g_trade_delay < 0)
        return;
    if (g_trade_delay > 0) {
        g_trade_delay--;
        return;
    }
    if (!g_seated)
        return;
    if (openmmo_client_trade_request(g_client, g_trade_target) != 0)
        return;
    g_trade_delay = -1;
    printf("openmmo: offering %s a trade\n", g_trade_target);
}

static void drive_testbattle(void)
{
    const openmmo_party *party;

    if (!g_testbattle || g_testbattle_delay <= 0 || session_in_battle())
        return;
    if (--g_testbattle_delay > 0)
        return;

    party = openmmo_client_party(g_client);
    if (party != NULL && party->valid && party->count == 0) {
        printf("openmmo: OPENMMO_TESTBATTLE set but the party is empty\n");
        return;
    }
    if (openmmo_client_send_chat(g_client, "/testbattle") != 0)
        return;
    g_testbattle_round++;
    printf("openmmo: sending /testbattle (%d/%d)\n",
           g_testbattle_round, testbattle_rounds());
}

/*
 * -------------------------------------------------------------------------- * Remote-avatar
 * glue: turn the client's SPAWN/STEP/TURN/DESPAWN render events into real field avatars.
 */

/*
 * One PlayerAvatar per render slot (the model caps slots at the engine's 8-wide remote band).
 * Indexed by event.entity.slot.
 */
static PlayerAvatar *g_avatars[OPENMMO_ENTITY_NETID_CEIL];

/* The map the tracked remote entities belong to. */
static struct {
    FieldSystem *fs;
    int          mapId;
    int          valid;
} g_field_epoch;

/* Drop every cached avatar pointer without deleting through it. Used when the
 * object table they lived in has already been freed by the engine, deleting a
 * dangling PlayerAvatar would be a use-after-free, so the table's own teardown is
 * left to reclaim them and we simply forget the pointers. */
static long g_avatar_deletes; /* remote-band MapObject_Delete calls seen */

/* Map / cutscene npcs seated from the 0x12 store. Local ids sit above the
 * remote-player band so a spawned npc is never a PlayerAvatar. */
#define OPENMMO_NPC_LOCALID_BASE 0x200
#define OPENMMO_NPC_SEAT_MAX     OPENMMO_NPC_MAX
#define OPENMMO_MAP_OBJECT_RESERVE 4

static int map_objects_free(FieldSystem *fs);

static struct {
    s64 entity_id;
    int localid;
    int created;
    int live;
    /* The tile the 0x12 row named when a scripted walk took this npc over.
     * While the row still says that, the object's own tile is the live one
     * and must not be corrected back, see npc_tile_held(). */
    int anchored;
    int anchor_x, anchor_y;
} g_npc_seat[OPENMMO_NPC_SEAT_MAX];

static int npc_seats_any(void);

static void npc_seats_clear(void)
{
    memset(g_npc_seat, 0, sizeof g_npc_seat);
}
static long g_avatar_frees;   /* of those, ones that matched a cached avatar */

/* Defined with the rest of the peer-follower half, further down; a forgotten
 * avatar and a forgotten follower are one act and this is where it happens. */
static void peer_followers_forget(void);

static void forget_avatars(void)
{
    memset(g_avatars, 0, sizeof g_avatars);
    peer_followers_forget();
    openmmo_label_clear_all();
}

/* A remote player is two allocations and only one of them is the map's. */
void openmmo_map_object_deleting(void *mapObjVoid)
{
    MapObject *mapObj = mapObjVoid;
    PlayerAvatar *av;
    u32 localid;
    int slot;

    if (mapObj == NULL)
        return;

    localid = MapObject_GetLocalID(mapObj);
    if (localid < OPENMMO_ENTITY_LOCALID_BASE)
        return;

    slot = (int)(localid - OPENMMO_ENTITY_LOCALID_BASE);
    if (slot >= OPENMMO_ENTITY_NETID_CEIL)
        return;

    g_avatar_deletes++;
    av = g_avatars[slot];
    if (av == NULL || PlayerAvatar_GetMapObject(av) != mapObj)
        return;

    g_avatar_frees++;
    g_avatars[slot] = NULL;
    openmmo_label_clear(slot);
    Heap_FreeExplicit(HEAP_ID_FIELD2, av);
}

/*
 * HEAP_ID_FIELD2 itself is about to be destroyed, called from the field system's own
 * teardown.
 */
void openmmo_field_heap_destroyed(void)
{
    forget_avatars();
    /* The field going away takes the Underground with it, and the two engine
     * answers openmmo_underground.c gives must not outlive it. */
    openmmo_underground_forget();
}

/*
 * True if slot's cached avatar is still registered in the current object table. Safe across a
 * table rebuild: it queries the live table by the avatar's local id and never dereferences the
 * cached PlayerAvatar (which a map change may have freed).
 */
static int avatar_live(FieldSystem *fs, int slot)
{
    if (g_avatars[slot] == NULL)
        return 0;
    if (MapObjMan_LocalMapObjByIndex(fs->mapObjMan,
                                     OPENMMO_ENTITY_LOCALID_BASE + slot) == NULL) {
        g_avatars[slot] = NULL;
        return 0;
    }
    return 1;
}

/* How many players are on this map besides us, as the server last said. The
 * Underground scales what it buries by it (openmmo_underground.c). */
int openmmo_live_peer_count(void)
{
    openmmo_event evs[OPENMMO_ENTITY_NETID_CEIL];

    if (g_client == NULL || !g_seated)
        return 0;
    return openmmo_client_live_entities(g_client, evs,
                                        OPENMMO_ENTITY_NETID_CEIL);
}

static int openmmo_remote_at(FieldSystem *fs, int x, int z, char *name, int cap)
{
    openmmo_event evs[OPENMMO_ENTITY_NETID_CEIL];
    int n, i;
    const char *label;

    if (name == NULL || cap <= 0)
        return 0;
    name[0] = '\0';
    if (g_client != NULL) {
        n = openmmo_client_live_entities(g_client, evs, OPENMMO_ENTITY_NETID_CEIL);
        for (i = 0; i < n; i++) {
            if (evs[i].entity.x == x && evs[i].entity.z == z
                && evs[i].entity.name[0] != '\0') {
                snprintf(name, (size_t)cap, "%s", evs[i].entity.name);
                return 1;
            }
        }
    }
    if (fs == NULL || fs->playerAvatar == NULL)
        return 0;
    for (i = 0; i < OPENMMO_ENTITY_NETID_CEIL; i++) {
        if (!avatar_live(fs, i))
            continue;
        if (PlayerAvatar_GetXPos(g_avatars[i]) != x
            || PlayerAvatar_GetZPos(g_avatars[i]) != z)
            continue;
        label = openmmo_label_text(i);
        snprintf(name, (size_t)cap, "%s",
                 (label != NULL && label[0] != '\0') ? label : "?");
        return 1;
    }
    return 0;
}

static MapObject *npc_seated_object(FieldSystem *fs, s64 id);

/* Which map object a movement job's entity names: the local avatar, a seated
 * peer, or a cutscene npc the 0x12 store put on the map. */
static MapObject *script_move_object(FieldSystem *fs, s64 entity_id,
                                     int is_self, int slot)
{
    if (is_self && fs->playerAvatar != NULL)
        return PlayerAvatar_GetMapObject(fs->playerAvatar);
    if (slot >= 0 && slot < OPENMMO_ENTITY_NETID_CEIL && avatar_live(fs, slot))
        return PlayerAvatar_GetMapObject(g_avatars[slot]);
    return npc_seated_object(fs, entity_id);
}

static void play_script_move(FieldSystem *fs)
{
    if (fs == NULL || !FieldSystem_IsRunningFieldMap(fs) || g_client == NULL)
        return;
    /*
     * Nothing animates under a message box, and the objects are not even findable there, so a
     * job left running would spend its whole allowance of looks waiting for the player to
     * press A and be given up on, which is what killed every sequence in the bedroom one
     * line after it started.
     */
    if (FieldSystem_HasChildProcess(fs) || fs->task != NULL)
        return;
    openmmo_script_move_pump(fs, script_move_object);
}

/*
 * The engine's own "the field is safe to touch this frame" test, copied from pc/src/pc_lab.c's
 * lab_field_settled: a live running map with no map-change task in flight and no child process
 * (a battle or menu) stacked on top.
 */
static int field_settled(FieldSystem *fs)
{
    return fs != NULL
        && fs->task == NULL
        && FieldSystem_IsRunningFieldMap(fs)
        && !FieldSystem_HasChildProcess(fs);
}

/*
 * Settled is not enough to seat a peer: between a warp's arrival burst and the map change it
 * asks for, the field is idle on the MAP we are leaving.
 */
static int field_ready_for_peers(FieldSystem *fs)
{
    return field_settled(fs) && g_seated;
}

/* The client changed map on its own. Tell the server where it stands now. */
static int g_pending_local_warp = -1;

/* A warp this client is about to take on its own, told ahead of the map
 * change: the Pokegear's Fly. The header watcher below only arms on a header
 * Change, and a Fly to the town one is standing in changes nothing but the
 * tile; armed here, the landing is reported the same way once it settles. */
void openmmo_boot_expect_local_warp(int header)
{
    g_pending_local_warp = header;
    if (g_have_seat)
        g_seat.mapHeaderID = (enum MapHeaderID)header;
}

static void report_local_warp(FieldSystem *fs, int header)
{
    int x, z, dir;

    if (g_client == NULL || !g_seated || fs == NULL || fs->playerAvatar == NULL)
        return;
    x = PlayerAvatar_GetXPos(fs->playerAvatar);
    z = PlayerAvatar_GetZPos(fs->playerAvatar);
    dir = PlayerAvatar_GetFacingDir(fs->playerAvatar);
    printf("openmmo: local warp landed on header %d at (%d,%d) dir %d\n",
           header, x, z, dir);
    g_local_landing_header = header;
    g_local_landing_frames = LOCAL_LANDING_FRAMES;
    g_landing_mount = local_is_surfing();
    /* Landed: the guard above is the one that answers from here. */
    g_engine_warp_header = -1;
    g_engine_warp_frames = 0;
    openmmo_client_send_script_warp(g_client, header, x, z, dir);
    /* The seat must move with us. */
    Location_Set(&g_seat, (enum MapHeaderID)header, WARP_ID_NONE, x, z, dir);
    g_have_seat = 1;
    g_seated = 1;
}

/* After a local scene, say where it left us. */
static int g_scene_ran;

static void report_scene_position(FieldSystem *fs)
{
    extern void openmmo_party_mark_touched(void); /* openmmo_encounter.c */

    if (g_client == NULL)
        return;
    if (g_scene_ran && g_seated
        && fs != NULL && fs->location != NULL && fs->playerAvatar != NULL) {
        g_scene_ran = 0;
        report_local_warp(fs, (int)fs->location->mapHeaderID);
    }
    /*
     * A scene can also fight: the VM's own StartTrainerBattle never crosses the wild-path
     * scene tracker, so its exp went unreported until the next wild fight happened to carry
     * it. Every scene's end reports the party now, idempotent when nothing moved.
     */
    if (g_scene_fought) {
        g_scene_fought = 0;
        openmmo_party_mark_touched();
    }
}

/* Fold a server (region, bank, map, tile) into g_seat. 0 if the map
 * has no engine header, the avatar stays where it is and the log
 * names why, rather than guessing a map. */
static int seat_from_server(int region, int bank, int map, int x, int z, int dir)
{
    const char *why = NULL;
    int header = mmo_id_map_header_from_server(region, bank, map, &why);

    if (header < 0) {
        printf("openmmo: cannot seat on region %d bank %d map %d: %s\n",
               region, bank, map, why != NULL ? why : "untranslatable");
        return 0;
    }
    Location_Set(&g_seat, (enum MapHeaderID)header, WARP_ID_NONE, x, z,
                 dir >= 0 ? dir : FACE_DOWN);
    g_have_seat = 1;
    g_seated = 0;
    g_seat_engine = 0;
    return 1;
}

static void snap_avatar(FieldSystem *fs, int x, int z, int dir)
{
    if (fs->playerAvatar != NULL) {
        /*
         * The engine teleport underneath writes the height as if the ground were at zero
         * (MapObject_SetPosDirFromCoords passes y=0), so a correction on a hill draws the
         * player inside the terrain and then fails the height compare every later step asks,
         * a clamped player stayed clamped.
         */
        MapObject *obj = PlayerAvatar_GetMapObject(fs->playerAvatar);
        VecFx32 kept;
        int have = 0;

        if (obj != NULL) {
            MapObject_GetPosPtr(obj, &kept);
            have = 1;
        }
        PlayerAvatar_SetPosDirFromCoords(fs->playerAvatar, x, z, dir);
        if (have) {
            VecFx32 pos;
            int settled;

            MapObject_GetPosPtr(obj, &pos);
            pos.y = kept.y;
            MapObject_SetPos(obj, &pos);
            MapObject_SetY(obj, ((kept.y) >> 3) / FX32_ONE);
            settled = MapObject_RecalculateObjectHeight(obj);
            /* What the terrain made of the height we carried over. */
            MapObject_GetPosPtr(obj, &pos);
            printf("openmmo: the correction carried height %d, the terrain %s"
                   " %d\n", (int)(kept.y / FX32_ONE),
                   settled ? "put it at" : "could not answer so it stands at",
                   (int)(pos.y / FX32_ONE));
        }
    }
    if (fs->location != NULL) {
        fs->location->x = x;
        fs->location->z = z;
        fs->location->faceDirection = dir;
    }
}

/*
 * A live avatar on a running map. Weaker than field_settled: a new-game tv script (or any
 * other field task) still has a player we can snap, and a BOOT_WORLD join has to snap then or
 * it stays on the local tile for the whole cutscene.
 */
static int field_has_avatar(FieldSystem *fs)
{
    return fs != NULL
        && fs->location != NULL
        && fs->playerAvatar != NULL
        && FieldSystem_IsRunningFieldMap(fs);
}

/* Whether the server has the local player on the water. The badge and the party
 * that decide it are the server's and so is the tile, so this is read and not
 * asked (client.h, openmmo_client_transportation). */
static int local_is_surfing(void)
{
    return g_client != NULL
        && (openmmo_client_transportation(g_client) & MMO_TRANSPORT_SURFING) != 0;
}

/* Whether the server has Strength on for this visit. The party and the badge
 * that decide it are the server's, so this is read and not asked. */
static int local_strength_active(void)
{
    return g_client != NULL
        && openmmo_story_flag_is_set(openmmo_client_story_store(g_client),
                                     MMO_FLAG_STRENGTH_ACTIVE);
}

/*
 * Put FLAG_STRENGTH_ACTIVE where the engine reads it before it will slide a boulder. Safe to
 * call every frame: the engine also clears the flag on a map change, and without this the
 * server's set would not survive that clear.
 */
static void apply_local_strength(FieldSystem *fs)
{
    VarsFlags *vf;

    if (fs == NULL || fs->saveData == NULL || g_client == NULL)
        return;
    if (!local_strength_active())
        return;
    vf = SaveData_GetVarsFlags(fs->saveData);
    if (VarsFlags_CheckFlag(vf, MMO_FLAG_STRENGTH_ACTIVE))
        return;
    VarsFlags_SetFlag(vf, MMO_FLAG_STRENGTH_ACTIVE);
    printf("openmmo: strength on\n");
}

/* Put FLAG_HAS_PARTNER where LockAll and item-use read it. The walk is
 * the FOLLOW_PLAYER movement type on the seated MapObject, applied in
 * apply_npcs; this flag is only the engine's "a partner is present". */
static void apply_local_partner(FieldSystem *fs)
{
    VarsFlags *vf;
    int want, have;

    if (fs == NULL || fs->saveData == NULL || g_client == NULL)
        return;
    vf = SaveData_GetVarsFlags(fs->saveData);
    want = openmmo_story_flag_is_set(openmmo_client_story_store(g_client),
                                     MMO_FLAG_HAS_PARTNER);
    have = VarsFlags_CheckFlag(vf, MMO_FLAG_HAS_PARTNER);
    if (want == have)
        return;
    if (want)
        VarsFlags_SetFlag(vf, MMO_FLAG_HAS_PARTNER);
    else
        VarsFlags_ClearFlag(vf, MMO_FLAG_HAS_PARTNER);
    printf("openmmo: partner %s\n", want ? "on" : "off");
}

static int npc_seat_index(s64 id)
{
    int i;

    if (id == 0)
        return -1;
    for (i = 0; i < OPENMMO_NPC_SEAT_MAX; i++)
        if (g_npc_seat[i].live && g_npc_seat[i].entity_id == id)
            return i;
    return -1;
}

static int npc_store_has(const openmmo_npc_store *s, s64 id)
{
    int i;

    if (s == NULL || id == 0)
        return 0;
    for (i = 0; i < OPENMMO_NPC_MAX; i++)
        if (s->npc[i].live && s->npc[i].entity_id == id)
            return 1;
    return 0;
}

static MapObject *npc_object_by_localid(FieldSystem *fs, int localid)
{
    if (fs == NULL || fs->mapObjMan == NULL || localid < 0)
        return NULL;
    return MapObjMan_LocalMapObjByIndex(fs->mapObjMan, localid);
}

static MapObject *npc_find_existing(FieldSystem *fs, int gfx, int x, int z)
{
    MapObject *obj = NULL;
    MapObject *self = NULL;
    int idx = 0;
    u32 localid;

    if (fs == NULL || fs->mapObjMan == NULL)
        return NULL;
    if (fs->playerAvatar != NULL)
        self = PlayerAvatar_GetMapObject(fs->playerAvatar);
    while (MapObjectMan_FindObjectWithStatus(fs->mapObjMan, &obj, &idx,
                                             MAP_OBJ_STATUS_0) == TRUE) {
        if (obj == self)
            continue;
        localid = MapObject_GetLocalID(obj);
        if (localid >= OPENMMO_ENTITY_LOCALID_BASE
            && localid < OPENMMO_NPC_LOCALID_BASE)
            continue;
        if ((int)MapObject_GetGraphicsID(obj) == gfx
            && MapObject_GetX(obj) == x
            && MapObject_GetZ(obj) == z)
            return obj;
    }
    return NULL;
}

static MapObject *npc_seated_object(FieldSystem *fs, s64 id)
{
    int i = npc_seat_index(id);

    if (i < 0)
        return NULL;
    return npc_object_by_localid(fs, g_npc_seat[i].localid);
}

static int npc_seats_any(void)
{
    int i;

    for (i = 0; i < OPENMMO_NPC_SEAT_MAX; i++)
        if (g_npc_seat[i].live)
            return 1;
    return 0;
}

static void npc_seat_drop(FieldSystem *fs, int i)
{
    MapObject *obj;

    if (i < 0 || i >= OPENMMO_NPC_SEAT_MAX || !g_npc_seat[i].live)
        return;
    obj = npc_object_by_localid(fs, g_npc_seat[i].localid);
    if (obj != NULL) {
        if (g_npc_seat[i].created)
            MapObject_Delete(obj);
        else
            MapObject_SetFlagAndDeleteObject(obj);
    }
    memset(&g_npc_seat[i], 0, sizeof g_npc_seat[i]);
}

/* The decomp's LockAll / ReleaseAll, driven by the server's script lock. */
static int g_objs_paused;

static void apply_script_lock(FieldSystem *fs)
{
    int want;

    if (fs == NULL || fs->mapObjMan == NULL || g_client == NULL)
        return;
    if (!FieldSystem_IsRunningFieldMap(fs)) {
        /* A new map's objects come up unpaused, so the belief has to go with
         * the old ones or the next scene would never pause at all. */
        g_objs_paused = 0;
        return;
    }
    want = openmmo_client_in_dialog(g_client) ? 1 : 0;
    if (want == g_objs_paused)
        return;
    g_objs_paused = want;
    if (want)
        MapObjectMan_PauseAllMovement(fs->mapObjMan);
    else
        MapObjectMan_UnpauseAllMovement(fs->mapObjMan);
    printf("openmmo: map objects %s for the scene\n",
           want ? "locked" : "released");
    fflush(stdout);
}

/*
 * Whether a scripted walk owns this npc's tile this frame, so the 0x12 row must not be written
 * onto its map object.
 */
static int npc_tile_held(int si, const openmmo_npc *n)
{
    if (openmmo_script_move_holds(n->entity_id))
        return 1;
    if (si < 0 || !g_npc_seat[si].anchored)
        return 0;
    if (n->x == g_npc_seat[si].anchor_x && n->y == g_npc_seat[si].anchor_y)
        return 1;
    g_npc_seat[si].anchored = 0;
    return 0;
}

/* Seat every npc the 0x12 store holds. A row that matches a map object
 * already on that tile is a bind; one that does not is AddMapObject.
 * FOLLOW_PLAYER switches the movement type so the engine walks them. */
static void apply_npcs(FieldSystem *fs)
{
    const openmmo_npc_store *store;
    int i, s, used;
    MapObject *obj;
    enum MapHeaderID header;

    if (fs == NULL || fs->mapObjMan == NULL || g_client == NULL)
        return;
    if (!field_ready_for_peers(fs)) {
        /*
         * A SEAT belongs TO the MAP, not TO the frame. A dialog box and a menu are child
         * processes, so the field is "not settled" for as long as one is open, and
         * forgetting the seats there lost which map object each npc was.
         */
        if (!FieldSystem_IsRunningFieldMap(fs) || !g_seated) {
            if (npc_seats_any())
                printf("openmmo: npc seats cleared (map %d, seated %d)\n",
                       FieldSystem_IsRunningFieldMap(fs) ? 1 : 0, g_seated);
            npc_seats_clear();
        }
        return;
    }
    store = openmmo_client_npcs(g_client);
    if (store == NULL)
        return;
    header = fs->location != NULL
                 ? fs->location->mapHeaderID
                 : (enum MapHeaderID)0;

    for (i = 0; i < OPENMMO_NPC_SEAT_MAX; i++) {
        if (!g_npc_seat[i].live)
            continue;
        if (!npc_store_has(store, g_npc_seat[i].entity_id))
            npc_seat_drop(fs, i);
    }

    used = 0;
    for (i = 0; i < OPENMMO_NPC_MAX; i++) {
        const openmmo_npc *n = &store->npc[i];
        int created = 0;
        int localid;
        int si;

        if (!n->live)
            continue;
        si = npc_seat_index(n->entity_id);
        if (si >= 0 && !g_npc_seat[si].anchored
            && openmmo_script_move_holds(n->entity_id)) {
            g_npc_seat[si].anchored = 1;
            g_npc_seat[si].anchor_x = n->x;
            g_npc_seat[si].anchor_y = n->y;
            printf("openmmo: npc %lld walking, its tile is its own"
                   " (row says (%d,%d))\n",
                   (long long)n->entity_id, n->x, n->y);
            fflush(stdout);
        }
        obj = (si >= 0) ? npc_object_by_localid(fs, g_npc_seat[si].localid)
                        : NULL;
        if (obj == NULL)
            obj = npc_find_existing(fs, n->graphics_id, n->x, n->y);
        if (obj == NULL) {
            /* A LIVE SEAT already says this NPC has an object. */
            if (si >= 0 && g_npc_seat[si].live)
                continue;
            if (map_objects_free(fs) <= OPENMMO_MAP_OBJECT_RESERVE)
                continue;
            obj = MapObjectMan_AddMapObject(fs->mapObjMan, n->x, n->y,
                                            n->facing, n->graphics_id,
                                            n->movement, header);
            if (obj == NULL)
                continue;
            localid = OPENMMO_NPC_LOCALID_BASE + used;
            MapObject_SetLocalID(obj, (u32)localid);
            created = 1;
            printf("openmmo: npc spawn id %lld gfx %d at (%d,%d) dir %d"
                   " movetype %d follow=%d\n",
                   (long long)n->entity_id, n->graphics_id, n->x, n->y,
                   n->facing, n->movement,
                   n->movement == MMO_MOVEMENT_FOLLOW_PLAYER);
            fflush(stdout);
        } else {
            localid = (int)MapObject_GetLocalID(obj);
            if ((MapObject_GetX(obj) != n->x || MapObject_GetZ(obj) != n->y)
                && !npc_tile_held(si, n)) {
                printf("openmmo: npc %lld corrected (%d,%d) -> (%d,%d)\n",
                       (long long)n->entity_id, MapObject_GetX(obj),
                       MapObject_GetZ(obj), n->x, n->y);
                fflush(stdout);
                MapObject_SetPosDirFromCoords(obj, n->x, 0, n->y, n->facing);
            }
        }
        if ((int)MapObject_GetMovementType(obj) != n->movement
            && !npc_tile_held(si, n)) {
            MapObject_SwitchMovementType(obj, (u32)n->movement);
            if (n->movement == MMO_MOVEMENT_FOLLOW_PLAYER)
                MapObject_SetFlagIsPersistent(obj, TRUE);
        }
        if (si < 0) {
            for (s = 0; s < OPENMMO_NPC_SEAT_MAX; s++) {
                if (!g_npc_seat[s].live) {
                    si = s;
                    break;
                }
            }
        }
        if (si >= 0) {
            if (g_npc_seat[si].live)
                created = created || g_npc_seat[si].created;
            g_npc_seat[si].entity_id = n->entity_id;
            g_npc_seat[si].localid = localid;
            g_npc_seat[si].created = created;
            g_npc_seat[si].live = 1;
        }
        used++;
    }
}

/*
 * Put the avatar in the state the server says it is riding, through the engine's own state
 * change.
 */
static void apply_local_mount(FieldSystem *fs)
{
    int want, have;

    if (!field_has_avatar(fs) || g_client == NULL)
        return;
    if (!field_settled(fs))
        return;
    /* This is a surf correction, not A STATE correction, and the difference is the bicycle. */
    have = PlayerAvatar_GetPlayerState(fs->playerAvatar);
    if ((have == PLAYER_AVATAR_SURFING) == (local_is_surfing() != 0))
        return;
    want = local_is_surfing() ? PLAYER_AVATAR_SURFING : PLAYER_AVATAR_WALKING;
    if (g_local_landing_frames > 0 && local_is_surfing() == g_landing_mount)
        return;
    PlayerAvatar_SetTransitionState(fs->playerAvatar,
                                    want == PLAYER_AVATAR_SURFING
                                        ? PLAYER_TRANSITION_SURFING
                                        : PLAYER_TRANSITION_WALKING);
    PlayerAvatar_RequestChangeState(fs->playerAvatar);
    printf("openmmo: avatar state %d -> %d\n", have, want);
}

/*
 * Seat the chosen catalog body on the local avatar. GameStartNewSave builds Lucas/Dawn from
 * gender; a SkinSet type >= 512 names the look picked in the lobby.
 */
static void apply_local_body(FieldSystem *fs)
{
    MapObject *obj;
    int want, have;

    if (!field_has_avatar(fs) || g_client == NULL || !session_configured())
        return;
    /* SURFING is the only STATE that takes the sprite off a chosen body. */
    if (PlayerAvatar_GetPlayerState(fs->playerAvatar) == PLAYER_AVATAR_SURFING)
        return;
    obj = PlayerAvatar_GetMapObject(fs->playerAvatar);
    if (obj == NULL)
        return;
    want = openmmo_client_body_gfx(g_client);
    if (want == mmo_appearance_gender_gfx(s_player_gender >= 0 ? s_player_gender : 0)
        && s_pending_gfx >= 0)
        want = s_pending_gfx;
    /*
     * A composed look has a sheet a state (openmmo_look.c), and the engine picks it itself
     * through the patched Player_GetSpriteFromStateAndGender on every transition; what is
     * seated here is the walk, and only while the avatar is walking, so a ride or a cast keeps
     * the sheet the engine just chose.
     */
    if (mmo_appearance_look_of_gfx(want) >= 0) {
        int look = mmo_appearance_look_of_gfx(want);
        int state = PlayerAvatar_GetPlayerState(fs->playerAvatar);

        want = openmmo_look_state_gfx(look, state);
        if (want < 0)
            want = mmo_appearance_gender_gfx(s_player_gender >= 0 ? s_player_gender : 0);
    }
    have = (int)MapObject_GetGraphicsID(obj);
    if (want == have)
        return;
    /* sub_02061AD4 tears down the old draw first. AB4 alone left Lucas
     * standing on the spawn tile. */
    sub_02061AD4(obj, want);
    printf("openmmo: local body gfx %d -> %d\n", have,
           (int)MapObject_GetGraphicsID(obj));
}

/* How far the avatar may be moved without reloading the ground under it: half
 * a land-data chunk, which is 32 tiles square. */
#define SEAT_SNAP_TILES 16

/* The server's warp, taken as the engine's own transition. */
extern void sub_02056BDC(FieldSystem *fs, int header, int warpId, int x, int z, int dir, int type);
extern void sub_02056C18(FieldSystem *fs, int header, int warpId, int x, int z, int dir);
extern void FieldSystem_StartMapChangeWarpTask(FieldSystem *fs, int header, int warpId);

static int engine_transition_to_seat(FieldSystem *fs)
{
    PlayerAvatar *av = fs->playerAvatar;
    const WarpEvent *w;
    MapObject *self;
    u8 b;
    int x, z, idx, dir, hdr, wid, moving;

    if (av == NULL)
        return 0;
    x = PlayerAvatar_GetXPos(av);
    z = PlayerAvatar_GetZPos(av);
    dir = PlayerAvatar_GetFacingDir(av);
    /* The ride's own gate, not MapObject_IsMoving: a step is a movement
     * action (MAP_OBJ_STATUS_4 set until it lands), which IsMoving does not
     * read, and the r1021 log still had the two assertions on every
     * escalator. LocalMapObj_IsAnimationSet is what ov5_021D4A24 asks. */
    self = PlayerAvatar_GetMapObject(av);
    moving = self != NULL && !LocalMapObj_IsAnimationSet(self);
    idx = MapHeaderData_GetIndexOfWarpEventAtPos(fs, x, z);
    if (idx != -1) {
        w = MapHeaderData_GetWarpEventByIndex(fs, idx);
        if (w == NULL || (int)w->destHeaderID != (int)g_seat.mapHeaderID)
            return 0;
        hdr = w->destHeaderID;
        wid = w->destWarpID;
        b = TerrainCollisionManager_GetTileBehavior(fs, x, z);
        /*
         * A stair's and an escalator's departure is a walk the player is given (WALK_SLOW, the
         * ride's 0xa/0xb), and the server's answer to the step onto the tile arrives while
         * that step is still being walked: a map object already has its tile then, so the
         * event is found, but LocalMapObj_IsAnimationSet says no to a moving object and the
         * ride asserts twice and skips itself, the owner's "it won't always move you along
         * the escalator" (r1018, every escalator in the log).
         */
        if (TileBehavior_IsWarpStairsEast(b)) {
            if (moving)
                return -1;
            sub_02056BDC(fs, hdr, wid, 0, 0, DIR_EAST, 3);
        } else if (TileBehavior_IsWarpStairsWest(b)) {
            if (moving)
                return -1;
            sub_02056BDC(fs, hdr, wid, 0, 0, DIR_WEST, 3);
        } else if (TileBehavior_IsEscalatorFlipFace(b)) {
            if (moving)
                return -1;
            sub_02056BDC(fs, hdr, wid, 0, 0, dir == DIR_WEST ? DIR_EAST : DIR_WEST, 2);
        } else if (TileBehavior_IsEscalator(b)) {
            if (moving)
                return -1;
            sub_02056BDC(fs, hdr, wid, 0, 0, dir == DIR_WEST ? DIR_WEST : DIR_EAST, 2);
        } else if (TileBehavior_IsWarpEntranceSouth(b) || TileBehavior_IsWarpSouth(b)) {
            sub_02056C18(fs, hdr, wid, 0, 0, DIR_SOUTH);
        } else if (TileBehavior_IsWarpEntranceEast(b) || TileBehavior_IsWarpEast(b)) {
            sub_02056C18(fs, hdr, wid, 0, 0, DIR_EAST);
        } else if (TileBehavior_IsWarpEntranceWest(b) || TileBehavior_IsWarpWest(b)) {
            sub_02056C18(fs, hdr, wid, 0, 0, DIR_WEST);
        } else if (TileBehavior_IsWarpEntranceNorth(b) || TileBehavior_IsWarpNorth(b)) {
            sub_02056C18(fs, hdr, wid, 0, 0, DIR_NORTH);
        } else if (TileBehavior_IsWarpPanel(b)) {
            FieldSystem_StartMapChangeWarpTask(fs, hdr, wid);
        } else {
            return 0;
        }
        printf("openmmo: the server's warp is the tile's own; the engine"
               " takes it (header %d, tile behaviour 0x%02x)\n", hdr, b);
        return 1;
    }
    /* A door faced, which the engine enters on a press. */
    x += MapObject_GetDxFromDir(dir);
    z += MapObject_GetDzFromDir(dir);
    if (!TileBehavior_IsDoor(TerrainCollisionManager_GetTileBehavior(fs, x, z)))
        return 0;
    idx = MapHeaderData_GetIndexOfWarpEventAtPos(fs, x, z);
    if (idx == -1)
        return 0;
    w = MapHeaderData_GetWarpEventByIndex(fs, idx);
    if (w == NULL || (int)w->destHeaderID != (int)g_seat.mapHeaderID)
        return 0;
    sub_02056BDC(fs, w->destHeaderID, w->destWarpID, 0, 0, dir, 1);
    printf("openmmo: the server's warp is the door faced; the engine takes it"
           " (header %d)\n", (int)w->destHeaderID);
    return 1;
}

static void finish_seat(FieldSystem *fs);

/* Load the server's map, or snap the avatar onto its tile if that map
 * is already up. Safe to call every frame: a map change in flight or
 * an already-applied seat is a no-op. */
static void try_apply_seat(FieldSystem *fs)
{
    Location *saved;

    if (g_local_landing_frames > 0)
        g_local_landing_frames--;
    if (g_engine_warp_frames > 0 && --g_engine_warp_frames == 0)
        g_engine_warp_header = -1;
    if (!g_have_seat || g_seated || !field_has_avatar(fs))
        return;

    if ((int)fs->location->mapHeaderID != (int)g_seat.mapHeaderID) {
        if (!field_settled(fs))
            return;
        /*
         * The engine announced this very warp and has not landed yet, so its transition is the
         * one bringing us there and a load started here would be the second one, the flash
         * the rule above exists to stop.
         */
        if (g_seat_engine && g_engine_warp_frames > 0
            && g_engine_warp_header == (int)g_seat.mapHeaderID)
            return;
        if (g_seat_reload) {
            int taken = engine_transition_to_seat(fs);

            if (taken < 0)
                return; /* the step onto the tile is still being walked */
            if (taken > 0) {
                /* The engine's own task is the map change now; its landing
                 * on the seat's header brings us back here on the
                 * same-header path, which waits for the walk out and
                 * reports where the engine put us (g_seat_engine). */
                g_seat_reload = 0;
                g_seat_engine = 1;
                return;
            }
        }
        saved = FieldOverworldState_GetPlayerLocation(
            SaveData_GetFieldOverworldState(fs->saveData));
        *saved = g_seat;
        g_seat_reload = 0;
        pc_lab_start_map_change(fs, &g_seat);
        printf("openmmo: loading header %d at (%d,%d)\n",
               (int)g_seat.mapHeaderID, g_seat.x, g_seat.z);
        return;
    }

    if (g_seat_engine) {
        MapObject *self = fs->playerAvatar != NULL
                              ? PlayerAvatar_GetMapObject(fs->playerAvatar) : NULL;

        if (!field_settled(fs) || self == NULL || !LocalMapObj_IsAnimationSet(self))
            return;
        /*
         * Seated first: the report refuses while the seat is pending, and the first cut of
         * this let it refuse in silence, cleared the flag, and fell to the snap below on the
         * next frame, the player seen riding off an escalator and put back on it, facing the
         * way the server's row said (owner, r1018).
         */
        g_seat_engine = 0;
        g_seated = 1;
        report_local_warp(fs, (int)fs->location->mapHeaderID);
        finish_seat(fs);
        return;
    }

    /*
     * Same header is not the same ground. A DS overworld map is a stretch of a shared 960x960
     * plane, and the land data under it is streamed a chunk at a time around wherever the
     * player is standing, on a map change, which is the only thing that asks for it.
     */
    {
        int dx = PlayerAvatar_GetXPos(fs->playerAvatar) - g_seat.x;
        int dz = PlayerAvatar_GetZPos(fs->playerAvatar) - g_seat.z;

        if (dx < 0) dx = -dx;
        if (dz < 0) dz = -dz;
        /* A warp reloads even at distance zero: the server faded the screen
         * and opened a transition, and only a map change closes both. */
        if (g_seat_reload || dx > SEAT_SNAP_TILES || dz > SEAT_SNAP_TILES) {
            if (!field_settled(fs))
                return;
            saved = FieldOverworldState_GetPlayerLocation(
                SaveData_GetFieldOverworldState(fs->saveData));
            *saved = g_seat;
            g_seat_reload = 0;
            pc_lab_start_map_change(fs, &g_seat);
            printf("openmmo: re-loading header %d at (%d,%d); %d,%d tiles"
                   " from a warp or past the loaded land\n",
                   (int)g_seat.mapHeaderID, g_seat.x, g_seat.z, dx, dz);
            return;
        }
    }

    if (fs->location->x != g_seat.x || fs->location->z != g_seat.z
        || PlayerAvatar_GetXPos(fs->playerAvatar) != g_seat.x
        || PlayerAvatar_GetZPos(fs->playerAvatar) != g_seat.z)
        snap_avatar(fs, g_seat.x, g_seat.z, g_seat.faceDirection);

    finish_seat(fs);
}

/* The seat is taken: the player stands on the map the server named, by a
 * snap or by the engine's own transition. Everything the map is dressed with
 * once that is true, the same list either way. */
static void finish_seat(FieldSystem *fs)
{
    g_seated = 1;
    /* GameStartNewSave has rolled a trainer id and re-inited the block the
     * name and money live in; write those again. The id it rolled stands
     * until the save-block seat replaces it with this character's own. */
    seat_trainer(fs->saveData);
    seat_badges(fs->saveData);
    openmmo_bag_mark_dirty();
    openmmo_bag_sync(fs->saveData, FieldSystem_HasChildProcess(fs));
    apply_local_mount(fs);
    apply_local_body(fs);
    apply_local_strength(fs);
    apply_local_partner(fs);
    apply_npcs(fs);
    apply_script_lock(fs);
    printf("openmmo: standing on header %d at (%d,%d) dir %d\n",
           (int)fs->location->mapHeaderID,
           PlayerAvatar_GetXPos(fs->playerAvatar),
           PlayerAvatar_GetZPos(fs->playerAvatar),
           PlayerAvatar_GetFacingDir(fs->playerAvatar));
}

/* What a real map load left free, under OPENMMO_HEAP_REPORT=1. */
/*
 * Every labelled remote player, projected and painted, at the moment the frame is otherwise
 * finished.
 */
static void report_peers(uint64_t frame);

static void draw_labels(uint64_t frame)
{
    FieldSystem *fs = pc_lab_field_system();
    int slot;

    /* The map going away clears the plates; a field task only holds them. */
    if (fs == NULL || !FieldSystem_IsRunningFieldMap(fs)) {
        openmmo_label_frame(frame);
        return;
    }
    if (!field_settled(fs)) {
        openmmo_label_hold(frame);
        return;
    }
    openmmo_label_frame(frame);

    for (slot = 0; slot < OPENMMO_ENTITY_NETID_CEIL; slot++) {
        if (!avatar_live(fs, slot))
            continue;
        openmmo_label_draw_for(slot, PlayerAvatar_GetMapObject(g_avatars[slot]));
    }

    report_peers(frame);
}

/*
 * Where each peer is, said three ways, once a second under OPENMMO_PEER_REPORT=1: the tile the
 * model holds for them, the tile their avatar is actually standing on, and the local player's
 * own.
 */
static void report_peers(uint64_t frame)
{
    static int on = -1;
    FieldSystem *fs;
    openmmo_event evs[OPENMMO_ENTITY_NETID_CEIL];
    int n, i, slot;

    if (on < 0) {
        const char *e = getenv("OPENMMO_PEER_REPORT");
        on = (e != NULL && e[0] != '\0' && e[0] != '0');
    }
    if (!on || (frame % 60) != 0 || g_client == NULL)
        return;
    fs = pc_lab_field_system();
    if (fs == NULL || !field_settled(fs))
        return;

    n = openmmo_client_live_entities(g_client, evs, OPENMMO_ENTITY_NETID_CEIL);
    for (i = 0; i < n; i++) {
        slot = evs[i].entity.slot;
        if (slot < 0 || slot >= OPENMMO_ENTITY_NETID_CEIL)
            continue;
        printf("openmmo: peer slot %d \"%s\" model (%d,%d) dir %d, avatar ",
               slot, evs[i].entity.name, evs[i].entity.x, evs[i].entity.z,
               evs[i].entity.dir);
        if (avatar_live(fs, slot))
            printf("(%d,%d) dir %d", PlayerAvatar_GetXPos(g_avatars[slot]),
                   PlayerAvatar_GetZPos(g_avatars[slot]),
                   PlayerAvatar_GetFacingDir(g_avatars[slot]));
        else
            printf("none");
        if (fs->playerAvatar != NULL)
            printf(", self (%d,%d) dir %d",
                   PlayerAvatar_GetXPos(fs->playerAvatar),
                   PlayerAvatar_GetZPos(fs->playerAvatar),
                   PlayerAvatar_GetFacingDir(fs->playerAvatar));
        printf("\n");
    }
    fflush(stdout);
}

static int cached_avatars(void)
{
    int slot, n = 0;

    for (slot = 0; slot < OPENMMO_ENTITY_NETID_CEIL; slot++) {
        if (g_avatars[slot] != NULL)
            n++;
    }

    return n;
}

/*
 * What the field was actually built with, read back where it can be read back. The object
 * table is the engine's own accessor over the table it built, so this says 64 rather than 80
 * if the patch that carries our number stopped applying.
 */
static void pool_report(const FieldSystem *fs)
{
    extern int openmmo_overworld_anim_capacity(void);
    extern int openmmo_texture_slot_capacity(void);

    fprintf(stderr,
            "openmmo: pools, map objects %d built, anims %d asked,"
            " textures %d asked\n",
            MapObjectMan_GetMaxObjects(fs->mapObjMan),
            openmmo_overworld_anim_capacity(),
            openmmo_texture_slot_capacity());
    fflush(stderr);
}

static void heap_report_tick(const FieldSystem *fs, int settled, int mapId)
{
    extern void openmmo_heap_report(const char *when);
    extern int openmmo_heap_report_enabled(void);
    static int reported_map = -2;
    static long since;

    if (!openmmo_heap_report_enabled())
        return;

    /* Deliberately not reset when the field stops being settled: the settle test
     * flaps around a transition, and a report per flap buries the one line per
     * map that says anything. */
    if (!settled) {
        return;
    }

    if (reported_map != mapId) {
        reported_map = mapId;
        since = 0;
        openmmo_heap_report("map settled");
        if (fs != NULL) {
            pool_report(fs);
        }
        fprintf(stderr, "openmmo: avatars, %ld remote deletes, %ld freed,"
                " %d cached\n", g_avatar_deletes, g_avatar_frees, cached_avatars());
    } else if (++since == 300) {
        openmmo_heap_report("300 frames on");
    }
}

/* Map an engine facing DIR_* to the D-pad key the engine's movement-action lookup
 * expects, so a step reuses PlayerAvatar_GetMovementActionAnimCode exactly as
 * CommPlayer_MoveClient does. */
/*
 * The movement action a peer crosses a tile with, given the pace the model timed the step at.
 */
static int peer_action_speed(int speed, u16 *pad)
{
    switch (speed) {
    case OPENMMO_ENTITY_SPEED_RUN:
        *pad |= PAD_BUTTON_B;
        return PLAYER_ACTION_SPEED_NORMAL;
    case OPENMMO_ENTITY_SPEED_WALK:
        return PLAYER_ACTION_SPEED_SLOWER;
    case OPENMMO_ENTITY_SPEED_SLOW:
        return PLAYER_ACTION_SPEED_NOT_MOVING;
    default:
        return PLAYER_ACTION_SPEED_FAST;
    }
}

static u16 pad_from_dir(int dir)
{
    switch (dir) {
    case DIR_NORTH: return PAD_KEY_UP;
    case DIR_SOUTH: return PAD_KEY_DOWN;
    case DIR_WEST:  return PAD_KEY_LEFT;
    case DIR_EAST:  return PAD_KEY_RIGHT;
    default:        return 0;
    }
}

/* How many slots of the map's object table nobody is using. */
static int map_objects_free(FieldSystem *fs)
{
    MapObject *obj = NULL;
    int idx = 0;
    int used = 0;

    while (MapObjectMan_FindObjectWithStatus(fs->mapObjMan, &obj, &idx,
                                             MAP_OBJ_STATUS_0)) {
        used++;
    }

    return MapObjectMan_GetMaxObjects(fs->mapObjMan) - used;
}

/*
 * Slots kept back from remote players for the map's own use: a script that spawns an NPC, a
 * follower, a field effect. They are added without asking, and a crowd that has taken the last
 * slot turns one of those into the NULL write above.
 */

/* ------------------------------------------------------------------ *
 *  A peer's follower
 * ------------------------------------------------------------------ */

/* The fourth local-id band. 0x100 is the remote avatars, 0x200 the npcs, 0x300
 * our own follower; a peer's is one per slot above those. */
#define OPENMMO_PEER_FOLLOW_LOCALID_BASE 0x400

/* How many of them may draw at once, and it is the texture pool that decides. */
#define OPENMMO_PEER_FOLLOWERS_DRAWN 8

/*
 * And the object table. A follower is refused before a player is: a crowd of twelve where the
 * last four walk alone is a working game, and one where the thirteenth object writes through a
 * NULL is not.
 */
#define OPENMMO_FOLLOWER_OBJECT_RESERVE \
    (OPENMMO_MAP_OBJECT_RESERVE + OPENMMO_ENTITY_NETID_CEIL)

static int g_peer_follow_gfx[OPENMMO_ENTITY_NETID_CEIL];   /* 0 = none seated */

static MapObject *peer_follower(FieldSystem *fs, int slot)
{
    return MapObjMan_LocalMapObjByIndex(fs->mapObjMan,
                                        OPENMMO_PEER_FOLLOW_LOCALID_BASE + slot);
}

static int peer_followers_drawn(FieldSystem *fs)
{
    int i, n = 0;

    for (i = 0; i < OPENMMO_ENTITY_NETID_CEIL; i++) {
        if (peer_follower(fs, i) != NULL)
            n++;
    }
    return n;
}

static void peer_follower_drop(FieldSystem *fs, int slot)
{
    MapObject *obj = peer_follower(fs, slot);

    if (obj != NULL)
        MapObject_Delete(obj);
    g_peer_follow_gfx[slot] = 0;
}

/* Seat, or re-seat, the Pokemon behind one peer, on the tile the peer is standing on. */
static void peer_follower_seat(FieldSystem *fs, const openmmo_event *ev,
                               enum MapHeaderID header)
{
    int slot = ev->entity.slot;
    MapObject *obj;

    peer_follower_drop(fs, slot);
    if (!ev->entity.has_follower)
        return;

    if (openmmo_dev_env("OPENMMO_FOLLOWER_NO_CULL") == NULL) {
        if (peer_followers_drawn(fs) >= OPENMMO_PEER_FOLLOWERS_DRAWN) {
            static int said;

            if (!said) {
                said = 1;
                printf("openmmo: %d peer followers are drawn already, the"
                       " next players walk alone\n",
                       OPENMMO_PEER_FOLLOWERS_DRAWN);
                fflush(stdout);
            }
            return;
        }
        if (map_objects_free(fs) <= OPENMMO_FOLLOWER_OBJECT_RESERVE) {
            static int said;

            if (!said) {
                said = 1;
                printf("openmmo: the map's object table is down to %d free --"
                       " peer followers give way to players\n",
                       map_objects_free(fs));
                fflush(stdout);
            }
            return;
        }
    }

    /*
     * MOVEMENT_TYPE_NONE, and that is the load-bearing line of this whole half:
     * MOVEMENT_TYPE_FOLLOW_PLAYER follows the LOCAL avatar (MapObjectMan_GetPlayerMapObject),
     * so a peer's follower given that type walks behind the wrong trainer.
     */
    obj = MapObjectMan_AddMapObject(fs->mapObjMan, ev->entity.x, ev->entity.z,
                                    ev->entity.dir,
                                    (u32)ev->entity.follower_gfx,
                                    MOVEMENT_TYPE_NONE, header);
    if (obj == NULL)
        return;
    MapObject_SetLocalID(obj, (u32)(OPENMMO_PEER_FOLLOW_LOCALID_BASE + slot));
    g_peer_follow_gfx[slot] = ev->entity.follower_gfx;
    printf("openmmo: peer slot %d walks with gfx %d\n", slot,
           ev->entity.follower_gfx);
    fflush(stdout);
}

/*
 * One step of a peer's follower: onto the tile the peer was standing on before this step, at
 * the pace the peer took it.
 */
static void peer_follower_step(FieldSystem *fs, int slot, int was_x, int was_z,
                               int speed)
{
    MapObject *obj = peer_follower(fs, slot);
    int fx, fz, dx, dz, dir, action;

    if (obj == NULL)
        return;
    fx = MapObject_GetX(obj);
    fz = MapObject_GetZ(obj);
    dx = was_x - fx;
    dz = was_z - fz;
    if (dx == 0 && dz == 0)
        return;

    if ((dx != 0 && dz != 0) || dx < -1 || dx > 1 || dz < -1 || dz > 1) {
        MapObject_SetPosDirFromCoords(obj, was_x, 0, was_z,
                                      MapObject_GetFacingDir(obj));
        return;
    }

    if (dz < 0)
        dir = DIR_NORTH;
    else if (dz > 0)
        dir = DIR_SOUTH;
    else if (dx < 0)
        dir = DIR_WEST;
    else
        dir = DIR_EAST;

    /* The same three paces the crowd draws a player with, so a follower keeps
     * station with the trainer instead of arriving a beat late or early. */
    switch (speed) {
    case OPENMMO_ENTITY_SPEED_FASTEST:
    case OPENMMO_ENTITY_SPEED_RUN:
        action = MOVEMENT_ACTION_RUN_NORTH + dir;
        break;
    case OPENMMO_ENTITY_SPEED_SLOW:
        action = MOVEMENT_ACTION_WALK_SLOW_NORTH + dir;
        break;
    default:
        action = MOVEMENT_ACTION_WALK_NORMAL_NORTH + dir;
        break;
    }
    LocalMapObj_SetAnimationCode(obj, (enum MovementAction)action);
}

/* Every peer's follower forgotten, for a map change or a session that ended.
 * The objects go with the table; only the bookkeeping is ours to clear. */
static void peer_followers_forget(void)
{
    memset(g_peer_follow_gfx, 0, sizeof g_peer_follow_gfx);
}

/* Apply one render event to the avatar in its slot. fs must already be settled. */
static void apply_entity_event(FieldSystem *fs, const openmmo_event *ev)
{
    int slot = ev->entity.slot;
    if (slot < 0 || slot >= OPENMMO_ENTITY_NETID_CEIL)
        return;

    switch (ev->kind) {
    case OPENMMO_EV_ENTITY_SPAWN: {
        MapObject *stale;
        PlayerAvatar *av;

        /* Replace any avatar or leftover object already in this slot's band, 
         * the engine does the same reclaim before adding a remote player.
         * avatar_live() forgets a pointer left dangling by a table rebuild
         * without deleting through it, so only a genuinely live avatar is deleted. */
        if (avatar_live(fs, slot)) {
            PlayerAvatar *old = g_avatars[slot];

            /* Uncache before deleting: PlayerAvatar_Delete goes through
             * MapObject_Delete, whose hook frees a still-cached avatar's struct,
             * and PlayerAvatar_Free would then free it a second time. */
            g_avatars[slot] = NULL;
            PlayerAvatar_Delete(old);
        }
        stale = MapObjMan_LocalMapObjByIndex(fs->mapObjMan, ev->entity.localid);
        if (stale != NULL)
            MapObject_Delete(stale);

        /*
         * The cull. Refusing to draw one player is a missing sprite; letting the engine run
         * out of objects is a write through NULL.
         */
        if (openmmo_dev_env("OPENMMO_CROWD_NO_CULL") == NULL
            && map_objects_free(fs) <= OPENMMO_MAP_OBJECT_RESERVE) {
            static int said;
            if (!said) {
                said = 1;
                printf("openmmo: the map's object table is full (%d slots) --"
                       " remote players past this one are not drawn\n",
                       MapObjectMan_GetMaxObjects(fs->mapObjMan));
                fflush(stdout);
            }
            break;
        }

        av = PlayerAvatar_New(fs->mapObjMan, ev->entity.x, ev->entity.z,
                              ev->entity.dir, 0x0, ev->entity.gender,
                              ev->entity.version, NULL);
        if (av == NULL)
            break;
        MapObject_SetLocalID(PlayerAvatar_GetMapObject(av), (u32)ev->entity.localid);
        g_avatars[slot] = av;
        openmmo_label_set(slot, ev->entity.name);
        printf("openmmo: named slot %d \"%s\"\n", slot,
               ev->entity.name[0] != '\0' ? ev->entity.name : "?");
        fflush(stdout);
        peer_follower_seat(fs, ev, fs->location != NULL
                                   ? fs->location->mapHeaderID
                                   : (enum MapHeaderID)0);
        {
            MapObject *obj = PlayerAvatar_GetMapObject(av);
            u32 before = MapObject_GetGraphicsID(obj);
            /*
             * Always seated, body or not: PlayerAvatar_New asked the patched
             * Player_GetSpriteFromStateAndGender, which answers the LOCAL look for the local
             * gender, so a peer of our gender with no body of their own was built wearing our
             * look.
             */
            int want = ev->entity.has_body
                           ? openmmo_look_body_gfx(ev->entity.gfx, ev->entity.gender)
                           : mmo_appearance_gender_gfx(ev->entity.gender);
            const char *fg = openmmo_dev_env("OPENMMO_FAKE_GFX");

            /* OPENMMO_FAKE_GFX=N applies one id to the whole crowd; a
             * comma-list cycles. A body on the event wins. */
            if (!ev->entity.has_body && fg != NULL && fg[0] != '\0') {
                int n = 0, pick = slot;
                const char *p = fg;

                while (*p) {
                    char *end;
                    long v = strtol(p, &end, 0);

                    if (end == p)
                        break;
                    if (n == pick) {
                        want = (int)v;
                        break;
                    }
                    n++;
                    p = (*end == ',') ? end + 1 : end;
                    if (*end != ',' && n > pick)
                        want = (int)v;
                }
                if (want < 0 && n > 0) {
                    /* wrap the list */
                    int i = 0;

                    p = fg;
                    pick %= n;
                    while (*p) {
                        char *end;
                        long v = strtol(p, &end, 0);

                        if (end == p)
                            break;
                        if (i == pick) {
                            want = (int)v;
                            break;
                        }
                        i++;
                        p = (*end == ',') ? end + 1 : end;
                    }
                }
            }
            if (want >= 0 && (u32)want != before) {
                sub_02061AD4(obj, want);
                printf("openmmo: appearance slot %d gfx %u -> %u (live=%d)\n",
                       slot, before, MapObject_GetGraphicsID(obj),
                       avatar_live(fs, slot));
                fflush(stdout);
            } else {
                printf("openmmo: appearance slot %d gfx %u (gender %d version %d)\n",
                       slot, before, ev->entity.gender, ev->entity.version);
                fflush(stdout);
            }
        }
        break;
    }
    case OPENMMO_EV_ENTITY_STEP: {
        PlayerAvatar *av;
        u16 pad;
        u32 anim;
        int action;

        int was_x, was_z;

        if (!avatar_live(fs, slot))
            break;
        av = g_avatars[slot];
        /* Read before the step: the tile the peer is about to leave is the one
         * its follower steps onto. */
        was_x = PlayerAvatar_GetXPos(av);
        was_z = PlayerAvatar_GetZPos(av);
        pad = pad_from_dir(ev->entity.dir);
        action = peer_action_speed(ev->entity.speed, &pad);
        /* collision 0, the normal-step call from CommPlayer_MoveClient. The
         * action both animates and advances the MapObject one tile, keeping the
         * drawn avatar on the model's tile. Running is asked for so that the run
         * key the pace put in the pad is read. */
        anim = PlayerAvatar_GetMovementActionAnimCode(av, pad, pad, action, 1, 0);
        if (anim != 0xff)
            PlayerAvatar_SetMapObjMovement(av, (enum MovementAction)anim, 1);
        peer_follower_step(fs, slot, was_x, was_z, ev->entity.speed);
        break;
    }
    case OPENMMO_EV_ENTITY_PLACE: {
        openmmo_event seat = *ev;

        if (!avatar_live(fs, slot))
            break;
        /*
         * Seated afresh rather than written onto the live object: a position write passes y =
         * 0 and leaves the avatar standing at height zero wherever the ground is, and it
         * clears the step the object is in the middle of.
         */
        printf("openmmo: peer slot %d set down at (%d,%d)\n",
               slot, ev->entity.x, ev->entity.z);
        fflush(stdout);
        seat.kind = OPENMMO_EV_ENTITY_SPAWN;
        apply_entity_event(fs, &seat);
        break;
    }
    case OPENMMO_EV_ENTITY_TURN: {
        if (avatar_live(fs, slot))
            PlayerAvatar_TryFace(g_avatars[slot], ev->entity.dir);
        break;
    }
    case OPENMMO_EV_ENTITY_DESPAWN: {
        /* A leave that arrives after a table rebuild has nothing live to delete;
         * avatar_live() forgets the dangling pointer rather than freeing it. */
        if (avatar_live(fs, slot)) {
            PlayerAvatar *going = g_avatars[slot];

            g_avatars[slot] = NULL; /* uncached first; see the spawn path above */
            PlayerAvatar_Delete(going);
        }
        peer_follower_drop(fs, slot);
        openmmo_label_clear(slot);
        break;
    }
    default:
        break;
    }
}

/*
 * Seat every peer the model already holds. Spawn events that arrived while the field was down
 * were drained and dropped; the model kept them.
 */
/* A step the engine refused is a peer who never arrives. */
/* Tiles of gap a peer is walked across rather than set down at. Four is two
 * corners' worth: enough that a burst of steps or a corner is walked, short
 * enough that a warp does not become a sprint through the scenery. */
#define PEER_CATCHUP_SNAP 4

static void catch_up_peers(FieldSystem *fs)
{
    openmmo_event evs[OPENMMO_ENTITY_NETID_CEIL];
    int n, i;

    if (fs == NULL || g_client == NULL || !field_ready_for_peers(fs))
        return;
    /* Under a task or a child process the field does not tick its objects at
     * all, so a correction now would land on a frozen avatar. */
    if (fs->task != NULL || FieldSystem_HasChildProcess(fs))
        return;

    n = openmmo_client_live_entities(g_client, evs, OPENMMO_ENTITY_NETID_CEIL);
    for (i = 0; i < n; i++) {
        int slot = evs[i].entity.slot;
        PlayerAvatar *av;
        MapObject *obj;
        int ax, az, dx, dz, dir, behind, action;
        u16 pad;
        u32 anim;

        if (slot < 0 || slot >= OPENMMO_ENTITY_NETID_CEIL)
            continue;
        if (!avatar_live(fs, slot))
            continue;
        av = g_avatars[slot];
        obj = PlayerAvatar_GetMapObject(av);
        if (obj == NULL || MapObject_IsMoving(obj))
            continue;

        ax = PlayerAvatar_GetXPos(av);
        az = PlayerAvatar_GetZPos(av);
        dx = evs[i].entity.x - ax;
        dz = evs[i].entity.z - az;

        if (dx == 0 && dz == 0) {
            if (PlayerAvatar_GetFacingDir(av) != evs[i].entity.dir)
                PlayerAvatar_TryFace(av, evs[i].entity.dir);
            continue;
        }

        /*
         * Walking is the answer for anything a walk can close, and rounding a corner is the
         * ordinary case of being two behind, one on each axis.
         */
        if (dx * dx + dz * dz > PEER_CATCHUP_SNAP * PEER_CATCHUP_SNAP) {
            openmmo_event seat = evs[i];

            /* Seated afresh rather than moved. */
            printf("openmmo: peer slot %d re-seated at (%d,%d) from (%d,%d)\n",
                   slot, evs[i].entity.x, evs[i].entity.z, ax, az);
            fflush(stdout);
            seat.kind = OPENMMO_EV_ENTITY_SPAWN;
            apply_entity_event(fs, &seat);
            continue;
        }

        /* More than one tile behind the model is the same catching-up the model
         * itself runs to close, so the avatar closes it at the same pace rather
         * than strolling further behind every frame. */
        behind = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);

        /* One axis at a time, the long one first, so a diagonal catch-up walks
         * the way a person would rather than zig-zagging. */
        if (dx != 0 && (dz == 0 || (dx < 0 ? -dx : dx) >= (dz < 0 ? -dz : dz)))
            dz = 0;
        else
            dx = 0;

        dir = dx > 0 ? OPENMMO_DIR_EAST
            : dx < 0 ? OPENMMO_DIR_WEST
            : dz > 0 ? OPENMMO_DIR_SOUTH
                     : OPENMMO_DIR_NORTH;
        pad = pad_from_dir(dir);
        action = peer_action_speed(behind > 1 ? OPENMMO_ENTITY_SPEED_RUN
                                              : evs[i].entity.speed,
                                   &pad);
        anim = PlayerAvatar_GetMovementActionAnimCode(av, pad, pad, action, 1, 0);
        if (anim != 0xff)
            PlayerAvatar_SetMapObjMovement(av, (enum MovementAction)anim, 1);
    }
}

static void apply_live_entities(FieldSystem *fs)
{
    openmmo_event evs[OPENMMO_ENTITY_NETID_CEIL];
    int n, i, seated = 0;

    if (g_client == NULL || fs == NULL)
        return;
    n = openmmo_client_live_entities(g_client, evs, OPENMMO_ENTITY_NETID_CEIL);
    for (i = 0; i < n; i++) {
        if (avatar_live(fs, evs[i].entity.slot))
            continue;
        apply_entity_event(fs, &evs[i]);
        seated++;
    }
    if (n > 0 || seated > 0) {
        printf("openmmo: field up with %d remote player(s), seated %d\n",
               n, seated);
        fflush(stdout);
    }
}

/*
 * A synthetic remote entity, so the render path can be seen without a second live player
 * (whose real mutual-sighting is Phase 3.7's two-window oracle). Gated on OPENMMO_FAKE_ENTITY;
 * default off, so a plain boot is untouched.
 */
static void drive_fake_entity(FieldSystem *fs)
{
    static int active;
    static int phase;      /* 0 = need spawn, then 1.. = walking */
    static int step_timer;
    /* A four-side square: north, east, south, west, each a few tiles. */
    static const int kDirs[4] = { DIR_NORTH, DIR_EAST, DIR_SOUTH, DIR_WEST };

    /* The demo runs during the bedroom intro, which holds a script FieldTask, so
     * it uses a looser "map is up" gate than the socket path's full settled test, 
     * enough to prove the avatar draws and steps. */
    if (fs == NULL || !FieldSystem_IsRunningFieldMap(fs) || fs->playerAvatar == NULL)
        return;

    openmmo_event ev;
    memset(&ev, 0, sizeof ev);
    ev.entity.slot = 0;
    ev.entity.localid = OPENMMO_ENTITY_LOCALID_BASE + 0;

    if (!active) {
        int px = PlayerAvatar_GetXPos(fs->playerAvatar);
        int pz = PlayerAvatar_GetZPos(fs->playerAvatar);
        active = 1;
        ev.kind = OPENMMO_EV_ENTITY_SPAWN;
        ev.entity.x = px + 2;    /* two tiles east of the player: open, in view */
        ev.entity.z = pz;
        ev.entity.dir = DIR_WEST; /* facing the player */
        snprintf(ev.entity.name, sizeof ev.entity.name, "%s",
                 g_fake_name != NULL ? g_fake_name : "Player");
        apply_entity_event(fs, &ev);
        printf("openmmo: fake entity spawned at (%d,%d), player at (%d,%d)\n",
               ev.entity.x, ev.entity.z, px, pz);
        return;
    }

    /* One walk step every 12 frames, cycling the square. After two full loops,
     * despawn once to exercise the delete path, then respawn, so the demo covers
     * SPAWN, STEP and DESPAWN, not just the two that draw. */
    if (step_timer > 0) {
        step_timer--;
        return;
    }
    step_timer = 12;

    if (phase == 24) {
        ev.kind = OPENMMO_EV_ENTITY_DESPAWN;
        apply_entity_event(fs, &ev);
        printf("openmmo: fake entity despawned\n");
        active = 0; /* respawn next frame */
        phase = 0;
        return;
    }

    ev.kind = OPENMMO_EV_ENTITY_STEP;
    /* A synthetic peer walks: zeroed memory is the engine's fastest
     * pace, not its ordinary one. */
    ev.entity.speed = OPENMMO_ENTITY_SPEED_WALK;
    ev.entity.dir = kDirs[(phase / 3) % 4];
    apply_entity_event(fs, &ev);
    phase++;
}

/*
 * A crowd of synthetic remote players, for measuring what the overworld's pools will actually
 * hold (6.7.2).
 */
static void drive_fake_crowd(FieldSystem *fs)
{
    static int spawned;
    static int step_timer;
    static int phase;
    static const int kDirs[4] = { DIR_NORTH, DIR_EAST, DIR_SOUTH, DIR_WEST };
    int i;

    if (fs == NULL || !FieldSystem_IsRunningFieldMap(fs) || fs->playerAvatar == NULL)
        return;

    /* A map change rebuilds the object table and takes the crowd with it, so the
     * demo re-arms when its first body is no longer registered: one crowd per
     * map, which is what walking a list of maps needs. */
    if (spawned && !avatar_live(fs, 0))
        spawned = 0;

    if (!spawned) {
        int px = PlayerAvatar_GetXPos(fs->playerAvatar);
        int pz = PlayerAvatar_GetZPos(fs->playerAvatar);
        int live = 0;

        spawned = 1;

        for (i = 0; i < g_fake_crowd && i < OPENMMO_ENTITY_NETID_CEIL; i++) {
            openmmo_event ev;

            memset(&ev, 0, sizeof ev);
            ev.kind = OPENMMO_EV_ENTITY_SPAWN;
            ev.entity.slot = i;
            ev.entity.localid = OPENMMO_ENTITY_LOCALID_BASE + i;
            ev.entity.x = px;
            ev.entity.z = pz;
            ev.entity.dir = kDirs[i & 3];
            ev.entity.gender = (i >> 0) & 1;
            ev.entity.version = (i >> 1) & 1;
            /* A name each, so the label path has something to draw without a
             * server. OPENMMO_FAKE_NAME supplies one (the whole crowd shares it,
             * numbered), which is how a name wider than the engine's own field
             * or outside ASCII gets driven onto a nameplate headlessly. */
            snprintf(ev.entity.name, sizeof ev.entity.name, "%s%d",
                     g_fake_name != NULL ? g_fake_name : "Player", i);
            if (g_fake_followers) {
                ev.entity.has_follower = 1;
                ev.entity.follower_gfx = mmo_follower_gfx_base() + i;
            }
            apply_entity_event(fs, &ev);
            if (avatar_live(fs, i))
                live++;
        }

        printf("openmmo: crowd of %d requested at (%d,%d), %d live\n",
               g_fake_crowd, px, pz, live);
        fflush(stdout);
        return;
    }

    if (step_timer > 0) {
        step_timer--;
        return;
    }
    step_timer = 12;

    for (i = 0; i < g_fake_crowd && i < OPENMMO_ENTITY_NETID_CEIL; i++) {
        openmmo_event ev;

        memset(&ev, 0, sizeof ev);
        ev.kind = OPENMMO_EV_ENTITY_STEP;
        /* A synthetic peer walks: zeroed memory is the engine's fastest
         * pace, not its ordinary one. */
        ev.entity.speed = OPENMMO_ENTITY_SPEED_WALK;
        ev.entity.slot = i;
        ev.entity.localid = OPENMMO_ENTITY_LOCALID_BASE + i;
        ev.entity.dir = (phase & 1) ? DIR_SOUTH : DIR_NORTH;
        apply_entity_event(fs, &ev);
    }
    phase++;
}

/* The GAME, stopped where it stands, at the platform's word. */
static volatile int g_host_paused;

void openmmo_mod_pause(int on)
{
    g_host_paused = on ? 1 : 0;
}

/*
 * Called once per frame from the engine's main loop, beside CommSys_Update.
 * openmmo_client_pump does exactly one poll(timeout=0) and a bounded frame decode: a dead or
 * silent server costs this frame a single non-blocking system call, never a stall.
 */
void openmmo_mod_frame(void)
{
    static int primed;
    static int rearmed;
    static int was_settled;

    /* Before anything this frame would do, including the crash handler and
     * the session pump: a parked frame is one that has not started. */
    while (g_host_paused)
        mmo_plat_sleep_us(20 * 1000);

    /*
     * The engine takes SIGSEGV, SIGBUS, SIGILL and SIGFPE for itself in main(), after the
     * constructor above armed us, so by the first frame the crash report is no longer
     * installed. Take them back here, once.
     */
    if (!rearmed) {
        rearmed = 1;
        mmo_plat_crash_install("client");
    }

    openmmo_sprite_bind_overlay();
    openmmo_sprite_dump_once();
    appearance_dump_once();

    /* Before the session gate below: offline there is no client at all and the
     * report of what the player has is exactly what an offline run owes. Online
     * it answers once, on a save the server took: the window is drawing the
     * character that save replaced, and the join is what reads the new one. */
    openmmo_playtime_tick();
    if (openmmo_import_tick(g_client))
        leave_session("your save is this character now; back to the launcher,"
                      " and Play carries on as it", LEAVE_SUPERSEDED);

    if (g_client == NULL)
        return;

    if (!primed) {
        primed = 1;
        openmmo_check_state_arena();
        maybe_start_from_env();
        openmmo_hud_open();

        {
            const char *h = getenv("OPENMMO_HUD");

            g_hud_on = !(h != NULL && h[0] == '0' && h[1] == '\0');
        }

        {
            const char *o = openmmo_dev_env("OPENMMO_OSK");
            g_osk_on = (o != NULL && o[0] != '\0' && o[0] != '0');
            if (g_osk_on) {
                if (o[0] == 'n' || o[0] == 'N')
                    g_osk_surface = MMO_ENTRY_NAME;
                else
                    g_osk_surface = MMO_ENTRY_SEARCH;
                /* Chat is not on this list any more: it is the engine's own
                 * keyboard, and mmo_entry_open refuses a surface that is. The
                 * demo is the widget, so it demonstrates the two surfaces the
                 * widget still owns. */
                mmo_entry_open(&g_osk, g_osk_surface);
                printf("openmmo: on-screen keyboard demo enabled (OPENMMO_OSK)\n");
            }
        }

        {
            const char *f = openmmo_dev_env("OPENMMO_FAKE_ENTITY");
            g_fake_entity = (f != NULL && f[0] != '\0' && f[0] != '0');
            if (g_fake_entity)
                printf("openmmo: synthetic remote entity enabled (OPENMMO_FAKE_ENTITY)\n");
        }

        {
            const char *n = openmmo_dev_env("OPENMMO_FAKE_NAME");

            g_fake_name = n; /* NULL = unset (default "Player"); "" = empty */
        }

        {
            const char *c = openmmo_dev_env("OPENMMO_FAKE_CROWD");

            g_fake_crowd = (c != NULL && c[0] != '\0') ? atoi(c) : 0;
            if (g_fake_crowd > OPENMMO_ENTITY_NETID_CEIL) {
                printf("openmmo: crowd of %d asked for, %d is this client's slot"
                       " ceiling (entity.h)\n",
                       g_fake_crowd, OPENMMO_ENTITY_NETID_CEIL);
                g_fake_crowd = OPENMMO_ENTITY_NETID_CEIL;
            }
            if (g_fake_crowd > 0) {
                g_fake_entity = 0; /* the crowd owns slot 0 */
                printf("openmmo: synthetic crowd of %d enabled (OPENMMO_FAKE_CROWD)\n",
                       g_fake_crowd);
            }
        }

        {
            const char *f = openmmo_dev_env("OPENMMO_FAKE_FOLLOWERS");

            g_fake_followers = (f != NULL && f[0] != '\0' && f[0] != '0');
            if (g_fake_followers) {
                printf("openmmo: the synthetic crowd walks with a Pokemon each"
                       " (OPENMMO_FAKE_FOLLOWERS)\n");
            }
        }

        {
            const char *t = openmmo_dev_env("OPENMMO_TESTBATTLE");

            g_testbattle = (t != NULL && t[0] != '\0' && t[0] != '0');
            if (g_testbattle)
                printf("openmmo: will ask the server for a wild battle after join\n");
        }

        {
            const char *d = openmmo_dev_env("OPENMMO_DUEL");

            if (d != NULL && d[0] != '\0') {
                g_duel_target = d;
                printf("openmmo: will challenge %s after join\n", d);
            }
        }

        {
            const char *t = openmmo_dev_env("OPENMMO_TRADE_WITH");

            if (t != NULL && t[0] != '\0') {
                g_trade_target = t;
                printf("openmmo: will offer %s a trade after join\n", t);
            }
        }

        {
            const char *s = openmmo_dev_env("OPENMMO_SHOP");

            g_ask_shop = (s != NULL && s[0] != '\0' && s[0] != '0');
            if (g_ask_shop) {
                g_ask_shop_delay = 30;
                printf("openmmo: will ask the server for a mart after join\n");
            }
        }

        {
            const char *d = openmmo_dev_env("OPENMMO_DIALOG");

            g_ask_dialog = (d != NULL && d[0] != '\0' && d[0] != '0');
            if (g_ask_dialog) {
                g_ask_dialog_delay = 30;
                printf("openmmo: will ask the server for a dialog box after join\n");
            }
        }

        {
            const char *yn = openmmo_dev_env("OPENMMO_YESNO");

            g_ask_yesno = (yn != NULL && yn[0] != '\0' && yn[0] != '0');
            if (g_ask_yesno) {
                g_ask_yesno_delay = 30;
                printf("openmmo: will ask the server for a yes/no after join\n");
            }
        }

        {
            const char *mn = openmmo_dev_env("OPENMMO_MENU");

            g_ask_menu = (mn != NULL && mn[0] != '\0' && mn[0] != '0');
            if (g_ask_menu) {
                g_ask_menu_delay = 30;
                printf("openmmo: will ask the server for a species menu after join\n");
            }
        }

        {
            const char *m = openmmo_dev_env("OPENMMO_MOVE");

            g_ask_move = (m != NULL && m[0] != '\0' && m[0] != '0');
            if (g_ask_move) {
                g_ask_move_delay = 30;
                printf("openmmo: will ask the server for a scripted move after join\n");
            }
        }

        {
            const char *f = openmmo_dev_env("OPENMMO_FAKE_CHARS");

            if (f != NULL && f[0] != '\0' && f[0] != '0') {
                mmo_creator_reset(&g_creator);
                mmo_creator_set_list(&g_creator, NULL);
                g_leave_lobby = 1;
                printf("openmmo: character select: 0 character(s)\n");
            }
        }
    }

    if (!g_in_lobby)
        creator_sync_osk();
    chat_sync_entry();
    if (g_osk_on || g_creator_osk || g_chat_entry)
        drive_osk();
    if (!g_in_lobby)
        drive_creator();
    drive_chat();
    drive_debug();

    /* Wrap the engine's renderer once it has installed itself, so the HUD is
     * painted into the frame every publish. Idempotent: our own name is skipped,
     * and a re-install by the engine is simply re-wrapped. */
    {
        const char *rn = pc_video_renderer_name();
        if (rn != NULL && strcmp(rn, OPENMMO_RENDERER_NAME) != 0)
            pc_video_set_renderer(openmmo_render, OPENMMO_RENDERER_NAME);
    }

    /* Before pumping, notice whether the local player has crossed to a new map. */
    FieldSystem *fs = pc_lab_field_system();
    if (fs != NULL) {
        int mid = fs->location != NULL ? (int)fs->location->mapHeaderID : -1;
        /*
         * Reset only on a real map-header change. A new FieldSystem pointer for the same
         * header is title-exit and GameStart rebuilding the field, the join-burst peers are
         * still the ones on this map, and the server will not send them again.
         */
        if (g_field_epoch.valid && g_field_epoch.mapId >= 0 && mid >= 0
            && g_field_epoch.mapId != mid
            /*
             * ...unless this change is the warp arrival we are seating for. The client already
             * dropped the old map's model on MapTransition, and the arrival burst has since
             * refilled it with the destination's players.
             */
            && !(g_have_seat && mid == (int)g_seat.mapHeaderID)) {
            openmmo_client_reset_entities(g_client);
            /* ...and this is also where a warp the *client* took shows up. */
            g_pending_local_warp = mid;
            /* Point the seat at the map we are walking onto now, not once the
             * report goes out: a self-correction racing this transition
             * re-arms the seat, and with the old header still in it that
             * loaded the old map under the new tile. */
            if (g_have_seat)
                g_seat.mapHeaderID = (enum MapHeaderID)mid;
        }
        g_field_epoch.fs = fs;
        g_field_epoch.mapId = mid;
        g_field_epoch.valid = 1;
    }

    openmmo_client_pump(g_client);
    openmmo_script_state_tick(fs, g_client);
    /* The Underground's connection icon, off this client's own round-trip
     * clock. Driven every frame and not only while down there, because the
     * frame the climb starts is the frame it has to go. */
    openmmo_underground_tick(openmmo_client_latency_ms(g_client));
    openmmo_fishing_tick(fs, g_client);
    openmmo_apricorn_tick(fs);
    openmmo_fieldmove_tick(fs);

    /* The image the player asked to carry offline, once the field will stand
     * for it. The session ends on a written one: the launcher adopts what
     * landed there as the offline save and PLAY OFFLINE plays it. */
    if (openmmo_offline_export_tick(g_client))
        leave_session("leaving the server; this game plays offline from here",
                      LEAVE_REPORT);

    int settled = field_settled(fs);
    int peers_ok = field_ready_for_peers(fs);

    if (g_pending_local_warp >= 0 && settled && field_has_avatar(fs)) {
        if (fs->location != NULL
            && (int)fs->location->mapHeaderID == g_pending_local_warp) {
            report_local_warp(fs, g_pending_local_warp);
            /* The position only. g_scene_fought is the scene's other errand
             * and this arrival carries none of it, see report_scene_position. */
            g_scene_ran = 0;
        }
        /* A settle on any other header means a server warp overtook this
         * one, and that arrival is the server's own doing, drop it. */
        g_pending_local_warp = -1;
    }
    if (settled)
        report_scene_position(fs);

    /* Peers who arrived while the title (or a map change) was up are still
     * in the model. Seat them the first frame the object table is safe and
     * the local player is on the map the server put it on. */
    if (peers_ok && !was_settled)
        apply_live_entities(fs);
    was_settled = peers_ok;

    heap_report_tick(fs, settled, g_field_epoch.valid ? g_field_epoch.mapId : -1);

    openmmo_event ev;
    while (openmmo_client_poll_event(g_client, &ev)) {
        switch (ev.kind) {
        case OPENMMO_EV_STATUS:
            printf("openmmo: status %s\n", openmmo_status_name(ev.status));
            break;
        case OPENMMO_EV_JOINED: {
            const openmmo_world_state *ws = openmmo_client_world_state(g_client);
            int sx = -1, sz = -1;

            if (ws != NULL && ws->valid)
                printf("openmmo: joined the game, region %d bank %d map %d at (%d,%d), money %d\n",
                       ws->region, ws->bank_id, ws->map_id, ws->x, ws->y, ws->money);
            else
                printf("openmmo: joined the game\n");

            /* The server named a map and a tile. Sit there: leave the
             * lobby (or the title, on a named-character skip) for that
             * map, or (already in the field) load it. */
            if (ws != NULL && ws->valid) {
                if (openmmo_client_self_tile(g_client, &sx, &sz) != 0) {
                    sx = ws->x;
                    sz = ws->y;
                }
                if (seat_from_server(ws->region, ws->bank_id, ws->map_id,
                                     sx, sz, FACE_DOWN)
                    && pc_lab_field_system() == NULL) {
                    if (g_in_lobby)
                        g_leave_field = 1;
                    else
                        g_leave_title = 1;
                }
            }
            if (g_testbattle)
                g_testbattle_delay = TESTBATTLE_ASK_DELAY;
            if (g_duel_target != NULL)
                g_duel_delay = DUEL_ASK_DELAY;
            if (g_trade_target != NULL)
                g_trade_delay = DUEL_ASK_DELAY;
            mmo_creator_reset(&g_creator);
            mmo_chatwin_show(&g_chat);
            printf("openmmo: chat window on the poketch\n");
            break;
        }
        case OPENMMO_EV_SCRIPT_STATE:
            /* A re-seat while already in the world. The tick applies it
             * off the store's own generation counter, so nothing has to
             * be done here; say so, because a seat landing mid-session
             * overwrites whatever the VM had written since the last one. */
            printf("openmmo: the server re-seated the local script state\n");
            break;
        case OPENMMO_EV_CHARACTERS:
            creator_take_list();
            break;
        case OPENMMO_EV_CHAT:
            printf("openmmo: chat [%d] %s: %s\n", ev.chat.type,
                   ev.chat.sender[0] ? ev.chat.sender : "-", ev.chat.text);
            /* A whisper from someone else becomes who the Whispers channel
             * replies to. The server echoes our own back with our id on it,
             * so match on the id, not the name. */
            if (ev.chat.type == MMO_CHAT_WHISPER && ev.chat.sender[0] != '\0') {
                const openmmo_world_state *ws =
                    openmmo_client_world_state(g_client);

                if (ws == NULL || ev.chat.sender_id != ws->character_id)
                    snprintf(g_last_whisper, sizeof g_last_whisper, "%s",
                             ev.chat.sender);
            }
            break;
        case OPENMMO_EV_FAILED:
            printf("openmmo: session failed: %s\n", ev.message);
            snprintf(g_message, sizeof g_message, "%s", ev.message);
            mmo_chatwin_hide(&g_chat);
            break;
        case OPENMMO_EV_DISCONNECTED:
            printf("openmmo: disconnected: %s\n", ev.message);
            snprintf(g_message, sizeof g_message, "%s", ev.message);
            mmo_chatwin_hide(&g_chat);
            break;
        case OPENMMO_EV_ENTITY_SPAWN:
        case OPENMMO_EV_ENTITY_STEP:
        case OPENMMO_EV_ENTITY_TURN:
        case OPENMMO_EV_ENTITY_DESPAWN:
            /*
             * Only touch the field while it is safe to; a render event that arrives mid-
             * transition is dropped rather than dereferencing a teardown-time object table.
             */
            /* Asked again per event, not taken from the frame's own snapshot:
             * a warp arrives in this same drain loop and clears the seat, and
             * the spawns that follow it belong to the map we have not loaded
             * yet. Reading a value sampled before the loop seated them anyway. */
            if (field_ready_for_peers(fs))
                apply_entity_event(fs, &ev);
            break;
        case OPENMMO_EV_SELF_CORRECT:
            /* The server snapped the local player back to its authoritative tile.
             * The client has already re-anchored the from-tile; put the engine
             * avatar on the same tile so the next predicted step is not a
             * desync. A snap that arrives mid-transition waits on the seat. */
            /*
             * Never below ground: the tile named is a surface one and the cavern has no land
             * at it, so obeying it drops the avatar onto terrain that is not there.
             */
            if (openmmo_underground_active()) {
                printf("openmmo: ignoring a correction to (%d,%d) while"
                       " below ground\n", ev.entity.x, ev.entity.z);
                break;
            }
            if (field_has_avatar(fs)) {
                snap_avatar(fs, ev.entity.x, ev.entity.z, ev.entity.dir);
                printf("openmmo: snapped to (%d,%d) dir %d\n",
                       ev.entity.x, ev.entity.z, ev.entity.dir);
            }
            if (g_have_seat) {
                g_seat.x = ev.entity.x;
                g_seat.z = ev.entity.z;
                g_seat.faceDirection = ev.entity.dir;
                g_seated = 0;
            }
            break;
        case OPENMMO_EV_WARP:
            /* The server warped the local player to a new map and placed it.
             * The client has already reset its remote-entity model for the
             * new map's interest set. Load that map and stand on the tile
             * the arrival named. */
            printf("openmmo: warped to region %d bank %d map %d at (%d,%d) dir %d\n",
                   ev.warp.region, ev.warp.bank, ev.warp.map,
                   ev.warp.x, ev.warp.z, ev.warp.dir);
            if (seat_from_server(ev.warp.region, ev.warp.bank, ev.warp.map,
                                 ev.warp.x, ev.warp.z, ev.warp.dir)) {
                g_seat_reload = 1;
                if (g_local_landing_frames > 0
                    && (int)g_seat.mapHeaderID == g_local_landing_header) {
                    /* The engine already took it and said so; this is the
                     * server's answer to the step, not a new place. */
                    g_seat_reload = 0;
                    printf("openmmo: the server's warp confirms the engine's"
                           " landing on header %d\n", g_local_landing_header);
                } else if (g_engine_warp_frames > 0
                           && (int)g_seat.mapHeaderID == g_engine_warp_header) {
                    /* The engine is taking us there as this arrives. Hand the
                     * seat to that transition (g_seat_engine): its landing
                     * settles the seat once the walk out of the door is over,
                     * and nothing is snapped or loaded twice. */
                    g_seat_reload = 0;
                    g_seat_engine = 1;
                    printf("openmmo: the server's warp is the transition the"
                           " engine is already running (header %d)\n",
                           g_engine_warp_header);
                }
            }
            break;
        case OPENMMO_EV_ENCOUNTER: {
            int foe = ev.encounter.foe_species;
            int lv = ev.encounter.foe_level;

            printf("openmmo: the server started a %s battle (background %d"
                   " foe %d lv %d)\n",
                   ev.encounter.wild ? "wild" : "trainer",
                   ev.encounter.background, foe, lv);
            g_battle_ran = 0;
            g_battle_hold_frames = 0;
            g_battle_wild = ev.encounter.wild;
            if (foe <= 0) {
                foe = 19;
                lv = 3;
            }
            /* A reel-in that landed asked for this one: the fishing task is
             * the field task and starts the fight itself. */
            if (openmmo_fishing_take_battle(fs, g_client, foe, lv)) {
                g_battle_ran = 1;
                break;
            }
            /* And so did one of this game's own scripted sites: the paused
             * script starts it on the task the scene is already running under
             * (mods/openmmo/src/openmmo_static.c). */
            {
                extern int openmmo_static_take_battle(int foe_species, int foe_level);
                int taken = openmmo_static_take_battle(foe, lv);

                if (taken) {
                    /* Landed after the scene gave up and fought its own: a
                     * scene still up answers for it at its end, and one
                     * already over is run from here, so the server's instance
                     * closes either way and no second fight starts. */
                    if (taken == 2)
                        openmmo_client_battle_run(g_client);
                    g_battle_ran = 1;
                    break;
                }
            }
            if (openmmo_encounter_start(fs, g_client, foe, lv) == 0) {
                g_battle_ran = 1;
                break;
            }
            /* A field still busy, the smash or the headbutt whose roll this
             * is has not finished playing, is not a refusal: the fight
             * starts the frame the field is free. */
            if (openmmo_encounter_field_busy(fs)) {
                openmmo_encounter_defer(foe, lv);
                g_battle_ran = 1;
            }
            break;
        }
        case OPENMMO_EV_BATTLE_EVENT:
            if (ev.battle.opcode == MMO_GAME_OP_BATTLE_QUEUED_EVENT)
                g_battle_ran = 0;
            if (ev.battle.opcode == MMO_GAME_OP_BATTLE_MOVE_EVENT) {
                const openmmo_battle_anims *an =
                    openmmo_client_battle_anims(g_client);
                int i;
                for (i = 0; an && i < an->n; i++) {
                    const mmo_battle_anim *a = &an->cmd[i];
                    printf("openmmo: battle anim %s",
                           mmo_battle_anim_name(a->command));
                    if (a->kind == MMO_ANIM_PRINT
                        || a->kind == MMO_ANIM_MOVE
                        || a->kind == MMO_ANIM_STATUS)
                        printf(" id %u", a->id);
                    if (a->kind == MMO_ANIM_HP)
                        printf(" hp %d", a->hp);
                    if (a->fallback && a->why)
                        printf(" (fallback: %s)", a->why);
                    printf("\n");
                }
            }
            break;
        case OPENMMO_EV_DUEL_INVITE:
            /* The official invite carries both kinds of request; type 1 is a
             * trade offer and takes the trade's own question. */
            if (ev.duel.request_type == 1) {
                printf("openmmo: %s wants to trade\n", ev.duel.name);
                openmmo_trade_offer(ev.duel.name);
                break;
            }
            printf("openmmo: %s wants to battle\n", ev.duel.name);
            snprintf(g_duel_name, sizeof g_duel_name, "%s", ev.duel.name);
            g_duel_offered = 1;
            g_duel_asked = 0;
            break;
        case OPENMMO_EV_TRADE:
            if (ev.trade.entry)
                printf("openmmo: trade offer shown: dex %u\n",
                       openmmo_client_trade(g_client)->peer_mon.dex_id);
            else
                printf("openmmo: trade state %d (%s)\n", ev.trade.state,
                       ev.trade.peer[0] ? ev.trade.peer : "-");
            break;
        case OPENMMO_EV_GTL:
            printf("openmmo: gtl answered op 0x%02x, %d row(s) of %d\n",
                   ev.gtl.opcode, ev.gtl.rows, (int)ev.gtl.total);
            break;
        case OPENMMO_EV_DUEL_OUTCOME:
            printf("openmmo: the challenge was %s\n",
                   ev.duel.outcome == MMO_DUEL_ACCEPTED ? "accepted"
                   : ev.duel.outcome == MMO_DUEL_DECLINED ? "declined"
                                                          : "left unanswered");
            break;
        case OPENMMO_EV_LINK_BATTLE:
            printf("openmmo: link battle %d vs %s, net id %d, %d mon(s)\n",
                   ev.link_battle.battle_id, ev.link_battle.peer_name,
                   ev.link_battle.net_id, ev.link_battle.count);
            /* An offer we were still holding is spent: this is the answer. */
            g_duel_offered = 0;
            g_duel_asked = 0;
            g_battle_ran = 0;
            g_battle_hold_frames = 0;
            /* A field that is not settled yet is not a refusal: the seat stays
             * live and openmmo_link_battle_poll opens the scene when it is. */
            openmmo_link_battle_open(fs, g_client);
            break;
        case OPENMMO_EV_LINK_BATTLE_END:
            openmmo_link_battle_closed(g_client);
            break;
        case OPENMMO_EV_BATTLE_END:
            printf("openmmo: back in the overworld, battle acknowledged\n");
            g_battle_ran = 0;
            g_battle_hold_frames = 0;
            g_battle_wild = 0;
            if (g_testbattle && g_testbattle_round < testbattle_rounds())
                g_testbattle_delay = TESTBATTLE_ASK_DELAY;
            break;
        case OPENMMO_EV_MONEY:
            if (fs != NULL && fs->saveData != NULL)
                openmmo_seat_trainer_money(SaveData_GetTrainerInfo(fs->saveData),
                                   ev.money.money);
            else
                printf("openmmo: money %d\n", ev.money.money);
            break;
        case OPENMMO_EV_BAG:
            openmmo_bag_mark_dirty();
            if (fs != NULL && fs->saveData != NULL
                && FieldSystem_IsRunningFieldMap(fs))
                openmmo_bag_sync(fs->saveData,
                                 FieldSystem_HasChildProcess(fs));
            break;
        case OPENMMO_EV_SHOP:
            printf("openmmo: the server %s a shelf of %d line(s)%s\n",
                   ev.shop.open ? "opened" : "closed", ev.shop.count,
                   settled ? "" : " (the field is busy; it waits)");
            openmmo_shop_mark_pending();
            if (settled)
                openmmo_shop_try_open(fs);
            break;
        case OPENMMO_EV_DIALOG:
            openmmo_dialog_mark_pending();
            if (settled)
                openmmo_dialog_try_open(fs);
            break;
        case OPENMMO_EV_SCRIPT_MOVE:
            openmmo_script_move_mark_pending();
            play_script_move(fs);
            break;
        case OPENMMO_EV_OBJECTIVE:
            printf("openmmo: objectives %d%s\n",
                   ev.objective.count,
                   ev.objective.replace ? " (replace)" : "");
            break;
        case OPENMMO_EV_FRIENDS:
            printf("openmmo: friends %d (%d online)%s\n",
                   ev.friends.count, ev.friends.online,
                   ev.friends.replace ? " (replace)" : "");
            break;
        case OPENMMO_EV_GUILD:
            printf("openmmo: guild %s members %d (%d online)\n",
                   ev.guild.in_guild
                       ? (ev.guild.name[0] ? ev.guild.name : "?")
                       : "none",
                   ev.guild.member_count, ev.guild.online);
            if (openmmo_client_guild(g_client)->in_guild
                && !openmmo_client_guild(g_client)->log_valid)
                (void)openmmo_client_guild_log(g_client, 0);
            break;
        case OPENMMO_EV_MAIL:
            /* News only. The mailbox is a screen the player opens; an
             * arriving letter raises the window's own badge (snap.mail
             * carries the unread count) and nothing else. */
            printf("openmmo: mail inbox %d sent %d\n",
                   ev.mail.inbox, ev.mail.outbox);
            break;
        case OPENMMO_EV_LINK:
            printf("openmmo: link %s members %d\n",
                   ev.link.present ? "in" : "none", ev.link.count);
            openmmo_union_mark_pending();
            if (settled)
                openmmo_union_try_open(fs);
            break;
        case OPENMMO_EV_UI:
            printf("openmmo: ui 0x%02x pages %d options %d rows %d%s%s\n",
                   ev.ui.opcode, ev.ui.pages, ev.ui.options, ev.ui.rows,
                   ev.ui.confirm ? " confirm" : "",
                   ev.ui.prompt ? " prompt" : "");
            openmmo_widget_mark_pending();
            if (settled)
                openmmo_widget_try_open(fs);
            break;
        case OPENMMO_EV_SYNC:
            printf("openmmo: sync 0x%02x digest %d transfer %d stream %d image %d\n",
                   ev.sync.opcode, ev.sync.digest, ev.sync.transfer_done,
                   ev.sync.stream_done, ev.sync.image_done);
            break;
        }
    }

    openmmo_encounter_poll(fs, g_client);
    /* The link battle's own wire and scene. Polled every frame rather than
     * only when the field is settled: a fight runs with the field torn down,
     * and the two engines are talking through this the whole time. */
    openmmo_link_battle_poll(fs, g_client);
    /* The reconcile runs before the syncs, so what a box screen just did is
     * reported before any seat could pull it back under the old mirror. */
    openmmo_pc_reconcile_tick(fs, g_client);
    openmmo_party_report_if_touched(fs, g_client);
    openmmo_party_field_sync(fs, g_client);
    openmmo_pc_field_sync(fs, g_client);
    drive_battle_hold();
    drive_testbattle();
    drive_shop_cmd();
    drive_duel_cmd();
    drive_trade_cmd();
    drive_dialog_cmd();
    drive_yesno_cmd();
    drive_menu_cmd();
    drive_move_cmd();

    /* After the events, so a failure's words are already in g_message when the
     * state that explains them is published. */
    status_page_publish();
    /* Beside the status page because this is the point in the frame where what
     * the session is doing has just been settled. It is off unless the front
     * door turned it on, and it never blocks. */
    openmmo_presence_tick(fs, g_client);
    openmmo_hud_open();
    apply_hud_cmds();
    /* A page the window asked for goes out once the session can carry it. */
    openmmo_gtl_tick();
    /* The trade scene's wire: relayed commands and barrier markers drain
     * whether or not the field is settled. */
    openmmo_trade_tick();
    /*
     * In a windowed session the engine's own start menu is a second copy of the window's UI, 
     * every screen it offered is on the HUD bar, so X on the settled field opens nothing.
     */
    if (openmmo_session_configured() && openmmo_hud_windowed()
        && !openmmo_underground_active()) {
        FieldSystem *fs = pc_lab_field_system();

        if (fs != NULL && FieldSystem_IsRunningFieldMap(fs)
            && !FieldSystem_HasChildProcess(fs)) {
            gSystem.pressedKeys &= ~PAD_BUTTON_X;
            gSystem.pressedKeysRepeatable &= ~PAD_BUTTON_X;
        }
    }
    {
        char typed[OPENMMO_HUD_TEXT];
        int composing = mmo_chatwin_composing(&g_chat);

        if (composing)
            osk_line_utf8(&g_osk, typed, sizeof typed);
        else
            typed[0] = '\0';
        openmmo_hud_publish_now(g_client, g_status_flags, g_message,
                                typed, composing,
                                composing ? g_whisper_name : "",
                                (uint32_t)g_chat_send_type);
    }

    /* --- a broken link is redialed, not obeyed -------------------------- */
    if (g_started && g_client != NULL
        && (g_status_flags & OPENMMO_STATUS_F_WAS_LIVE)) {
        uint32_t st = (uint32_t)openmmo_client_status(g_client);
        long now = mmo_plat_seconds();

        /* Remembered while the world is live, because the reason to need the
         * character's name only arrives once the state that carried it is
         * gone. */
        if (st == OPENMMO_ST_IN_GAME && !g_rejoin.active) {
            const openmmo_world_state *ws =
                openmmo_client_world_state(g_client);

            if (ws != NULL && ws->valid && ws->name[0] != '\0')
                snprintf(g_rejoin.character, sizeof g_rejoin.character,
                         "%s", ws->name);
        }

        if (g_rejoin.active && st == OPENMMO_ST_IN_GAME) {
            fprintf(stderr,
                    "openmmo: rejoined as %s after %d attempt(s), %ld s "
                    "offline\n",
                    g_rejoin.character[0] != '\0' ? g_rejoin.character : "?",
                    g_rejoin.tries, (long)(now - g_rejoin.lost_s));
            g_rejoin.active = 0;
            g_rejoin.tries = 0;
            g_rejoin.lost_s = 0;
            g_status_flags &= ~(uint32_t)OPENMMO_STATUS_F_REJOIN;
            /* The hush below held every player at zero; the reseat's map
             * reload restarts the BGM, this puts the levels back. */
            {
                extern void openmmo_mixer_apply(void); /* openmmo_mixer.c */

                openmmo_mixer_apply();
            }
        }

        /* Raw states, not openmmo_status_over: once F_REJOIN is up, over()
         * answers no on purpose (the launcher must not act), so the campaign
         * asks the question it actually has, did this attempt land in a
         * terminal state. */
        if ((g_rejoin.active
             || (st == OPENMMO_ST_FAILED || st == OPENMMO_ST_DISCONNECTED))
            && !g_rejoin.gave_up) {
            extern void openmmo_mixer_hush(void);

            if (!g_rejoin.active) {
                g_rejoin.active = 1;
                g_rejoin.lost_s = now;
                g_rejoin.next_s = now + REJOIN_FIRST_WAIT_S;
                g_rejoin.tries = 0;
                g_status_flags |= OPENMMO_STATUS_F_REJOIN;
                fprintf(stderr,
                        "openmmo: link lost (%s), rejoining as %s for up "
                        "to %d s\n",
                        g_message[0] != '\0' ? g_message : "no reason given",
                        g_rejoin.character[0] != '\0' ? g_rejoin.character
                                                       : "the first character",
                        (int)REJOIN_GIVE_UP_S);
            }

            /* Between attempts the status is over again (the last one failed
             * or the link is still down); an attempt in flight is connecting
             * through joining and is left to run against its own phase
             * deadlines, which fire in the fused build now (client.c). */
            if (st == OPENMMO_ST_FAILED || st == OPENMMO_ST_DISCONNECTED) {
                /* A refusal is not an outage. */
                if (openmmo_client_login_refusal(g_client)
                    == MMO_LOGIN_ALREADY_LOGGED_IN) {
                    g_rejoin.gave_up = 1;
                    g_rejoin.active = 0;
                    g_status_flags &= ~(uint32_t)OPENMMO_STATUS_F_REJOIN;
                    fprintf(stderr,
                            "openmmo: stopped rejoining after %d attempt(s): "
                            "the account is signed in elsewhere\n",
                            g_rejoin.tries);
                } else if (now - g_rejoin.lost_s > REJOIN_GIVE_UP_S) {
                    g_rejoin.gave_up = 1;
                    g_rejoin.active = 0;
                    g_status_flags &= ~(uint32_t)OPENMMO_STATUS_F_REJOIN;
                    fprintf(stderr,
                            "openmmo: gave up rejoining after %d attempt(s) "
                            "over %ld s (%s)\n",
                            g_rejoin.tries, (long)(now - g_rejoin.lost_s),
                            g_message[0] != '\0' ? g_message : "no reason");
                } else if (now >= g_rejoin.next_s) {
                    rejoin_attempt();
                    g_rejoin.next_s = now + REJOIN_RETRY_S;
                }
            }

            if (g_rejoin.active) {
                /* The field under the notice hears nothing and says nothing:
                 * a tap on the glass lands on the drawn pad, and the pad
                 * drives a world the server left. Same spot the HUD already
                 * eats X from, widened to everything. */
                gSystem.pressedKeys = 0;
                gSystem.pressedKeysRepeatable = 0;
                gSystem.heldKeys = 0;
                openmmo_mixer_hush();
                return;
            }
        }
    }

    /*
     * A session that was live and is now over must not keep drawing a world nobody is
     * connected to. Publish first (the launcher reads the page), then leave.
     */
    if (g_started && g_client != NULL
        && (g_status_flags & OPENMMO_STATUS_F_WAS_LIVE)
        && openmmo_status_over((uint32_t)openmmo_client_status(g_client),
                               g_status_flags)) {
        /* Not in the same frame, and on Android not at all: the window draws
         * an ended session from the page just published (view_status_banner),
         * and exiting here leaves it no frame to draw it in. The two
         * platforms then part ways below, for reasons each branch carries. */
        static int leaving;
        const char *why = g_message[0] != '\0'
                              ? g_message
                              : openmmo_status_name(
                                    openmmo_client_status(g_client));

        /* Parked or holding, the dead field still must not hear the glass. */
        gSystem.pressedKeys = 0;
        gSystem.pressedKeysRepeatable = 0;
        gSystem.heldKeys = 0;

#ifdef __ANDROID__
        /* No exit on Android, ever. */
        extern void openmmo_mixer_hush(void);

        if (!leaving) {
            leaving = 1;
            fprintf(stderr,
                    "openmmo: session over (%s), the notice stays up; the "
                    "player closes the app\n", why);
        }
        openmmo_mixer_hush();
        return;
#else
        /*
         * The desktop viewer and launcher are other processes, so exit() here cannot crash
         * them and the launcher outlives the game to say what happened. Held a few seconds
         * first so the window's own notice is readable before the page's writer goes away.
         */
        enum { HOLD_S = 4 };
        static long leave_at;
        long now = mmo_plat_seconds();

        if (!leaving) {
            leaving = 1;
            leave_at = now + HOLD_S;
            fprintf(stderr,
                    "openmmo: session over (%s), holding %d s so the window "
                    "can say so\n", why, (int)HOLD_S);
        }
        if (now < leave_at)
            return;
        fprintf(stderr,
                "openmmo: leaving so the window cannot keep drawing a game "
                "nobody is connected to (%s)\n", why);
        exit(1);
#endif
    }

    /* A JOINED or WARP that named a map applies once the field is idle.
     * The title path writes the seat into the save before the field
     * starts, so the first settle is already on that map; BOOT_WORLD
     * and a live warp load it from here. */
    try_apply_seat(fs);
    apply_local_mount(fs);
    apply_local_body(fs);
    apply_local_strength(fs);
    apply_local_partner(fs);
    apply_npcs(fs);
    /*
     * Beside the npcs, and on the weaker of the two settled tests on purpose. `g_seated` is
     * what a peer waits for, because a peer is placed at a tile the server named and a warp's
     * arrival burst is settled on the map we are still leaving.
     */
    openmmo_follow_tick(fs, field_settled(fs));
    {
        extern void openmmo_follow_move_tick(FieldSystem *fs);
        openmmo_follow_move_tick(fs);
    }

    if (fs != NULL && fs->saveData != NULL
        && FieldSystem_IsRunningFieldMap(fs)) {
        openmmo_bag_sync(fs->saveData, FieldSystem_HasChildProcess(fs));
        openmmo_bag_aim_cursor(fs);
        openmmo_poketch_report(fs);
    }
    /* Not under `settled`: this is also what notices a screen the window
     * asked for closing again, and while one is up the field is not. */
    openmmo_apps_pump(fs);
    openmmo_contest_link_pump();
    /* Beside the relay and outside the settled gate for the same reason: the
     * station holds the pen while the contest's own application is up, which is
     * exactly when the field is not settled. It reads `settled` itself, for the
     * one frame it needs one, the frame it asks for a group. */
    openmmo_contest_lab_frame(fs, settled);
    /*
     * Why the field is busy, which is the question behind every screen that never opened.
     * `settled` is four conditions and the log only ever showed the verdict; this shows which
     * one is holding, and only when it changes.
     */
    if (getenv("OPENMMO_SETTLE_TRACE") != NULL) {
        static int was = -1;
        int now = fs == NULL ? 0
            : 1 | (fs->task != NULL ? 2 : 0)
                | (FieldSystem_IsRunningFieldMap(fs) ? 4 : 0)
                | (FieldSystem_HasChildProcess(fs) ? 8 : 0);

        if (now != was) {
            was = now;
            printf("openmmo: settle fs=%d task=%d field=%d child=%d\n",
                   now & 1, (now >> 1) & 1, (now >> 2) & 1, (now >> 3) & 1);
        }
    }
    if (settled) {
        openmmo_shop_try_open(fs);
        openmmo_bag_try_open(fs);
        openmmo_dialog_try_open(fs);
        drive_duel_offer(fs);
        openmmo_trade_pump(fs);
        openmmo_mail_try_open(fs);
        openmmo_mail_pump(fs);
        openmmo_union_try_open(fs);
        openmmo_gtl_try_open(fs);
        openmmo_widget_try_open(fs);
        openmmo_debug_try_open(fs);
    }
    openmmo_travel_tick(fs, g_seated);
    openmmo_cries_debug_tick(settled);
    openmmo_poly_overflow_tick();
    play_script_move(fs);
    catch_up_peers(fs);

    if (g_fake_entity)
        drive_fake_entity(fs);
    if (g_fake_crowd > 0)
        drive_fake_crowd(fs);
}
