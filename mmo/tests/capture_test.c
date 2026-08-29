/* The client's decoders against real official-client bytes. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "game.h"
#include "login.h"
#include "crypto.h"

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
#define LOGIN_FX MMO_REPO_ROOT "/protocols.login/src/test/resources/fixtures/"

/* Read a committed capture fixture. Returns the byte count, or 0 if the file is
 * missing or does not fit, both of which are failures, never a silent skip:
 * the fixtures are committed beside the suite. */
static size_t load(const char *path, u8 *out, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("  FAIL cannot open fixture %s\n", path);
        failures++;
        return 0;
    }
    size_t n = fread(out, 1, cap, f);
    int over = !feof(f) && fread(out, 1, 1, f) == 1;
    fclose(f);
    if (over) {
        printf("  FAIL fixture larger than the read buffer: %s\n", path);
        failures++;
        return 0;
    }
    return n;
}

/* --- presence: LoadEntity (0x05) ----------------------------------------- */

static void test_load_entity(void)
{
    printf("LoadEntity (0x05) from the official client's own stream:\n");

    u8 b[512];
    size_t n = load(GAME_FX "game/s2c/05/load_entity_31914.bin", b, sizeof b);
    mmo_load_entity e;
    CHECK(n == 48, "the plain spawn is 48 bytes");
    CHECK(n && mmo_game_read_load_entity(b, n, &e) == 0,
          "the client decodes an official LoadEntity");
    if (n) {
        CHECK(strcmp(e.name, "Player") == 0, "name reads as \"Player\"");
        CHECK(e.region_id == 0 && e.bank_id == 4 && e.map_id == 1,
              "region/bank/map read as 0/4/1");
        CHECK(e.x == 6 && e.y == 6 && e.z == 0, "tile reads as (6,6,0)");
        CHECK(e.facing == 0, "facing reads as DOWN");
        CHECK(e.skin_region == 3 && e.skin_count == 5,
              "the SkinSet is region 3 with five populated slots");
        CHECK(e.transportation == 0 && e.entity_state == 0,
              "the two bytes after the heading are 0");
        CHECK(e.gender == 0, "the gender byte reads as male");
        CHECK(e.has_follower == 0, "no follower");
    }

    /* The same packet with the follower bit set: the conditional tail is the
     * part a wrong flags layout would desync on. */
    n = load(GAME_FX "game/s2c/05/load_entity_follower_31914.bin", b, sizeof b);
    CHECK(n == 50, "the follower spawn is 50 bytes");
    CHECK(n && mmo_game_read_load_entity(b, n, &e) == 0,
          "the client decodes an official LoadEntity carrying a follower");
    if (n) {
        CHECK(e.has_follower == 1 && e.follower_dex == 1,
              "the follower flag and its dex id are read from the tail");
        CHECK(e.map_id == 0x13 && e.x == 13 && e.y == 2,
              "map 0x13 at tile (13,2)");
    }
}

/* --- presence: GbaEntityMove (0xEA) -------------------------------------- */

static void test_gba_move(void)
{
    printf("GbaEntityMove (0xEA) from the official client's own stream:\n");

    u8 b[64];
    size_t n = load(GAME_FX "game/s2c/ea/gba_entity_move_31914.bin", b, sizeof b);
    CHECK(n == 14, "the step is 14 bytes");
    mmo_gba_move m;
    CHECK(n && mmo_game_read_gba_move(b, n, &m) == 0,
          "the client decodes an official GbaEntityMove");
    if (n) {
        CHECK(m.bank_id == 4 && m.map_id == 3, "bank/map read as 4/3");
        CHECK(m.x == 6 && m.y == 12, "tile reads as (6,12)");
        CHECK(m.movement_mode == 2 && m.direction == 1,
              "walking, facing the server's UP");
    }
}

/* --- the join leg: JoinResponse (0x01) ----------------------------------- */

static void test_join_response(void)
{
    printf("JoinResponse (0x01) from the official client's own stream:\n");

    u8 b[64];
    size_t n = load(GAME_FX "game/s2c/01/join_response_31914.bin", b, sizeof b);
    CHECK(n == 24, "the response is 24 bytes");
    mmo_join_response jr;
    CHECK(n && mmo_game_read_join_response(b, n, &jr) == 0,
          "the client decodes an official JoinResponse");
    if (n) {
        CHECK(jr.can_join == 1, "canJoin is true");
        CHECK(jr.playtime == 1337 && jr.reward_points == 420 && jr.balance == 187,
              "playtime/rewardPoints/balance read as 1337/420/187");
    }
}

