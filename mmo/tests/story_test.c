/*
 * The server-owned progression store: the world-flag table, the story flags and
 * the story variables the overworld reads from the server and never writes back.
 */
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "codec.h"
#include "client.h"

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

/* The server's WORLD_FLAG_GROUPS (WorldStateService.kt), each a zlib stream. */
static const u8 grp0[17] = {
    0x78,0x9c,0x63,0x70,0x60,0xa0,0x04,0x34,0xa8,0xb0,0x32,0x00,0x00,0x13,0x39,0x00,0xea
};
static const u8 grp12[17] = {
    0x78,0x9c,0x63,0x70,0x60,0xa0,0x04,0x34,0x30,0x30,0x32,0x00,0x00,0x12,0xc5,0x00,0xc2
};

/* The 67-byte blocks those streams decompress to (python zlib.decompress): a
 * big-endian U16 length of 64, 64 bytes of flag bits, and a zero count of keyed
 * flag words. The reader hands back only the middle part. */
static const u8 block0[67] = {
    0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,
    0x24,0x05,0x00
};
static const u8 block12[67] = {
    0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,
    0x00,0x01,0x00
};

/* Build the WorldFlagTableResetPacket body: a U8 group count, each group a
 * U16LE-length-prefixed blob. */
static size_t build_flag_reset(u8 *out)
{
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    mmo_put_u8(&b, 4);
    mmo_put_u16le(&b, sizeof grp0);  mmo_put_bytes(&b, grp0, sizeof grp0);
    mmo_put_u16le(&b, sizeof grp12); mmo_put_bytes(&b, grp12, sizeof grp12);
    mmo_put_u16le(&b, sizeof grp12); mmo_put_bytes(&b, grp12, sizeof grp12);
    mmo_put_u16le(&b, 0);            /* the empty fourth group */
    size_t n = b.len;
    memcpy(out, b.data, n);
    mmo_wbuf_free(&b);
    return n;
}

static void test_world_flag_table(void)
{
    printf("world-flag table (server WorldFlagTableReset, pinned decompressed blocks):\n");
    u8 body[256];
    size_t n = build_flag_reset(body);

    u8 blocks[OPENMMO_WORLD_FLAG_GROUPS][OPENMMO_WORLD_FLAG_BLOCK];
    int lens[OPENMMO_WORLD_FLAG_GROUPS];
    int count = -1;
    int rc = mmo_game_read_world_flag_reset(body, n, (u8 *)blocks,
                 OPENMMO_WORLD_FLAG_BLOCK, lens, OPENMMO_WORLD_FLAG_GROUPS, &count);
    CHECK(rc == 0, "the table decodes");
    CHECK(count == 4, "four groups");
    CHECK(lens[0] == 64 && lens[1] == 64 && lens[2] == 64,
          "each stream carries a 64-byte bit array");
    CHECK(lens[3] == 0, "the fourth group is empty");
    CHECK(memcmp(blocks[0], block0 + 2, 64) == 0, "group 0 matches the oracle bits");
    CHECK(memcmp(blocks[1], block12 + 2, 64) == 0, "group 1 matches the oracle bits");
    CHECK(memcmp(blocks[2], block12 + 2, 64) == 0, "group 2 matches the oracle bits");

    /* A block whose parts do not add up is refused rather than read at a stale
     * offset: this stream claims 65 bytes of bit array in a 67-byte block. */
    static const u8 short_block[13] = {
        0x78,0x9c,0x63,0x70,0x64,0xa0,0x14,0x00,0x00,0x11,0x05,0x00,0x42
    };
    mmo_wbuf w;
    mmo_wbuf_init(&w);
    mmo_put_u8(&w, 1);
    mmo_put_u16le(&w, (int)sizeof short_block);
    mmo_put_bytes(&w, short_block, sizeof short_block);
    CHECK(mmo_game_read_world_flag_reset(w.data, w.len, (u8 *)blocks,
              OPENMMO_WORLD_FLAG_BLOCK, lens, OPENMMO_WORLD_FLAG_GROUPS, &count) == -1,
          "a block whose parts do not cross to its end is refused");
    mmo_wbuf_free(&w);
}

