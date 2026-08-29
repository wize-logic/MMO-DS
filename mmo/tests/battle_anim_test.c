/* A 0x33 maps onto the commands a local fight emits. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "battle_anim.h"
#include "game.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            printf("  ok   %s\n", msg);                                         \
        } else {                                                                \
            printf("  FAIL %s\n", msg);                                         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

#ifndef MMO_REPO_ROOT
#define MMO_REPO_ROOT "."
#endif

#define GAME_FX  MMO_REPO_ROOT "/protocols.game/src/test/resources/fixtures/"

static size_t load(const char *path, u8 *out, size_t cap)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f)
        return 0;
    n = fread(out, 1, cap, f);
    fclose(f);
    return n;
}

static void test_map_known_move(void)
{
    u8 b[128];
    size_t n;
    mmo_battle_move_event mv;
    mmo_battle_anim a[MMO_BATTLE_ANIM_MAX];
    int got;
    u8 msg[MMO_BTL_ANIM_MOVE_SIZE];

    printf("a the official client move event maps onto the engine's own commands:\n");

    n = load(GAME_FX "game/s2c/33/hp_update_tackle_31914.bin", b, sizeof b);
    CHECK(n == 27 && mmo_game_read_battle_move_event(b, n, &mv) == 0,
          "the Tackle body still walks");
    got = mmo_battle_map_move(&mv, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 4, "Tackle produces print, move, flicker, hp");
    CHECK(got >= 1 && a[0].command == MMO_BTLCMD_PRINT_ATTACK_MESSAGE
              && a[0].kind == MMO_ANIM_PRINT && a[0].id == 33,
          "the first command is PRINT_ATTACK of Tackle");
    CHECK(got >= 2 && a[1].command == MMO_BTLCMD_SET_MOVE_ANIMATION
              && a[1].kind == MMO_ANIM_MOVE && a[1].id == 33
              && !a[1].fallback,
          "then SET_MOVE_ANIMATION of Tackle");
    CHECK(got >= 3 && a[2].command == MMO_BTLCMD_FLICKER_BATTLER
              && a[2].kind == MMO_ANIM_FLICKER,
          "a hit flickers the target");
    CHECK(got >= 4 && a[3].command == MMO_BTLCMD_UPDATE_HP_GAUGE
              && a[3].kind == MMO_ANIM_HP && a[3].hp == 16,
          "then the gauge is told the resulting hp");
    CHECK(mmo_battle_anim_write(&a[0], msg, 4) == 4
              && msg[0] == MMO_BTLCMD_PRINT_ATTACK_MESSAGE
              && msg[2] == 33 && msg[3] == 0,
          "the print message is the 4-byte PRINT_ATTACK the scene reads");
    CHECK(mmo_battle_anim_write(&a[1], msg, sizeof msg) == 88
              && msg[0] == 0x16 && msg[2] == 33 && msg[3] == 0
              && msg[76] == 0,
          "the move message is the 88-byte SET_MOVE_ANIMATION the scene reads");

    n = load(GAME_FX "game/s2c/33/stat_change_growl_31914.bin", b, sizeof b);
    CHECK(n == 29 && mmo_game_read_battle_move_event(b, n, &mv) == 0,
          "the Growl body still walks");
    got = mmo_battle_map_move(&mv, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 3, "Growl produces print, move, then the stat-drop");
    CHECK(got >= 1 && a[0].kind == MMO_ANIM_PRINT && a[0].id == 45,
          "the first command is PRINT_ATTACK of Growl");
    CHECK(got >= 2 && a[1].id == 45 && a[1].kind == MMO_ANIM_MOVE
              && !a[1].fallback,
          "then SET_MOVE_ANIMATION of Growl");
    CHECK(got >= 3 && a[2].kind == MMO_ANIM_STATUS
              && a[2].anim_mode == 1
              && a[2].id == MMO_BTL_SUBANIM_STAT_DROP
              && a[2].command == MMO_BTLCMD_SET_MOVE_ANIMATION,
          "the stage drop is WE_SUB STAT_DROP");
    CHECK(mmo_battle_anim_write(&a[2], msg, sizeof msg) == 88
              && msg[0] == 0x16 && msg[76] == 1
              && msg[80] == MMO_BTL_SUBANIM_STAT_DROP && msg[2] == 0,
          "a status message is SET_MOVE_ANIMATION with animMode 1");

    n = load(GAME_FX "game/s2c/33/growl_no_target_31914.bin", b, sizeof b);
    CHECK(n == 12 && mmo_game_read_battle_move_event(b, n, &mv) == 0,
          "a Growl with no targets still walks");
    got = mmo_battle_map_move(&mv, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 2 && a[0].kind == MMO_ANIM_PRINT
              && a[1].id == 45 && a[1].kind == MMO_ANIM_MOVE,
          "no targets means the print and the move");
}

static void test_fallback_and_faint(void)
{
    mmo_battle_move_event mv;
    mmo_battle_anim a[MMO_BATTLE_ANIM_MAX];
    int got;
    u8 msg[4];

    printf("a move the engine cannot draw falls back, a KO faints:\n");

    memset(&mv, 0, sizeof mv);
    mv.source_move = 468;
    got = mmo_battle_map_move(&mv, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 2 && a[0].kind == MMO_ANIM_PRINT && a[0].id == 468
              && a[1].fallback && a[1].id == MMO_BTL_ANIM_FALLBACK_MOVE
              && a[1].why != NULL && a[1].command == MMO_BTLCMD_SET_MOVE_ANIMATION,
          "a post-DS move prints its id and plays as Pound");

    mv.source_move = 1000;
    got = mmo_battle_map_move(&mv, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 2 && a[0].kind == MMO_ANIM_PRINT && a[0].id == 1000
              && a[1].fallback && a[1].id == MMO_BTL_ANIM_FALLBACK_MOVE
              && a[1].why != NULL,
          "an overlay-only move prints its id and plays as Pound too");

    mv.source_move = 0;
    got = mmo_battle_map_move(&mv, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 2 && a[1].fallback && a[1].id == 1 && a[1].why != NULL,
          "move 0 is the same fallback");

    memset(&mv, 0, sizeof mv);
    mv.source_move = 33;
    mv.n_targets = 1;
    mv.target[0].n_subs = 1;
    mv.target[0].sub[0].type = MMO_BATTLE_SUB_HP;
    mv.target[0].sub[0].hp = 0;
    got = mmo_battle_map_move(&mv, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 5 && a[0].kind == MMO_ANIM_PRINT
              && a[1].id == 33 && !a[1].fallback
              && a[2].kind == MMO_ANIM_FLICKER
              && a[3].kind == MMO_ANIM_HP && a[3].hp == 0
              && a[4].kind == MMO_ANIM_FAINT
              && a[4].command == MMO_BTLCMD_PLAY_FAINTING_SEQUENCE,
          "an HP of 0 adds the fainting sequence");
    CHECK(mmo_battle_anim_write(&a[2], msg, sizeof msg) == 4
              && msg[0] == 0x17,
          "flicker is the 4-byte command the scene executed");

    CHECK(mmo_battle_map_move(NULL, a, MMO_BATTLE_ANIM_MAX) == -1,
          "a missing event is refused");
    CHECK(mmo_battle_command_count() == 67
              && strcmp(mmo_battle_command_name(0), "NONE") == 0
              && strcmp(mmo_battle_command_name(20), "PRINT_ATTACK_MESSAGE") == 0
              && strcmp(mmo_battle_command_name(66), "CLEAR_MESSAGE_BOX") == 0
              && mmo_battle_command_name(67) == NULL
              && strcmp(mmo_battle_anim_name(MMO_BTLCMD_SET_MOVE_ANIMATION),
                        "SET_MOVE_ANIMATION") == 0
              && strcmp(mmo_battle_anim_name(99), "?") == 0,
          "the 67 command names are the engine's");
}

static void test_map_outcomes(void)
{
    mmo_battle_switch_in sw;
    mmo_battle_anim a[MMO_BATTLE_ANIM_MAX];
    int got;
    u8 msg[MMO_BTL_ANIM_SHOW_SIZE];

    printf("catch, switch and rewards map onto the engine's own commands:\n");

    memset(&sw, 0, sizeof sw);
    sw.entity_id = 7;
    sw.species = 25;
    sw.hp = 20;
    got = mmo_battle_map_switch(&sw, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 2 && a[0].kind == MMO_ANIM_RETURN
              && a[0].command == MMO_BTLCMD_RETURN_POKEMON
              && a[1].kind == MMO_ANIM_SHOW
              && a[1].command == MMO_BTLCMD_SHOW_POKEMON
              && a[1].id == 25 && a[1].source == 7,
          "a switch-in is RETURN then SHOW");
    CHECK(mmo_battle_anim_write(&a[1], msg, sizeof msg) == MMO_BTL_ANIM_SHOW_SIZE
              && msg[0] == MMO_BTLCMD_SHOW_POKEMON && msg[2] == 25,
          "SHOW_POKEMON carries the species in the same slot SET_MOVE uses");

    got = mmo_battle_map_catch(5004, 9, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 1 && a[0].kind == MMO_ANIM_CATCH
              && a[0].command == MMO_BTLCMD_OPEN_CAPTURE_BALL
              && a[0].id == 5004 && a[0].source == 9,
          "a ball throw is OPEN_CAPTURE_BALL");
    CHECK(mmo_battle_anim_write(&a[0], msg, 4) == 4
              && msg[0] == MMO_BTLCMD_OPEN_CAPTURE_BALL
              && msg[2] == 0x8c && msg[3] == 0x13,
          "the catch message is 4 bytes with the item at offset 2");

    got = mmo_battle_map_switch_prompt(a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 1 && a[0].kind == MMO_ANIM_PARTY
              && a[0].command == MMO_BTLCMD_SHOW_PARTY_MENU,
          "a forced switch opens the party menu");

    got = mmo_battle_map_reward(7, 46, 0, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 1 && a[0].kind == MMO_ANIM_EXP
              && a[0].command == MMO_BTLCMD_UPDATE_EXP_GAUGE
              && a[0].hp == 46,
          "a reward is UPDATE_EXP_GAUGE of the gained XP");
    CHECK(mmo_battle_anim_write(&a[0], msg, 16) == 16
              && msg[0] == MMO_BTLCMD_UPDATE_EXP_GAUGE
              && msg[8] == 46 && msg[9] == 0,
          "gainedExp sits at offset 8");

    got = mmo_battle_map_reward(7, 46, 1, a, MMO_BATTLE_ANIM_MAX);
    CHECK(got == 2 && a[1].kind == MMO_ANIM_LEVEL
              && a[1].command == MMO_BTLCMD_PLAY_LEVEL_UP_ANIMATION,
          "a level-up adds PLAY_LEVEL_UP_ANIMATION");

    CHECK(mmo_battle_map_switch(NULL, a, MMO_BATTLE_ANIM_MAX) == -1,
          "a missing switch-in is refused");
}

int battle_anim_tests_run(void)
{
    failures = 0;
    test_map_known_move();
    test_fallback_and_faint();
    test_map_outcomes();
    if (failures)
        printf("battle_anim: %d check(s) FAILED\n", failures);
    else
        printf("battle_anim: all checks passed\n");
    return failures;
}