/* --- the overworld: LoadMap (0x10) --------------------------------------- */

static void test_load_map(void)
{
    printf("LoadMap (0x10) from the official client's own stream:\n");

    u8 b[512];
    size_t n = load(GAME_FX "game/s2c/10/load_map_31914.bin", b, sizeof b);
    CHECK(n == 55, "the map block is 55 bytes");
    mmo_load_map lm;
    CHECK(n && mmo_game_read_load_map(b, n, &lm) == 0,
          "the client decodes an official LoadMap");
    if (n) {
        CHECK(lm.is_nds == 0, "it is a GBA map");
        CHECK(lm.region_id == 0 && lm.bank_id == 3 && lm.map_id == 0x13,
              "region/bank/map read as 0/3/0x13");
        CHECK(lm.width == 24 && lm.height == 40, "the grid is 24x40");
        CHECK(lm.reload_player == 1 && lm.delete_cache == 1,
              "the packed prefix asks for a player reload and a cache drop");
        CHECK(lm.connection_total == 2, "two edge connections");
    }
}

/* --- progression: StoryFlagUpdate (0x2A) and the flag table (0x0A) -------- */

static void test_story(void)
{
    printf("StoryFlagUpdate (0x2A) and WorldFlagTableReset (0x0A) from official:\n");

    u8 b[512];
    size_t n = load(GAME_FX "game/s2c/2a/story_flag_set_31914.bin", b, sizeof b);
    CHECK(n == 5, "a story-flag delta is 5 bytes");
    int region = -1, flag = -1, on = -1;
    CHECK(n && mmo_game_read_story_flag(b, n, &region, &flag, &on) == 0,
          "the client decodes an official StoryFlagUpdate");
    CHECK(region == 0 && flag == 0x2C && on == 1,
          "region 0, flag 0x2C, set");

    n = load(GAME_FX "game/s2c/0a/world_flag_table_31914.bin", b, sizeof b);
    CHECK(n == 60, "the flag table is 60 bytes of zlib groups");
    static u8 blocks[8 * 128];
    int lens[8], count = 0;
    CHECK(n && mmo_game_read_world_flag_reset(b, n, blocks, 128, lens, 8, &count) == 0,
          "the client inflates an official WorldFlagTableReset");
    CHECK(count == 4, "four flag groups");
    CHECK(count == 4 && lens[0] == 64 && lens[1] == 64 && lens[2] == 64 && lens[3] == 0,
          "three 64-byte bit arrays and one empty group");
}

/* --- world state: LocalPlayerState (0xF3) -------------------------------- */

static void test_local_player_state(void)
{
    printf("LocalPlayerState (0xF3) from the official client's own stream:\n");

    u8 b[512];
    size_t n = load(GAME_FX "game/s2c/f3/local_player_state_31914.bin", b, sizeof b);
    CHECK(n == 65, "the state block is 65 bytes");
    mmo_local_player_state ps;
    CHECK(n && mmo_game_read_local_player_state(b, n, &ps) == 0,
          "the client decodes an official LocalPlayerState");
    if (n) {
        CHECK(ps.region == 0 && ps.map_id == 19, "region 0, map 19");
        CHECK(ps.x == 13 && ps.y == 2 && ps.z == 0, "tile reads as (13,2,0)");
        CHECK(ps.money == 28900, "money reads as 28900");
        CHECK(ps.party_total == 1 && ps.party_dex[0] == 1,
              "a one-monster party, national dex 1");
        CHECK(ps.badge_count == 0 && ps.var_count == 7,
              "no badges and seven story variables");
        /* Seven entries in the 21 bytes that close a 65-byte block pins the entry
         * at three bytes wide. It does not pin how those three split: this block
         * is an S2C capture, so the bytes are the server's encoding and only the
         * official client's own reader says which two are the key. */
        CHECK(ps.var_stored == 7, "all seven entries are walked to the end of the block");
    }
}

/* --- cash delta: LocalCharacterDelta (0x0C) ------------------------------ */