static void test_zlib_traps(void)
{
    printf("zlib inflate traps a corrupt stream:\n");
    u8 out[128];
    /* A good stream decodes. */
    CHECK(mmo_inflate_zlib(grp0, sizeof grp0, out, sizeof out) == 67,
          "the intact stream inflates");
    /* Corrupt the Adler-32 trailer. */
    u8 bad[17];
    memcpy(bad, grp0, sizeof grp0);
    bad[16] ^= 0xff;
    CHECK(mmo_inflate_zlib(bad, sizeof bad, out, sizeof out) == (size_t)-1,
          "a bad Adler-32 traps");
    /* Corrupt the header check byte. */
    memcpy(bad, grp0, sizeof grp0);
    bad[1] ^= 0x01;
    CHECK(mmo_inflate_zlib(bad, sizeof bad, out, sizeof out) == (size_t)-1,
          "a bad header checksum traps");
    /* An output cap smaller than the block traps rather than truncates. */
    CHECK(mmo_inflate_zlib(grp0, sizeof grp0, out, 10) == (size_t)-1,
          "an undersized output cap traps");
}

static void test_story_flag_reader(void)
{
    printf("story-flag reader (StoryFlagUpdate: region S8, flagId S16LE, enabled S16LE):\n");
    /* region 0, flag 0x1234, enabled. */
    u8 set_body[5] = { 0x00, 0x34, 0x12, 0x01, 0x00 };
    int region = -1, flag = -1, en = -1;
    CHECK(mmo_game_read_story_flag(set_body, sizeof set_body, &region, &flag, &en) == 0,
          "the set packet decodes");
    CHECK(region == 0 && flag == 0x1234 && en == 1, "region/flag/enabled read back");
    /* enabled = 0 is a clear. */
    u8 clr_body[5] = { 0x00, 0x34, 0x12, 0x00, 0x00 };
    CHECK(mmo_game_read_story_flag(clr_body, sizeof clr_body, NULL, NULL, &en) == 0 && en == 0,
          "a clear reads enabled == 0");
}

/* A minimal LocalPlayerState body carrying two story variables, in the
 * LocalPlayerStatePacketCodec field order, ending in the variable list. */
static size_t build_local_player_state(u8 *out)
{
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    mmo_put_u8(&b, 0);            /* region */
    mmo_put_u16le(&b, 3);         /* mapId */
    mmo_put_u32le(&b, 0);         /* moveSpeed F32 */
    mmo_put_u16le(&b, 6);         /* x */
    mmo_put_u16le(&b, 8);         /* y */
    mmo_put_u16le(&b, 0);         /* z */
    mmo_put_u32le(&b, 100);       /* money */
    mmo_put_u8(&b, 0);            /* gender */
    mmo_put_u16le(&b, 0);         /* skinTone */
    mmo_put_u16le(&b, 0);         /* hairColor */
    mmo_put_u32le(&b, 0);         /* playtime F64 low */
    mmo_put_u32le(&b, 0);         /* playtime F64 high */
    mmo_put_u8(&b, 0);            /* packed flags */
    mmo_put_u16le(&b, 0);         /* partyDex: empty */
    mmo_put_u8(&b, 0);            /* partyForms: empty */
    mmo_put_u16le(&b, 0);         /* pokedexSeen: empty */
    mmo_put_u16le(&b, 0);         /* pokedexCaught: empty */
    mmo_put_u8(&b, 0);            /* badges: empty */
    mmo_put_u16le(&b, 2);         /* variables: 2 entries, (key S16LE, value S8) */
    mmo_put_s16le(&b, 0x05); mmo_put_u8(&b, 42);    /* var 0x4005 = 42 */
    mmo_put_s16le(&b, 0x10); mmo_put_u8(&b, (u8)-7); /* var 0x4010 = -7 */
    size_t n = b.len;
    memcpy(out, b.data, n);
    mmo_wbuf_free(&b);
    return n;
}

static void test_local_player_vars(void)
{
    printf("LocalPlayerState story variables:\n");
    u8 body[128];
    size_t n = build_local_player_state(body);
    mmo_local_player_state ps;
    CHECK(mmo_game_read_local_player_state(body, n, &ps) == 0, "the packet decodes");
    CHECK(ps.var_count == 2 && ps.var_stored == 2, "both variables read");
    CHECK(ps.var_key[0] == 0x05 && ps.var_val[0] == 42, "var 0x4005 = 42");
    CHECK(ps.var_key[1] == 0x10 && ps.var_val[1] == -7, "var 0x4010 = -7");
}