static void test_local_character_delta(void)
{
    printf("LocalCharacterDelta (0x0C) from a capture the official client accepted:\n");

    u8 b[32];
    size_t n = load(GAME_FX "game/s2c/0c/money_32763.bin", b, sizeof b);
    CHECK(n == 6, "the money-only body is 6 bytes");
    mmo_local_character_delta d;
    CHECK(n && mmo_game_read_local_character_delta(b, n, &d) == 0,
          "the client decodes an official LocalCharacterDelta");
    CHECK(d.has_money && d.money == 1900,
          "mask bit 0x1 reads as cash 1900");
}

/* --- the battle-event stream -------------------------------------------- */

/* The first wild fight in the official client playthrough, from the 0x30 that opens it
 * to the 0x31 that closes it. The bodies are what our server sent and the
 * official client accepted; the walk is that client's own readers (f/L80, f/Y9,
 * f/qx0, f/Ln1, f/CO1). Entity ids match the presence pair below. */

static const u32 OFFICIAL_SELF_MON  = (u32)0x200129EF0F0AC000LL;
static const u32 OFFICIAL_WILD_MON  = (u32)0x20012A25E80BC000LL;

static void test_battle_event_stream(void)
{
    printf("the battle-event stream from the official client's own fight:\n");

    u8 b[128];
    size_t n;

    n = load(GAME_FX "game/s2c/32/action_prompt_31914.bin", b, sizeof b);
    mmo_battle_queued q;
    CHECK(n == 1 && mmo_game_read_battle_queued(b, n, &q) == 0,
          "the action prompt is one packed byte");
    CHECK(q.value == 0 && q.flag == 1, "the high bit is the prompt flag");

    n = load(GAME_FX "game/s2c/33/stat_change_growl_31914.bin", b, sizeof b);
    mmo_battle_move_event mv;
    CHECK(n == 29 && mmo_game_read_battle_move_event(b, n, &mv) == 0,
          "a Growl walks as a move event");
    CHECK(mv.source_entity == OFFICIAL_WILD_MON && mv.source_move == 45,
          "the wild side used Growl");
    CHECK(mv.n_targets == 1 && mv.target[0].n_subs == 1
              && mv.target[0].sub[0].type == MMO_BATTLE_SUB_STAT
              && mv.target[0].sub[0].stat == 1
              && mv.target[0].sub[0].stages == -1,
          "its only sub-event is attack down one");

    n = load(GAME_FX "game/s2c/33/hp_update_tackle_31914.bin", b, sizeof b);
    CHECK(n == 27 && mmo_game_read_battle_move_event(b, n, &mv) == 0,
          "a Tackle walks as a move event");
    CHECK(mv.source_entity == OFFICIAL_SELF_MON && mv.source_move == 33,
          "the player's side used Tackle");
    CHECK(mv.n_targets == 1 && mv.target[0].outcome == 0x0200
              && mv.target[0].n_subs == 1
              && mv.target[0].sub[0].type == MMO_BATTLE_SUB_HP
              && mv.target[0].sub[0].hp == 16,
          "its outcome word is a hit and the resulting hp is 16");

    n = load(GAME_FX "game/s2c/33/growl_no_target_31914.bin", b, sizeof b);
    CHECK(n == 12 && mmo_game_read_battle_move_event(b, n, &mv) == 0,
          "a Growl with no targets still walks");
    CHECK(mv.source_move == 45 && mv.n_targets == 0,
          "the target list can be empty");

    n = load(GAME_FX "game/s2c/16/experience_31914.bin", b, sizeof b);
    mmo_battle_entity_delta d;
    CHECK(n == 17 && mmo_game_read_battle_entity_delta(b, n, &d) == 0,
          "an experience delta walks");
    CHECK(d.entity_id == OFFICIAL_SELF_MON && (d.mask & MMO_BATTLE_DELTA_EXPERIENCE)
              && d.xp_level == 6 && d.xp == 181 && d.hp == -1,
          "it carries only the level and the running total");

    n = load(GAME_FX "game/s2c/16/level_up_31914.bin", b, sizeof b);
    CHECK(n == 38 && mmo_game_read_battle_entity_delta(b, n, &d) == 0,
          "a level-up delta walks");
    CHECK((d.mask & MMO_BATTLE_DELTA_STATS) && (d.mask & MMO_BATTLE_DELTA_HP)
              && (d.mask & MMO_BATTLE_DELTA_EVS)
              && d.hp == 13 && d.stats[0] == 22 && d.have_stats && d.have_evs,
          "it carries stats, hp and EVs but no experience");

    n = load(GAME_FX "game/s2c/79/xp_reward_31914.bin", b, sizeof b);
    mmo_battle_stat_counters sc;
    CHECK(n == 13 && mmo_game_read_battle_stat_counters(b, n, &sc) == 0,
          "the reward counters walk");
    CHECK(sc.entity_id == OFFICIAL_SELF_MON && sc.base == 46 && !sc.have[0],
          "the base counter is the XP gained and no extra group is set");

    n = load(GAME_FX "game/s2c/31/battle_end_31914.bin", b, sizeof b);
    mmo_battle_bulk_state bs;
    CHECK(n == 15 && mmo_game_read_battle_bulk_state(b, n, &bs) == 0,
          "the closing bulk state walks");
    CHECK(bs.phase == 0 && bs.prize == 0 && bs.flag == 2,
          "phase 0 and flag 2 is a resolved fight");

    /* source + move + kind + one target + one sub of type 99, no body. */
    u8 bad[25] = { 0 };
    bad[10] = 1;                          /* kind */
    bad[11] = 1;                          /* one target */
    bad[22] = 1;                          /* one sub */
    bad[23] = 99;                         /* unknown type */
    CHECK(mmo_game_read_battle_move_event(bad, sizeof bad, &mv) == -1,
          "an unknown sub-event type is refused");
}

/* --- the battle door: BattleFieldState (0x30) ---------------------------- */

static void test_battle_field_state(void)
{
    printf("BattleFieldState (0x30) from the official client's own stream:\n");

    u8 b[1024];
    size_t n = load(GAME_FX "game/s2c/30/battle_wild_31914.bin", b, sizeof b);
    CHECK(n == 200, "the opening field state is 200 bytes");
    mmo_battle_field_state bs;
    CHECK(n && mmo_game_read_battle_field_state(b, n, &bs) == 0,
          "the client decodes an official BattleFieldState");
    CHECK(bs.wild == 1 && bs.opposing == MMO_BATTLE_OPPOSING_WILD,
          "the encounter reads as wild");
}

/* --- the battle door's other half: EntityPresence (0x0F) ----------------- */

/*
 * The two frames the battle scene is opened and closed by, taken either side of one wild fight
 * in an official session.
 */
static void test_entity_presence(void)
{
    printf("EntityPresence (0x0F) either side of an official battle:\n");

    u8 b[64];
    mmo_entity_presence in_battle, overworld;

    size_t n = load(GAME_FX "game/s2c/0f/presence_in_battle_31914.bin", b, sizeof b);
    CHECK(n == 9, "the presence body is an id and a status byte");
    CHECK(n && mmo_game_read_entity_presence(b, n, &in_battle) == 0,
          "the client decodes the presence that opens a battle");
    CHECK(in_battle.status == MMO_PRESENCE_IN_BATTLE,
          "the status reads as in-battle");

    n = load(GAME_FX "game/s2c/0f/presence_overworld_31914.bin", b, sizeof b);
    CHECK(n && mmo_game_read_entity_presence(b, n, &overworld) == 0,
          "the client decodes the presence that closes it");
    CHECK(overworld.status == MMO_PRESENCE_OVERWORLD,
          "the status reads as the overworld");

    CHECK(in_battle.entity_id == overworld.entity_id,
          "both name the same entity, so the status is the only thing that moved");
    CHECK(in_battle.entity_id == (u32)0x2001291A52099000LL,
          "and that entity is the captured session's own character");
}

/* --- what the client sends: pinned to what the official client sends ------------------ */