static void test_store_queries(void)
{
    printf("store query API:\n");
    openmmo_story_store s;
    memset(&s, 0, sizeof s);
    s.region = 0;

    /* Seat the world-flag table from the decoded oracle blocks' bit arrays. */
    memcpy(s.flag_block[0], block0 + 2, 64);
    s.flag_block_len[0] = 64;
    s.flag_group_count = 1;
    /* The bit array's last three bytes are 0x80,0x24,0x05, so the set bits are
     * 495, 498, 501, 504 and 506, nothing in the low bits at all. */
    CHECK(openmmo_world_flag_bit(&s, 0, 495) == 1, "world flag bit 495 is set");
    CHECK(openmmo_world_flag_bit(&s, 0, 498) == 1, "bit 498 is set");
    CHECK(openmmo_world_flag_bit(&s, 0, 506) == 1, "bit 506 is set");
    CHECK(openmmo_world_flag_bit(&s, 0, 14) == 0,
          "bit 14 is clear, the old reader read the length prefix as flags");
    CHECK(openmmo_world_flag_bit(&s, 0, 496) == 0, "bit 496 is clear");
    CHECK(openmmo_world_flag_bit(&s, 0, 64 * 8) == -1, "a bit past the block traps");
    CHECK(openmmo_world_flag_bit(&s, 3, 0) == -1, "a group past the table traps");

    /* Story flags: set two, then query. */
    s.set_flag[0] = 0x0100; s.set_flag[1] = 0x0207; s.set_flag_count = 2;
    CHECK(openmmo_story_flag_is_set(&s, 0x0100) == 1, "flag 0x0100 set");
    CHECK(openmmo_story_flag_is_set(&s, 0x0207) == 1, "flag 0x0207 set");
    CHECK(openmmo_story_flag_is_set(&s, 0x0999) == 0, "an unset flag reads 0");

    /* Story vars: present and absent, and both id forms accepted. */
    s.var[0x05] = 42; s.var_present[0x05] = 1; s.var_count = 1;
    s16 v = 0;
    CHECK(openmmo_story_var_get(&s, 0x4005, &v) == 1 && v == 42, "var by GBA id 0x4005");
    v = 0;
    CHECK(openmmo_story_var_get(&s, 0x05, &v) == 1 && v == 42, "var by offset 0x05");
    CHECK(openmmo_story_var_get(&s, 0x4006, &v) == 0, "an absent var reads 0");
}

/*
 * The other half of the store: the local script VM's own flags and vars (0xE5). These are
 * Platinum's id space, not the GBA one above, and unlike every other progression surface here
 * the client is the one that writes them.
 */
static void test_script_state_roundtrip(void)
{
    const mmo_script_flag wf[3] = {
        { 0, 1 }, { 2408, 1 }, { MMO_SCRIPT_FLAG_MAX - 1, 0 },
    };
    const mmo_script_var wv[2] = {
        { MMO_SCRIPT_VAR_BASE, 7 },
        { MMO_SCRIPT_VAR_BASE + MMO_SCRIPT_VAR_MAX - 1, 0xffff },
    };
    mmo_wbuf w;
    mmo_wbuf_init(&w);
    CHECK(mmo_game_write_script_state(&w, wf, 3, wv, 2, NULL, 0) == 0, "ScriptState frames");
    /* 2 + 3*3 + 2 + 2*4 = 21 bytes, and the counts lead each list. */
    /* 2 + 3*3 + 2 + 2*4 + 1 = 22 bytes: the counts lead each list, and the
     * block list's own count is the last byte. */
    CHECK(w.len == 22, "ScriptState body is the declared length");
    CHECK(w.data[0] == 3 && w.data[1] == 0, "flag count leads little-endian");

    mmo_script_flag rf[8];
    mmo_script_var rv[8];
    int nf = -1, fs = -1, nv = -1, vs = -1;
    int nb = -1, bs = -1;
    CHECK(mmo_game_read_script_state(w.data, w.len, rf, 8, &nf, &fs,
                                     rv, 8, &nv, &vs, NULL, 0, &nb, &bs) == 0,
          "ScriptState decodes");
    CHECK(nb == 0 && bs == 0, "a frame carrying no save block says so");
    CHECK(nf == 3 && fs == 3 && nv == 2 && vs == 2, "every row survives");
    CHECK(rf[1].id == 2408 && rf[1].on == 1, "a set flag round-trips");
    CHECK(rf[2].id == MMO_SCRIPT_FLAG_MAX - 1 && rf[2].on == 0,
          "a cleared flag round-trips");
    CHECK(rv[0].id == MMO_SCRIPT_VAR_BASE && rv[0].value == 7,
          "a var round-trips un-rebased");
    CHECK(rv[1].value == 0xffff, "a var keeps its full 16 bits");
    mmo_wbuf_free(&w);
}