static void test_client_to_server(void)
{
    printf("what the client sends, against what the official client sent:\n");

    u8 want[256];
    size_t n = load(GAME_FX "game/c2s/06/movement_31914.bin", want, sizeof want);
    mmo_wbuf w;
    mmo_wbuf_init(&w);
    /* The official client's step: from tile (13,2), heading DOWN, walking. */
    mmo_game_write_movement(&w, 13, 2, 0, 0);
    CHECK(n == 5 && w.len == n && memcmp(w.data, want, n) == 0,
          "MovementPacket matches the official body byte for byte");
    mmo_wbuf_free(&w);

    /* The run-away intent, which is how a battle is left. The official client composed these
     * two bytes, so they settle both the slot reference and the action kind, and
     * they say the kind carries no tail. */
    n = load(GAME_FX "game/c2s/32/run.bin", want, sizeof want);
    mmo_wbuf_init(&w);
    mmo_game_write_battle_run(&w);
    CHECK(n == 2 && w.len == n && memcmp(w.data, want, n) == 0,
          "BattleActionSelect's run matches the official body byte for byte");
    mmo_wbuf_free(&w);

    /*
     * MOVE: the official client's playthrough sent Tackle (id 33) nineteen times, always slot 0 and extra 0.
     * ITEM: the ball throw and the potion-on-monster from the same client's item-use / top-up
     * taps.
     */
    n = load(GAME_FX "game/c2s/32/move_tackle_31914.bin", want, sizeof want);
    {
        mmo_battle_select sel;
        memset(&sel, 0, sizeof sel);
        sel.action = MMO_BATTLE_ACTION_MOVE;
        sel.move_or_item = 33;
        mmo_wbuf_init(&w);
        CHECK(mmo_game_write_battle_select(&w, &sel) == 0 &&
                  n == 5 && w.len == n && memcmp(w.data, want, n) == 0,
              "BattleActionSelect's move matches the official Tackle body");
        mmo_wbuf_free(&w);
    }
    n = load(GAME_FX "game/c2s/32/item_throw.bin", want, sizeof want);
    {
        mmo_battle_select sel;
        memset(&sel, 0, sizeof sel);
        sel.action = MMO_BATTLE_ACTION_ITEM;
        sel.move_or_item = 5004;
        sel.target = 0;
        sel.extra = 0xFF;
        mmo_wbuf_init(&w);
        CHECK(mmo_game_write_battle_select(&w, &sel) == 0 &&
                  n == 13 && w.len == n && memcmp(w.data, want, n) == 0,
              "BattleActionSelect's item matches the official ball-throw body");
        mmo_wbuf_free(&w);
    }
    n = load(GAME_FX "game/c2s/32/item_potion_31914.bin", want, sizeof want);
    {
        mmo_battle_select sel;
        memset(&sel, 0, sizeof sel);
        sel.action = MMO_BATTLE_ACTION_ITEM;
        sel.move_or_item = 5017;
        sel.target = 0x200129EF0F0AC000LL;
        sel.extra = 0xFF;
        mmo_wbuf_init(&w);
        CHECK(mmo_game_write_battle_select(&w, &sel) == 0 &&
                  n == 13 && w.len == n && memcmp(w.data, want, n) == 0,
              "BattleActionSelect's item matches the official potion body");
        mmo_wbuf_free(&w);
    }

    n = load(GAME_FX "game/c2s/08/chat_31914.bin", want, sizeof want);
    mmo_wbuf_init(&w);
    mmo_game_write_chat(&w, "hi guys");
    CHECK(n == 17 && w.len == n && memcmp(w.data, want, n) == 0,
          "ChatMessageSend matches the official body byte for byte");
    mmo_wbuf_free(&w);

    /* Buy: three Poké Balls. The body is what the official client writes (item, qty,
     * type 0) and what the committed buy_poke_balls fixture holds. */
    n = load(GAME_FX "game/c2s/23/buy_poke_balls.bin", want, sizeof want);
    mmo_wbuf_init(&w);
    mmo_game_write_shop_buy(&w, 5004, 3);
    CHECK(n == 5 && w.len == n && memcmp(w.data, want, n) == 0,
          "a buy matches the committed ExchangeItemRequest body");
    mmo_wbuf_free(&w);

    /* SelectCharacter: the official client sends a real characterIdHash, the client sends 0. */
    n = load(GAME_FX "game/c2s/04/select_character_31914.bin", want, sizeof want);
    CHECK(n == 16, "the official SelectCharacter body is 16 bytes");
    mmo_wbuf_init(&w);
    mmo_game_write_select_character(&w, 0x2001291A52099000LL, 0);
    CHECK(n == 16 && w.len == 16 && memcmp(w.data, want, 8) == 0,
          "SelectCharacter's characterId matches official");
    CHECK(n == 16 && w.len == 16 && memcmp(w.data + 8, want + 8, 8) != 0,
          "DIVERGENCE: the official client sends a non-zero characterIdHash, the client sends 0");
    mmo_wbuf_free(&w);

    /*
     * LoginRequest. The 32-byte blob after the username is the hwid, official fills it with
     * SHA-256 of this machine's id, and the trailer is the client and installation revisions
     * followed by the platform byte.
     */
    n = load(LOGIN_FX "login/c2s/11/login_request_31914.bin", want, sizeof want);
    CHECK(n == 144, "the official LoginRequest body is 144 bytes");
    char pwhex[41];
    mmo_sha1_hex("test", 4, pwhex);
    u8 hwid[MMO_SHA256_DIGEST];
    size_t hwidlen = mmo_login_hwid(hwid);
    mmo_wbuf_init(&w);
    /* The fixture is a capture of a login that did not ask to be remembered,
     * so the comparison has to ask the same. */
    mmo_login_write_request(&w, "test", pwhex, 0);
    CHECK(w.len == 112 + hwidlen,
          "the client's LoginRequest body carries its hwid");
    CHECK(hwidlen == 0 || w.len == n,
          "with a machine id the client's body is the official client's 144 bytes");
    CHECK(n == 144 && memcmp(w.data, want, 10) == 0,
          "the username field matches official");
    CHECK(n == 144 && w.len >= 14 &&
              memcmp(w.data + w.len - 14, want + n - 14, 14) == 0,
          "the language, both revisions and the os byte match official");
    CHECK(n == 144 && w.data[10] != want[10],
          "DIVERGENCE: the official client's capture clears manualLogin, the client sets it");
    mmo_wbuf_free(&w);

    /* CreateCharacter: the three captured bodies, rebuilt from the same name,
     * gender, region and five-slot wardrobe the captures carry. */
    n = load(GAME_FX "game/c2s/03/female_hoenn.bin", want, sizeof want);
    {
        mmo_create_character in;
        memset(&in, 0, sizeof in);
        in.name = "LananaTestTwo";
        in.gender = 1;
        in.starting_region = 1;
        in.appearance.region_selection_index = 1;
        in.appearance.slot[MMO_SKIN_HAIR].present = 1;
        in.appearance.slot[MMO_SKIN_HAIR].type = 4;
        in.appearance.slot[MMO_SKIN_HAIR].color = 24;
        in.appearance.slot[MMO_SKIN_EYES].present = 1;
        in.appearance.slot[MMO_SKIN_EYES].type = 15;
        in.appearance.slot[MMO_SKIN_TOP].present = 1;
        in.appearance.slot[MMO_SKIN_TOP].type = 3;
        in.appearance.slot[MMO_SKIN_TOP].color = 2;
        in.appearance.slot[MMO_SKIN_FOOTWEAR].present = 1;
        in.appearance.slot[MMO_SKIN_FOOTWEAR].color = 26;
        in.appearance.slot[MMO_SKIN_LEGGINGS].present = 1;
        in.appearance.slot[MMO_SKIN_LEGGINGS].color = 33;
        mmo_wbuf_init(&w);
        CHECK(mmo_game_write_create_character(&w, &in) == 0 &&
                  n == 43 && w.len == n && memcmp(w.data, want, n) == 0,
              "CreateCharacter matches the captured female Hoenn body");
        mmo_wbuf_free(&w);
    }

    n = load(GAME_FX "game/c2s/03/male_hoenn_32710.bin", want, sizeof want);
    {
        mmo_create_character in;
        memset(&in, 0, sizeof in);
        in.name = "MacherDer";
        in.gender = 0;
        in.starting_region = 1;
        in.appearance.region_selection_index = 3;
        in.appearance.slot[MMO_SKIN_HAIR].present = 1;
        in.appearance.slot[MMO_SKIN_HAIR].type = 30;
        in.appearance.slot[MMO_SKIN_HAIR].color = 10;
        in.appearance.slot[MMO_SKIN_EYES].present = 1;
        in.appearance.slot[MMO_SKIN_EYES].type = 10;
        in.appearance.slot[MMO_SKIN_TOP].present = 1;
        in.appearance.slot[MMO_SKIN_TOP].type = 2;
        in.appearance.slot[MMO_SKIN_TOP].color = 21;
        in.appearance.slot[MMO_SKIN_FOOTWEAR].present = 1;
        in.appearance.slot[MMO_SKIN_FOOTWEAR].color = 28;
        in.appearance.slot[MMO_SKIN_LEGGINGS].present = 1;
        in.appearance.slot[MMO_SKIN_LEGGINGS].color = 8;
        mmo_wbuf_init(&w);
        CHECK(mmo_game_write_create_character(&w, &in) == 0 &&
                  n == 35 && w.len == n && memcmp(w.data, want, n) == 0,
              "CreateCharacter matches the captured male Hoenn body");
        mmo_wbuf_free(&w);
    }

    n = load(GAME_FX "game/c2s/03/female_kanto_32710.bin", want, sizeof want);
    {
        mmo_create_character in;
        memset(&in, 0, sizeof in);
        in.name = "MacherRin";
        in.gender = 1;
        in.starting_region = 0;
        in.appearance.region_selection_index = 0;
        in.appearance.slot[MMO_SKIN_HAIR].present = 1;
        in.appearance.slot[MMO_SKIN_HAIR].type = 11;
        in.appearance.slot[MMO_SKIN_HAIR].color = 46;
        in.appearance.slot[MMO_SKIN_EYES].present = 1;
        in.appearance.slot[MMO_SKIN_EYES].type = 14;
        in.appearance.slot[MMO_SKIN_TOP].present = 1;
        in.appearance.slot[MMO_SKIN_TOP].type = 3;
        in.appearance.slot[MMO_SKIN_TOP].color = 6;
        in.appearance.slot[MMO_SKIN_FOOTWEAR].present = 1;
        in.appearance.slot[MMO_SKIN_FOOTWEAR].color = 12;
        in.appearance.slot[MMO_SKIN_LEGGINGS].present = 1;
        in.appearance.slot[MMO_SKIN_LEGGINGS].type = 2;
        in.appearance.slot[MMO_SKIN_LEGGINGS].color = 39;
        mmo_wbuf_init(&w);
        CHECK(mmo_game_write_create_character(&w, &in) == 0 &&
                  n == 35 && w.len == n && memcmp(w.data, want, n) == 0,
              "CreateCharacter matches the captured female Kanto body");
        mmo_wbuf_free(&w);
    }
}