/* A reader that trusts the count would walk off a truncated body, and a caller
 * with less room than the wire brought must still learn how much it missed. */
static void test_script_state_traps(void)
{
    const u8 truncated[] = { 0x02, 0x00, 0x01, 0x00, 0x01 }; /* says 2, carries 1 */
    int nf = 0, fs = 0, nv = 0, vs = 0, nb = 0, bs = 0;
    CHECK(mmo_game_read_script_state(truncated, sizeof truncated, NULL, 0,
                                     &nf, &fs, NULL, 0, &nv, &vs,
                                     NULL, 0, &nb, &bs) == -1,
          "a body that ends inside its flag list is refused");

    const u8 no_vars[] = { 0x00, 0x00 }; /* flag count only: vars never start */
    CHECK(mmo_game_read_script_state(no_vars, sizeof no_vars, NULL, 0,
                                     &nf, &fs, NULL, 0, &nv, &vs,
                                     NULL, 0, &nb, &bs) == -1,
          "a body that ends before the var count is refused");

    /* Four bytes and no block count: the shape this op had before it carried
     * save blocks. It is a seat with no blocks, not a malformed frame. */
    const u8 empty[] = { 0x00, 0x00, 0x00, 0x00 };
    CHECK(mmo_game_read_script_state(empty, sizeof empty, NULL, 0,
                                     &nf, &fs, NULL, 0, &nv, &vs,
                                     NULL, 0, &nb, &bs) == 0
              && nf == 0 && nv == 0 && nb == 0,
          "an empty seat is a seat, not an error");

    mmo_wbuf w;
    mmo_wbuf_init(&w);
    const mmo_script_flag wf[3] = { { 1, 1 }, { 2, 1 }, { 3, 1 } };
    (void)mmo_game_write_script_state(&w, wf, 3, NULL, 0, NULL, 0);
    mmo_script_flag one[1];
    CHECK(mmo_game_read_script_state(w.data, w.len, one, 1, &nf, &fs,
                                     NULL, 0, &nv, &vs, NULL, 0, &nb, &bs) == 0
              && nf == 3 && fs == 1,
          "a short buffer keeps one row and still reports three");
    mmo_wbuf_free(&w);

    CHECK(mmo_game_write_script_state(&w, NULL, 1, NULL, 0, NULL, 0) == -1,
          "a count with no rows behind it is refused");
}

/* The save blocks the same op carries: whole, opaque, and keyed by the engine's
 * own save table id. Nothing here reads inside one, the point of the list is
 * that neither end has to. */
static void test_script_state_save_blocks(void)
{
    static const u8 fashion[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };
    static const u8 poffins[] = { 0x11, 0x22 };
    const mmo_save_block wb[2] = {
        { 14, (int)sizeof fashion, fashion },   /* IMAGE_CLIPS */
        { 16, (int)sizeof poffins, poffins },   /* POFFINS */
    };
    mmo_save_block rb[4];
    int nf = 0, fs = 0, nv = 0, vs = 0, nb = 0, bs = 0;
    mmo_wbuf w;

    mmo_wbuf_init(&w);
    CHECK(mmo_game_write_script_state(&w, NULL, 0, NULL, 0, wb, 2) == 0,
          "a frame of nothing but save blocks frames");
    /* 2 + 2 + 1 + (1 + 2 + 6) + (1 + 2 + 2) = 19 */
    CHECK(w.len == 19, "the block list is a count, then id/length/bytes each");
    CHECK(mmo_game_read_script_state(w.data, w.len, NULL, 0, &nf, &fs,
                                     NULL, 0, &nv, &vs, rb, 4, &nb, &bs) == 0
              && nb == 2 && bs == 2,
          "both blocks decode");
    CHECK(rb[0].id == 14 && rb[0].len == (int)sizeof fashion
              && memcmp(rb[0].data, fashion, sizeof fashion) == 0,
          "a block's bytes cross unchanged");
    CHECK(rb[1].id == 16 && rb[1].len == 2, "and so does the one after it");
    mmo_wbuf_free(&w);

    /* A caller with less room than the wire brought must still learn how much
     * it missed, the same way the flag list works. */
    mmo_wbuf_init(&w);
    (void)mmo_game_write_script_state(&w, NULL, 0, NULL, 0, wb, 2);
    CHECK(mmo_game_read_script_state(w.data, w.len, NULL, 0, &nf, &fs,
                                     NULL, 0, &nv, &vs, rb, 1, &nb, &bs) == 0
              && nb == 2 && bs == 1,
          "a short block buffer keeps one and still reports two");
    mmo_wbuf_free(&w);

    /* A block list whose last entry runs off the end of the body is refused:
     * the frame is not what it says it is, and a seat built from half of it
     * would write half a save block. */
    static const u8 lying[] = { 0x00, 0x00, 0x00, 0x00, 0x01, 0x0E, 0x08, 0x00, 0x01 };
    CHECK(mmo_game_read_script_state(lying, sizeof lying, NULL, 0, &nf, &fs,
                                     NULL, 0, &nv, &vs, rb, 4, &nb, &bs) == -1,
          "a block that claims more bytes than the body holds is refused");

    /* A full frame keeps every block. */
    {
        mmo_save_block full[MMO_SAVE_BLOCK_MAX];
        mmo_save_block over[MMO_SAVE_BLOCK_MAX + 1];
        mmo_save_block rfull[MMO_SAVE_BLOCK_MAX];
        int i;

        for (i = 0; i < MMO_SAVE_BLOCK_MAX; i++) {
            full[i].id = i + 1;
            full[i].len = 2;
            full[i].data = poffins;
            over[i] = full[i];
        }
        over[MMO_SAVE_BLOCK_MAX] = full[0];
        mmo_wbuf_init(&w);
        CHECK(mmo_game_write_script_state(&w, NULL, 0, NULL, 0, full,
                                          MMO_SAVE_BLOCK_MAX) == 0,
              "a frame of the most blocks the wire carries frames");
        CHECK(mmo_game_read_script_state(w.data, w.len, NULL, 0, &nf, &fs,
                                         NULL, 0, &nv, &vs, rfull,
                                         MMO_SAVE_BLOCK_MAX, &nb, &bs) == 0
                  && nb == MMO_SAVE_BLOCK_MAX && bs == MMO_SAVE_BLOCK_MAX,
              "and every one of them is kept, none dropped");
        mmo_wbuf_free(&w);
        mmo_wbuf_init(&w);
        CHECK(mmo_game_write_script_state(&w, NULL, 0, NULL, 0, over,
                                          MMO_SAVE_BLOCK_MAX + 1) == -1,
              "one more than that is refused before it is framed");
        mmo_wbuf_free(&w);
    }

    /* The writer refuses what the reader would have to: a block past the wire's
     * own width, and a count with no rows behind it. */
    const mmo_save_block wide = { 14, MMO_SAVE_BLOCK_BYTES + 1, fashion };
    mmo_wbuf_init(&w);
    CHECK(mmo_game_write_script_state(&w, NULL, 0, NULL, 0, &wide, 1) == -1,
          "a block past the wire's width is refused before it is framed");
    CHECK(mmo_game_write_script_state(&w, NULL, 0, NULL, 0, NULL, 1) == -1,
          "a block count with no blocks behind it is refused");
    mmo_wbuf_free(&w);
}

int story_tests_run(void)
{
    failures = 0;
    test_world_flag_table();
    test_zlib_traps();
    test_story_flag_reader();
    test_local_player_vars();
    test_store_queries();
    test_script_state_roundtrip();
    test_script_state_traps();
    test_script_state_save_blocks();
    if (failures)
        printf("story: %d check(s) FAILED\n", failures);
    else
        printf("story: all checks passed\n");
    return failures;
}