/* --- the monster record: the packet the official client cannot read --------- */

static void test_characters_list(void)
{
    printf("CharactersList (0x02) carrying two monsters, from official:\n");

    u8 b[1024];
    size_t n = load(GAME_FX "game/s2c/02/character_list_two_monsters_31914.bin",
                    b, sizeof b);
    CHECK(n == 401, "the two-monster list is 401 bytes");
    s64 id = 0;
    int count = mmo_game_read_first_character_id(b, n, &id);
    CHECK(count == 1, "one character in the list");
    CHECK(id == 0x2001291A52099000LL, "its id is read");

    /* The 401-byte body is the lockout capture: each party record is one byte
     * longer than the official client reads. The CharacterInfo and both SkinSets still sit
     * in front of that, so an empty-party slice of the same header is a list
     * the official client reader finishes. */
    u8 one[128];
    CHECK(n > 123, "the header reaches the party count");
    memcpy(one, b, 123);
    one[122] = 0; /* party 0: drop the two over-long records */

    mmo_character_list list;
    CHECK(mmo_game_read_character_list(one, 123, &list) == 0,
          "an empty-party slice of the capture walks to its last byte");
    CHECK(list.count == 1 && list.held == 1, "one entry is held");
    CHECK(list.entry[0].id == 0x2001291A52099000LL, "the id is the capture's");
    CHECK(strcmp(list.entry[0].name, "Player") == 0, "the name is Player");
    CHECK(list.entry[0].gender == 0, "rivalSex is male");
    CHECK(list.entry[0].region == 0, "positionRegionId is Kanto");

    u8 two[256];
    two[0] = 2;
    memcpy(two + 1, one + 1, 122);
    memcpy(two + 123, one + 1, 122);
    CHECK(mmo_game_read_character_list(two, 245, &list) == 0 &&
              list.count == 2 && list.held == 2,
          "two concatenated entries walk to the last byte");
    CHECK(mmo_game_pick_character(&list, "Player", -1, -1, NULL, 0) == -1,
          "the same name twice is refused rather than guessed");
}

int capture_tests_run(void)
{
    failures = 0;
    test_load_entity();
    test_gba_move();
    test_join_response();
    test_load_map();
    test_story();
    test_local_player_state();
    test_local_character_delta();
    test_battle_field_state();
    test_battle_event_stream();
    test_entity_presence();
    test_client_to_server();
    test_characters_list();

    if (failures)
        printf("official: %d check(s) FAILED\n", failures);
    else
        printf("official: all checks passed\n");
    return failures;
}
