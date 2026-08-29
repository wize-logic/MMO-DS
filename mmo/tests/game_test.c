/*
 * The game transport layer: JoinPacket/JoinResponse codecs and the compression
 * framing over the session envelope.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "codec.h"
#include "crypto.h"
#include "idmap.h"

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

/* JoinPacket body for userId 0x01020304 and token 00..1f, derived from the
 * server's JoinPacketCodec (choose/fixedBytes/listPrefixed field order). */
static const u8 join_vec[96] = {
    0,4,3,2,1,32,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,
    23,24,25,26,27,28,29,30,31,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static void test_join_encode(void)
{
    printf("JoinPacket body (server codec field order):\n");
    u8 token[32];
    for (int i = 0; i < 32; i++)
        token[i] = (u8)i;

    mmo_wbuf b;
    mmo_wbuf_init(&b);
    mmo_game_write_join(&b, 0x01020304, token, sizeof token);
    CHECK(!b.err, "encoder reported no error");
    CHECK(b.len == sizeof join_vec, "encoded length is 96 bytes");
    CHECK(b.len == sizeof join_vec && memcmp(b.data, join_vec, sizeof join_vec) == 0,
          "encoded JoinPacket matches the derived vector");
    mmo_wbuf_free(&b);
}

/* Build two size-2 (CRC-16) crypto contexts sharing one AES-CTR key/IV: one to
 * encipher (a stream endpoint's outbound) and one to decipher its mirror. */
static void matched_crypto(mmo_session_crypto *enc, mmo_session_crypto *dec)
{
    static const u8 key[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    static const u8 iv[16]  = { 0 };
    memset(enc, 0, sizeof *enc);
    memset(dec, 0, sizeof *dec);
    enc->checksum_size = 2;
    dec->checksum_size = 2;
    mmo_aes128ctr_init(&enc->enc, key, iv);
    mmo_aes128ctr_init(&dec->dec, key, iv);
}

/* Pull the single frame out of `frame` and hand its body to mmo_game_recv. */
static int recv_one(mmo_session_crypto *rcv, mmo_game_stream *gr, mmo_wbuf *frame,
                    u8 *op, u8 *out, size_t cap, size_t *outlen)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, frame->data, frame->len);
    const u8 *payload;
    size_t plen;
    if (mmo_frame_get(&r, &payload, &plen) != MMO_FRAME_OK)
        return -1;
    size_t on = mmo_game_recv(rcv, gr, payload, plen, op, out, cap);
    if (on == (size_t)-1)
        return -1;
    *outlen = on;
    return 0;
}

static void test_send_is_uncompressed(void)
{
    printf("game send is uncompressed (no flag byte):\n");
    mmo_session_crypto snd, dec;
    matched_crypto(&snd, &dec);

    u8 body[40];
    for (int i = 0; i < 40; i++) body[i] = (u8)(i * 3 + 1);
    mmo_wbuf frame;
    mmo_wbuf_init(&frame);
    mmo_game_send(&snd, 0x06, body, sizeof body, &frame);
    CHECK(!frame.err, "send produced a frame");

    /* Decipher the envelope directly: the plaintext must be opcode||body with no
     * inserted compression flag (that is what the server's decoder-less inbound
     * path expects). */
    mmo_rbuf r;
    mmo_rbuf_init(&r, frame.data, frame.len);
    const u8 *payload;
    size_t plen;
    u8 plain[64];
    size_t pn = 0;
    if (mmo_frame_get(&r, &payload, &plen) == MMO_FRAME_OK)
        pn = mmo_session_recv_app(&dec, payload, plen, plain, sizeof plain);
    CHECK(pn == sizeof body + 1 && plain[0] == 0x06 &&
          memcmp(plain + 1, body, sizeof body) == 0,
          "plaintext is opcode||body, no flag");
    mmo_wbuf_free(&frame);
}

/* Emulate the server's outbound: encipher opcode||flag||segment over the CRC
 * envelope, so mmo_game_recv can unwrap it as an inbound game frame. */
static void test_recv_decompresses(void)
{
    printf("game recv unwraps the S->C compression framing:\n");
    mmo_session_crypto srv, cli;
    mmo_game_stream gr;

    /* flag 0: a small raw body. */
    matched_crypto(&srv, &cli);
    mmo_game_stream_init(&gr);
    u8 small[40];
    for (int i = 0; i < 40; i++) small[i] = (u8)(i * 7 + 2);
    u8 comp0[64];
    comp0[0] = 0x00;                              /* flag 0 */
    memcpy(comp0 + 1, small, sizeof small);
    mmo_wbuf f0;
    mmo_wbuf_init(&f0);
    mmo_session_send_app(&srv, 0x05, comp0, sizeof small + 1, &f0);
    u8 op, out[2048];
    size_t on = 0;
    CHECK(recv_one(&cli, &gr, &f0, &op, out, sizeof out, &on) == 0 &&
          op == 0x05 && on == sizeof small && memcmp(out, small, on) == 0,
          "flag 0 (raw) inbound packet unwraps");
    mmo_wbuf_free(&f0);

    /* flag 1: a compressed body, a real deflated segment the server would emit. */
    matched_crypto(&srv, &cli);
    mmo_game_stream_init(&gr);
    u8 big[900];
    for (int i = 0; i < 900; i++) big[i] = (u8)(i * 5 + 7);
    mmo_deflate d;
    mmo_deflate_init(&d);
    u8 seg[1024];
    size_t seglen = mmo_deflate_segment(&d, big, sizeof big, seg, sizeof seg);
    u8 comp1[1200];
    comp1[0] = 0x01;                              /* flag 1 */
    memcpy(comp1 + 1, seg, seglen);
    mmo_wbuf f1;
    mmo_wbuf_init(&f1);
    mmo_session_send_app(&srv, 0x42, comp1, seglen + 1, &f1);
    on = 0;
    CHECK(recv_one(&cli, &gr, &f1, &op, out, sizeof out, &on) == 0 &&
          op == 0x42 && on == sizeof big && memcmp(out, big, on) == 0,
          "flag 1 (compressed) inbound packet inflates");
    mmo_wbuf_free(&f1);
}

static void test_join_response(void)
{
    printf("JoinResponse decode (server accept/reject bodies):\n");
    /* accept: canJoin=1, ""(00 00), S8 pad, playtime, rewardPoints, balance,
     * dayStart, now. The server sends playtime=1337 rewardPoints=420 balance=187. */
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    mmo_put_bool(&b, 1);
    mmo_put_utf16_nt(&b, "");
    mmo_put_u8(&b, 0);
    mmo_put_s32le(&b, 1337);
    mmo_put_s32le(&b, 420);
    mmo_put_s32le(&b, 187);
    mmo_put_s32le(&b, 111);
    mmo_put_s32le(&b, 222);

    mmo_join_response jr;
    int rc = mmo_game_read_join_response(b.data, b.len, &jr);
    CHECK(rc == 0 && jr.can_join, "accept body decodes to canJoin");
    CHECK(jr.playtime == 1337 && jr.reward_points == 420 && jr.balance == 187,
          "accept body carries playtime/rewardPoints/balance");
    mmo_wbuf_free(&b);

    u8 reject[1] = { 0 };
    rc = mmo_game_read_join_response(reject, 1, &jr);
    CHECK(rc == 0 && !jr.can_join, "reject body decodes to !canJoin");
}

static void test_select_character_encode(void)
{
    printf("SelectCharacter body (id then hash, both S64LE):\n");
    /* Server codec: field(S64LE characterId), field(S64LE characterIdHash). */
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    mmo_game_write_select_character(&b, 0x0102030405060708LL, 0);
    static const u8 want[16] = {
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, /* id, little-endian */
        0, 0, 0, 0, 0, 0, 0, 0,                         /* hash 0 (server ignores it) */
    };
    CHECK(!b.err && b.len == sizeof want && memcmp(b.data, want, sizeof want) == 0,
          "encoded SelectCharacter matches id||hash S64LE");
    mmo_wbuf_free(&b);
}

static void test_first_character_id(void)
{
    printf("CharactersList first-character id extraction:\n");
    /* A U8 count then each entry's CharacterInfo, whose leading field is the
     * S64LE id. Only the count and first id are needed to drive selection. */
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    mmo_put_u8(&b, 2);                              /* two characters */
    mmo_put_s64le(&b, 2305368665433149440LL);       /* first id */
    mmo_put_bytes(&b, (const u8 *)"\x00\x00", 2);   /* start of its name, ignored */
    s64 id = 0;
    int count = mmo_game_read_first_character_id(b.data, b.len, &id);
    CHECK(count == 2 && id == 2305368665433149440LL,
          "count and first id are read from a populated list");
    mmo_wbuf_free(&b);

    u8 empty = 0;                                  /* count 0: nothing to select */
    id = -1;
    count = mmo_game_read_first_character_id(&empty, 1, &id);
    CHECK(count == 0, "an empty list reports count 0");

    count = mmo_game_read_first_character_id(&empty, 0, &id);
    CHECK(count == -1, "a truncated body is rejected");
}

static void test_create_character_encode(void)
{
    printf("CreateCharacter body (name, gender, region, SkinSet):\n");

    mmo_create_character in;
    memset(&in, 0, sizeof in);
    in.name = "Lucas";
    in.gender = 0;
    in.starting_region = 3;
    in.appearance.region_selection_index = 3;

    mmo_wbuf b;
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_create_character(&b, &in) == 0, "an empty wardrobe encodes");
    /* "Lucas" UTF-16LE + NUL (12) + gender 0 + region 3 + skin region 3 + mask 0 */
    CHECK(b.len == 12 + 1 + 1 + 1 + 2, "empty SkinSet is region + U16LE mask 0");
    CHECK(b.data[12] == 0 && b.data[13] == 3 && b.data[14] == 3 &&
              b.data[15] == 0 && b.data[16] == 0,
          "gender, starting region, wardrobe region and empty mask follow the name");
    mmo_wbuf_free(&b);

    in.name = "";
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_create_character(&b, &in) == -1, "an empty name is refused");
    mmo_wbuf_free(&b);

    in.name = "Lucas";
    in.gender = 2;
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_create_character(&b, &in) == -1, "a gender other than 0/1 is refused");
    mmo_wbuf_free(&b);

    in.gender = 0;
    in.appearance.slot[0].present = 1;
    in.appearance.slot[0].type = 0x400;
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_create_character(&b, &in) == -1, "a type past 10 bits is refused");
    mmo_wbuf_free(&b);

    mmo_character_ref ref;
    u8 named[] = {
        1,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        'A', 0, 'b', 0, 'a', 0, 0, 0,
        0, 0,
        0, 0, 0, 0,
        1,
    };
    CHECK(mmo_game_read_first_character(named, sizeof named, &ref) == 0,
          "a one-entry list with a name and gender decodes");
    CHECK(ref.count == 1 && ref.id == 0x0102030405060708LL, "count and id");
    CHECK(strcmp(ref.name, "Aba") == 0 && ref.gender == 1, "name and rivalSex");
}

static void test_pick_character(void)
{
    mmo_character_list list;
    char err[80];

    printf("character pick by name, index and region:\n");
    memset(&list, 0, sizeof list);
    list.count = 2;
    list.held = 2;
    list.entry[0].id = 1;
    snprintf(list.entry[0].name, sizeof list.entry[0].name, "Lucas");
    list.entry[0].gender = 0;
    list.entry[0].region = 3;
    list.entry[1].id = 2;
    snprintf(list.entry[1].name, sizeof list.entry[1].name, "Dawn");
    list.entry[1].gender = 1;
    list.entry[1].region = 3;

    CHECK(mmo_game_pick_character(&list, NULL, -1, -1, err, sizeof err) == 0,
          "no selector takes the first row");
    CHECK(mmo_game_pick_character(&list, "dawn", -1, -1, err, sizeof err) == 1,
          "a name match is case-insensitive");
    CHECK(mmo_game_pick_character(&list, NULL, 1, -1, err, sizeof err) == 1,
          "index 1 is the second row");
    CHECK(mmo_game_pick_character(&list, "Missing", -1, -1, err, sizeof err) == -1 &&
              strstr(err, "Missing") != NULL,
          "a missing name is refused by name");
    CHECK(mmo_game_pick_character(&list, NULL, 4, -1, err, sizeof err) == -1 &&
              strstr(err, "past") != NULL,
          "an index past the list is refused");
    CHECK(mmo_game_pick_character(&list, NULL, -1, 3, err, sizeof err) == -1 &&
              strstr(err, "2 characters") != NULL,
          "two characters in one region are refused rather than guessed");
    CHECK(mmo_game_pick_character(&list, NULL, -1, 0, err, sizeof err) == -1 &&
              strstr(err, "no character") != NULL,
          "a region with nobody is refused");
    CHECK(mmo_game_pick_character(&list, "Lucas", 0, -1, err, sizeof err) == -1 &&
              strstr(err, "one of") != NULL,
          "two selectors at once are refused");

    list.entry[1].region = 0;
    CHECK(mmo_game_pick_character(&list, NULL, -1, 0, err, sizeof err) == 1,
          "a unique region match is that row");

    memset(&list, 0, sizeof list);
    CHECK(mmo_game_pick_character(&list, NULL, -1, -1, err, sizeof err) == -1,
          "an empty list is refused");
}

static void test_selected_character(void)
{
    mmo_character ch;
    u8 absent = 0;
    u8 truncated = 1;

    printf("SelectedCharacter (s2c 0x04) optional CharacterInfo:\n");
    CHECK(mmo_game_read_selected_character(&absent, 1, &ch) == 0,
          "an absent optional decodes");
    CHECK(ch.id == 0 && ch.name[0] == '\0' && ch.gender == -1,
          "absent means no id, no name, gender unknown");
    CHECK(mmo_game_read_selected_character(&truncated, 1, &ch) == -1,
          "present with no CharacterInfo is truncated");
    CHECK(mmo_game_read_selected_character(&absent, 0, &ch) == -1,
          "an empty body is rejected");
}

/* MovementPacket body, pinned to the server's own MovementPacketCodec vector
 * (GameCodecRoundtripTest): MovementPacket(x=100, y=250, UP, running=true)
 * encodes to 64 00 FA 00 81. The engine facing that maps to server UP is NORTH,
 * so feeding NORTH through mmo_game_dir_from_ds must reproduce that byte. */
static void test_movement_encode(void)
{
    printf("MovementPacket body (server MovementPacketCodec vector):\n");

    CHECK(mmo_game_dir_from_ds(0) == 1, "NORTH maps to server UP");
    CHECK(mmo_game_dir_from_ds(1) == 0, "SOUTH maps to server DOWN");
    CHECK(mmo_game_dir_from_ds(2) == 2, "WEST maps to server LEFT");
    CHECK(mmo_game_dir_from_ds(3) == 3, "EAST maps to server RIGHT");
    CHECK(mmo_game_dir_from_ds(99) == 0, "an out-of-range facing falls back to DOWN");

    static const u8 up_run[5] = { 0x64, 0x00, 0xFA, 0x00, 0x81 };
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    mmo_game_write_movement(&b, 100, 250, mmo_game_dir_from_ds(0), 1);
    CHECK(!b.err, "encoder reported no error");
    CHECK(b.len == sizeof up_run, "encoded length is 5 bytes");
    CHECK(b.len == sizeof up_run && memcmp(b.data, up_run, sizeof up_run) == 0,
          "NORTH+running matches the server's UP+running vector");
    mmo_wbuf_free(&b);

    /* A plain walk south clears the running bit; state = DOWN(0). */
    static const u8 down_walk[5] = { 0x64, 0x00, 0xFA, 0x00, 0x00 };
    mmo_wbuf_init(&b);
    mmo_game_write_movement(&b, 100, 250, mmo_game_dir_from_ds(1), 0);
    CHECK(b.len == sizeof down_walk && memcmp(b.data, down_walk, sizeof down_walk) == 0,
          "SOUTH walking clears the running bit and packs DOWN");
    mmo_wbuf_free(&b);

    /* Negative from-tile coordinates are S16LE two's complement. */
    static const u8 neg[5] = { 0xFF, 0xFF, 0x00, 0x00, 0x03 };
    mmo_wbuf_init(&b);
    mmo_game_write_movement(&b, -1, 0, mmo_game_dir_from_ds(3), 0);
    CHECK(b.len == sizeof neg && memcmp(b.data, neg, sizeof neg) == 0,
          "a negative x packs as S16LE and EAST maps to RIGHT");
    mmo_wbuf_free(&b);
}

/* FaceDirectionPacket body: one Direction ordinal. The the official client's
 * the official client writes that single byte; the server's FaceDirectionPacketCodec
 * is enumByOrdinalByte, DOWN=0 UP=1 LEFT=2 RIGHT=3. */
static void test_face_encode(void)
{
    printf("FaceDirectionPacket body (the official client the official client):\n");

    static const struct { int ds; u8 wire; const char *name; } cases[] = {
        { 0, 1, "NORTH faces as server UP" },
        { 1, 0, "SOUTH faces as server DOWN" },
        { 2, 2, "WEST faces as server LEFT" },
        { 3, 3, "EAST faces as server RIGHT" },
    };
    int i;

    for (i = 0; i < 4; i++) {
        mmo_wbuf b;
        mmo_wbuf_init(&b);
        mmo_game_write_face(&b, mmo_game_dir_from_ds(cases[i].ds));
        CHECK(!b.err && b.len == 1 && b.data[0] == cases[i].wire, cases[i].name);
        mmo_wbuf_free(&b);
    }
}

/*
 * The inbound presence stream (LoadEntity / GbaEntityMove / EntityMove / EntityFaceTurn /
 * EntityLeave / EntityDespawn), and the server-Direction -> engine-DIR_* mapping that every
 * one of them feeds through.
 */
static void test_presence_decode(void)
{
    /* LoadEntity: S64 id, gender U8, SkinSet(regionU8, maskU16, per-slot U16),
     * UTF-16LE name, region/bank/map U8, x/y S16, z U8, heading U8, statusU8,
     * stateU8, flags U8, conditional trailer. Here a female peer, mask 0 (no skin
     * words), name "Ash", flags 0 (no follower). */
    static const u8 load1[] = {
        0x42,0x00,0x00,0x00,0x01,0x00,0x00,0x00, /* id = 0x100000042 -> low32 66 */
        0x01,                                     /* gender = female */
        0x03, 0x00,0x00,                          /* skin: region 3, mask 0 */
        0x41,0x00, 0x73,0x00, 0x68,0x00, 0x00,0x00, /* name "Ash" */
        0x01, 0x03, 0x05,                         /* region 1, bank 3, map 5 */
        0x64,0x00,                                /* x = 100 */
        0xFA,0x00,                                /* y = 250 */
        0x07,                                     /* z = 7 */
        0x01,                                     /* heading = UP, 0x08 clear */
        0x00,                                     /* status flags */
        0x00,                                     /* entity state */
        0x00,                                     /* flags = 0 (no follower) */
    };
    mmo_load_entity le;
    CHECK(mmo_game_read_load_entity(load1, sizeof load1, &le) == 0, "LoadEntity decodes");
    CHECK(le.entity_id == 66, "LoadEntity id is the low 32 bits");
    CHECK(le.gender == 1, "the byte after the id is the peer's gender");
    CHECK(strcmp(le.name, "Ash") == 0, "LoadEntity name is the UTF-16 string");
    CHECK(le.region_id == 1 && le.bank_id == 3 && le.map_id == 5, "LoadEntity region/bank/map");
    CHECK(le.x == 100 && le.y == 250 && le.z == 7, "LoadEntity tile x/y/z");
    CHECK(le.facing == 1 && !le.facing_flag, "LoadEntity heading is the raw server ordinal (UP)");
    CHECK(le.skin_region == 3 && le.skin_count == 0, "LoadEntity skin region/count (empty skin)");
    CHECK(le.appearance.region_selection_index == 3 &&
              !le.appearance.slot[0].present,
        "an empty SkinSet is kept, not discarded");
    CHECK(le.transportation == 0 && le.entity_state == 0, "LoadEntity mount and state default to nothing");
    CHECK(!le.has_follower && le.follower_dex == 0, "LoadEntity no follower");
    CHECK(le.name_prefix[0] == '\0', "no prefix without the 0x10 flag");

    /* Two populated skin slots whose mask asks for per-slot overrides (bit 15), a
     * heading byte carrying the 0x08 flag beside a direction, and a follower. The
     * override bytes are the part a reader that only knows the words loses: miss
     * them and the name starts two bytes early. */
    static const u8 load2[] = {
        0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* id 7 */
        0x00,                                     /* gender = male */
        0x00, 0x03,0x80,                          /* skin: region 0, mask 0x8003 */
        0xAA,0xBB, 0x11,                          /* slot 0: word + override */
        0xCC,0xDD, 0x22,                          /* slot 1: word + override */
        0x00,0x00,                                /* name "" */
        0x02, 0x04, 0x06,                         /* region 2, bank 4, map 6 */
        0xFF,0xFF,                                /* x = -1 */
        0x2C,0x01,                                /* y = 300 */
        0x00,                                     /* z = 0 */
        0x0B,                                     /* heading = RIGHT with 0x08 set */
        0x05,                                     /* status flags */
        0x02,                                     /* entity state */
        0x04,                                     /* flags = 0x04 (follower) */
        0x97,0x00,                                /* followerDexId = 151 */
    };
    CHECK(mmo_game_read_load_entity(load2, sizeof load2, &le) == 0, "LoadEntity with skin overrides decodes");
    CHECK(le.entity_id == 7 && le.name[0] == '\0', "the skin decode lands on the right fields");
    CHECK(le.x == -1 && le.y == 300, "LoadEntity after the skin block: x/y");
    CHECK(le.facing == 3 && le.facing_flag, "the heading byte splits into a direction and its flag");
    CHECK(le.skin_region == 0 && le.skin_count == 2, "mask bit 15 is a flag, not a thirteenth slot");
    CHECK(le.appearance.slot[0].present && le.appearance.slot[1].present &&
              !le.appearance.slot[2].present,
        "the two populated slots are kept");
    CHECK(le.appearance.slot[0].type == 0x11 && le.appearance.slot[1].type == 0x22,
        "a nonzero override replaces the word's type, as the official reader prefers");
    CHECK(le.transportation == 5 && le.entity_state == 2, "LoadEntity carries the mount byte and the state byte");
    CHECK(le.has_follower && le.follower_dex == 151, "LoadEntity follower dex from the trailer");

    /* A flags byte with every conditional bit set (0x1F): the parser must consume
     * the 0x01/0x02/0x08 padding fields, land the follower id, and read the 0x10
     * tail as a bare string, there is no length or id in front of it. */
    static const u8 load3[] = {
        0x09,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* id 9 */
        0x00,                                     /* gender */
        0x00, 0x00,0x00,                          /* skin: region 0, mask 0 */
        0x00,0x00,                                /* name "" */
        0x00, 0x00, 0x00,                         /* region/bank/map 0 */
        0x00,0x00, 0x00,0x00, 0x00,               /* x 0, y 0, z 0 */
        0x00,                                     /* heading DOWN */
        0x00, 0x00,                               /* status, state */
        0x1F,                                     /* flags: 0x01|0x02|0x04|0x08|0x10 */
        0x11,                                     /* 0x01: S8 pad */
        0x22, 0x33,0x44,                          /* 0x02: S8 + U16 pad */
        0x01,0x00,                                /* 0x04: followerDexId = 1 */
        0x55,                                     /* 0x08: S8 pad */
        0x4F,0x00, 0x4B,0x00, 0x00,0x00,          /* 0x10: name prefix "OK" */
    };
    CHECK(mmo_game_read_load_entity(load3, sizeof load3, &le) == 0, "LoadEntity full flags trailer decodes");
    CHECK(le.has_follower && le.follower_dex == 1, "the follower id survives the padded trailer");
    CHECK(strcmp(le.name_prefix, "OK") == 0, "the 0x10 tail is a bare UTF-16 string");

    /* A truncated LoadEntity is rejected, not silently half-read. */
    CHECK(mmo_game_read_load_entity(load1, 6, &le) == -1, "a short LoadEntity is rejected");

    /* GbaEntityMove: S64 id, bank U8, map U8, x U8, y U8, mode U8, dir U8. */
    static const u8 gba[] = {
        0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x03, 0x05, 0x0A, 0x14, 0x02, 0x02,
    };
    mmo_gba_move gm;
    CHECK(mmo_game_read_gba_move(gba, sizeof gba, &gm) == 0, "GbaMove decodes");
    CHECK(gm.entity_id == 66 && gm.bank_id == 3 && gm.map_id == 5, "GbaMove id/bank/map");
    CHECK(gm.x == 10 && gm.y == 20 && gm.movement_mode == 2 && gm.direction == 2,
          "GbaMove tile/mode/dir");

    /* The step's last byte is packed: heading in the low two bits, a boolean at
     * bit 3, the rest unread. The whole byte is kept beside the heading. */
    static const u8 gba_packed[] = {
        0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x03, 0x05, 0x0A, 0x14, 0x02, 0x0B,
    };
    CHECK(mmo_game_read_gba_move(gba_packed, sizeof gba_packed, &gm) == 0,
          "GbaMove with a packed trailer decodes");
    CHECK(gm.direction == 3 && gm.dir_flags == 0x0B,
          "the heading is the low two bits and the byte is kept whole");

    /* EntityMove: S64 id, x S16, y S16, dir U8. */
    static const u8 mv[] = {
        0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x2C,0x01, 0xFB,0xFF, 0x01,
    };
    mmo_entity_move em;
    CHECK(mmo_game_read_entity_move(mv, sizeof mv, &em) == 0, "EntityMove decodes");
    CHECK(em.entity_id == 66 && em.x == 300 && em.y == -5 && em.direction == 1,
          "EntityMove id/x/y/dir (S16 signed)");

    /* NpcUpdate (0x11): a whole pose at once, S64 id, region/bank/map U8, x/y
     * S16LE, movement mode U8, packed heading byte. */
    static const u8 snap[] = {
        0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x01, 0x04, 0x03, 0x0C,0x00, 0x05,0x00, 0xF6, 0x02,
    };
    mmo_entity_snap sn;
    CHECK(mmo_game_read_entity_snap(snap, sizeof snap, &sn) == 0, "NpcUpdate decodes");
    CHECK(sn.entity_id == 66 && sn.region_id == 1 && sn.bank_id == 4 && sn.map_id == 3,
          "NpcUpdate id and map triple");
    CHECK(sn.x == 12 && sn.y == 5 && sn.movement_mode == 0xF6 && sn.direction == 2,
          "NpcUpdate tile, movement mode and heading");
    CHECK(mmo_game_read_entity_snap(snap, 8, &sn) == -1, "a short NpcUpdate is rejected");

    /* NpcSpawn (0x12): laid out to this server's NpcSpawnPacketCodec. A
     * high-bit entity id, FOLLOW_PLAYER packed into unk3, and a facing
     * that sits after unk5 rather than next to x/y. */
    static const u8 npc[] = {
        0x01,0x00,0x00,0x00,0x00,0x00,0x69,0x1A, /* id 0x1A69000000000001 */
        0x03,                                     /* sprite region */
        0x54,0x00,                                /* graphics 0x54 */
        0x02,0x30,                                /* unk3 = 0x3002 → movement 48 */
        0x00,0x00,                                /* unk4 */
        0x03, 0x01, 0x56,                         /* region 3, bank 1, map 86 */
        0x70,0x00,                                /* x = 112 */
        0x57,0x03,                                /* y = 855 */
        0x02,                                     /* unk5 */
        0x00,                                     /* facing DOWN */
        0x08,0x00,                                /* unk6 */
    };
    mmo_npc_spawn ns;
    CHECK(mmo_game_read_npc_spawn(npc, sizeof npc, &ns) == 0, "NpcSpawn decodes");
    CHECK(ns.entity_id == 0x1A69000000000001LL, "NpcSpawn keeps the whole s64 id");
    CHECK(ns.sprite_region == 3 && ns.graphics_id == 0x54, "NpcSpawn sprite");
    CHECK(ns.unk3 == 0x3002 && ns.movement == MMO_MOVEMENT_FOLLOW_PLAYER,
          "unk3's high byte is this server's movement id");
    CHECK(ns.region_id == 3 && ns.bank_id == 1 && ns.map_id == 86,
          "NpcSpawn region/bank/map");
    CHECK(ns.x == 112 && ns.y == 855 && ns.facing == 0, "NpcSpawn tile and facing");
    CHECK(mmo_game_read_npc_spawn(npc, 8, &ns) == -1, "a short NpcSpawn is rejected");

    {
        s64 id64 = 0;
        CHECK(mmo_game_read_entity_id64(npc, 8, &id64) == 0
                  && id64 == 0x1A69000000000001LL,
              "entity id64 keeps the npc band");
    }

    /* EntityFaceTurn: S64 id, facing S8. */
    static const u8 turn[] = { 0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x03 };
    u32 id = 0; int facing = -1;
    CHECK(mmo_game_read_face_turn(turn, sizeof turn, &id, &facing) == 0, "FaceTurn decodes");
    CHECK(id == 66 && facing == 3, "FaceTurn id and facing");

    /* -1 is the "look at the local player" request, not a heading of 255. */
    static const u8 turn_at_player[] = { 0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0xFF };
    facing = 0;
    CHECK(mmo_game_read_face_turn(turn_at_player, sizeof turn_at_player, &id, &facing) == 0
              && facing == -1,
          "FaceTurn 0xFF reads as the -1 look-at-the-player request");

    /* EntityTransportation (0x28): S64 id then the mount byte. It is the same
     * byte LoadEntity carries, which is why a spawn and a change agree. */
    static const u8 mount[] = { 0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x01 };
    u32 mount_id = 0; int riding = -1;
    CHECK(mmo_game_read_transportation(mount, sizeof mount, &mount_id, &riding) == 0,
          "EntityTransportation decodes");
    CHECK(mount_id == 66 && riding == MMO_TRANSPORT_SURFING,
          "EntityTransportation id and the surfing bit");
    static const u8 ashore[] = { 0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00 };
    CHECK(mmo_game_read_transportation(ashore, sizeof ashore, &mount_id, &riding) == 0
              && riding == MMO_TRANSPORT_NONE,
          "a cleared mount is the same packet with no bits");
    CHECK(mmo_game_read_transportation(mount, 8, &mount_id, &riding) == -1,
          "a short EntityTransportation is rejected");

    /* EntityLeave: S64 id only. */
    static const u8 leave[] = { 0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
    id = 0;
    CHECK(mmo_game_read_entity_leave(leave, sizeof leave, &id) == 0 && id == 66, "EntityLeave id");

    /* EntityDespawn: reserved byte then S16 id. */
    static const u8 despawn[] = { 0x00, 0x42,0x00 };
    id = 0;
    CHECK(mmo_game_read_entity_despawn(despawn, sizeof despawn, &id) == 0 && id == 66,
          "EntityDespawn skips the reserved byte and reads the S16 id");

    /* The server Direction -> engine DIR_* map is the exact inverse of the
     * outbound one, so a facing that round-trips is unchanged. */
    CHECK(mmo_game_dir_to_ds(1) == 0, "server UP maps to engine NORTH");
    CHECK(mmo_game_dir_to_ds(0) == 1, "server DOWN maps to engine SOUTH");
    CHECK(mmo_game_dir_to_ds(2) == 2, "server LEFT maps to engine WEST");
    CHECK(mmo_game_dir_to_ds(3) == 3, "server RIGHT maps to engine EAST");
    CHECK(mmo_game_dir_to_ds(99) == 1, "an out-of-range ordinal falls back to SOUTH");
    for (int d = 0; d < 4; d++)
        CHECK(mmo_game_dir_to_ds(mmo_game_dir_from_ds(d)) == d,
              "dir_to_ds inverts dir_from_ds for every engine facing");
}

/* The world-state block's three seatable packets, pinned to the server codecs
 * (LocalPlayerStatePacketCodec, StoryFlagUpdatePacketCodec, and the
 * BattleSideParty shape itemStacksPacket rides). Vectors are laid out field for
 * field from those codecs. */
static void test_world_state_decode(void)
{
    printf("world-state block decode (server codecs):\n");

    /*
     * LocalPlayerState: region 0, map 1, x 100, y 200, z 0, money 123456, gender 1, a two-mon
     * party (Pikachu 25, Charizard 6), no dex, three badges, one variable.
     */
    static const u8 lps[] = {
        0x00,                               /* region 0 */
        0x01,0x00,                          /* mapId 1 */
        0xCD,0xCC,0x4C,0x3D,                /* moveSpeed 0.05f */
        0x64,0x00,                          /* x 100 */
        0xC8,0x00,                          /* y 200 */
        0x00,0x00,                          /* z 0 */
        0x40,0xE2,0x01,0x00,                /* money 123456 */
        0x01,                               /* gender 1 */
        0x00,0x00,                          /* skinTone */
        0x00,0x00,                          /* hairColor */
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* playtime 0.0 */
        0x00,                               /* flags */
        0x02,0x00,                          /* partyDex count 2 */
        0x19,0x00, 0x06,0x00,               /* 25, 6 */
        0x02, 0x00,0x00,                    /* partyForms count 2, {0,0} */
        0x00,0x00,                          /* pokedexSeen count 0 */
        0x00,0x00,                          /* pokedexCaught count 0 */
        0x03, 0x00,0x00, 0x01,0x00, 0x02,0x00, /* badges count 3 */
        0x01,0x00,                          /* variables count 1 */
        0x05,0x00, 0x2A,                    /* var key 5 (S16LE) = 42 (S8) */
    };
    mmo_local_player_state ps;
    CHECK(mmo_game_read_local_player_state(lps, sizeof lps, &ps) == 0, "LocalPlayerState decodes");
    CHECK(ps.region == 0 && ps.map_id == 1, "LocalPlayerState region/map");
    CHECK(ps.x == 100 && ps.y == 200 && ps.z == 0, "LocalPlayerState tile");
    CHECK(ps.money == 123456 && ps.gender == 1, "LocalPlayerState money/gender");
    CHECK(ps.party_count == 2 && ps.party_total == 2, "LocalPlayerState party count");
    CHECK(ps.party_dex[0] == 25 && ps.party_dex[1] == 6, "LocalPlayerState party species (National Dex)");
    CHECK(ps.badge_count == 3 && ps.var_count == 1, "LocalPlayerState badge/var counts");
    CHECK(mmo_game_read_local_player_state(lps, 8, &ps) == -1, "a truncated LocalPlayerState is rejected");

    /* StoryFlagUpdate: region 0, flag 300, enabled; and a cleared one. */
    static const u8 flag_on[]  = { 0x00, 0x2C,0x01, 0x01,0x00 };
    static const u8 flag_off[] = { 0x00, 0x2C,0x01, 0x00,0x00 };
    int reg = -1, fid = -1, en = -1;
    CHECK(mmo_game_read_story_flag(flag_on, sizeof flag_on, &reg, &fid, &en) == 0, "StoryFlag decodes");
    CHECK(reg == 0 && fid == 300 && en == 1, "StoryFlag region/id/enabled");
    CHECK(mmo_game_read_story_flag(flag_off, sizeof flag_off, NULL, NULL, &en) == 0 && en == 0,
          "StoryFlag cleared reads enabled 0");

    /* ItemStacks (0x40): three bag stacks. The middle one sets slot|partyIndex|
     * status flags so its optional fields are consumed and the next stack still
     * lands on the right bytes. */
    static const u8 items[] = {
        0x01,                               /* side */
        0x01,                               /* replace */
        0x03,0x00,                          /* count 3 */
        /* stack A: flags 0 */
        0x00, 0,0,0,0,0,0,0,0, 0x95,0x13, 0x14,0x00, 0x01, /* Potion 5013 x20 */
        /* stack B: flags 0x1C (slot|partyIndex|status) */
        0x1C, 0,0,0,0,0,0,0,0, 0x8C,0x13, 0x05,0x00, 0x01, /* Poke Ball 5004 x5 */
        0x07,                               /* slot (0x04) */
        0x02,                               /* partyIndex (0x08) */
        0x0A,0x00,0x00,0x00, 0x01, 0x02,    /* status (0x10): S32 + S8 + S8 */
        /* stack C: flags 0 */
        0x00, 0,0,0,0,0,0,0,0, 0x89,0x13, 0x01,0x00, 0x01, /* Master Ball 5001 x1 */
    };
    mmo_item_stack st[8];
    int total = -1;
    int replace = -1;
    int got = mmo_game_read_item_stacks(items, sizeof items, st, 8, &total, &replace);
    CHECK(got == 3 && total == 3, "ItemStacks reads all three stacks");
    CHECK(replace == 1, "ItemStacks replace flag is set");
    CHECK(st[0].object_id == 0 && st[0].item_id == 5013 && st[0].quantity == 20,
          "ItemStacks stack A (wire 5013 x20)");
    CHECK(st[1].item_id == 5004 && st[1].quantity == 5, "ItemStacks stack B lands past the flag fields");
    CHECK(st[2].item_id == 5001 && st[2].quantity == 1, "ItemStacks stack C (Master Ball x1)");
    CHECK(mmo_game_read_item_stacks(items, 6, st, 8, &total, NULL) == -1,
          "a truncated ItemStacks is rejected");
}

static void test_item_stack_update_decode(void)
{
    printf("ItemStack update decode (0x42 BattleSideAddPokemon shape):\n");
    mmo_item_stack st;
    /* Potion 5017 x5, object id (5017 << 16) | 0x5000 = 0x13995000. */
    static const u8 potion[] = {
        0x01,                                           /* side */
        0x00,                                           /* flags */
        0x00, 0x50, 0x99, 0x13, 0x00, 0x00, 0x00, 0x00, /* entity */
        0x99, 0x13,                                     /* item 5017 */
        0x05, 0x00,                                     /* quantity 5 */
        0x01,                                           /* pokemon side */
    };
    CHECK(mmo_game_read_item_stack_update(potion, sizeof potion, &st) == 1,
          "an item-tagged 0x42 is a bag stack");
    CHECK(st.object_id == 0x13995000LL && st.item_id == 5017 && st.quantity == 5,
          "0x42 lands object id, Potion wire id and quantity");

    /* Same shape, monster uid ending in 0xC000: a battle-side add, not a bag. */
    static const u8 battle[] = {
        0x00,
        0x00,
        0x00, 0xC0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x19, 0x00,
        0x01, 0x00,
        0x00,
    };
    CHECK(mmo_game_read_item_stack_update(battle, sizeof battle, &st) == 0,
          "a monster-tagged 0x42 is not a bag stack");

    static const u8 gone[] = {
        0x01,
        0x00,
        0x00, 0x50, 0x99, 0x13, 0x00, 0x00, 0x00, 0x00,
        0x99, 0x13,
        0x00, 0x00,
        0x01,
    };
    CHECK(mmo_game_read_item_stack_update(gone, sizeof gone, &st) == 1
          && st.quantity == 0,
          "a zero-quantity 0x42 is still a bag stack (the removal)");
    CHECK(mmo_game_read_item_stack_update(potion, 6, &st) == -1,
          "a truncated 0x42 is rejected");
}

static void test_local_character_delta_decode(void)
{
    printf("LocalCharacterDelta decode (0x0C f/cd1 walk):\n");
    mmo_local_character_delta d;

    /* mask 0x0001, money 1900, the committed money_32763.bin body. */
    static const u8 cash[] = { 0x01, 0x00, 0x6c, 0x07, 0x00, 0x00 };
    CHECK(mmo_game_read_local_character_delta(cash, sizeof cash, &d) == 0
          && d.has_money && d.money == 1900,
          "mask bit 0x1 is the cash balance 1900");

    /* No bits: just the mask. The official client still finishes. */
    static const u8 empty[] = { 0x00, 0x00 };
    CHECK(mmo_game_read_local_character_delta(empty, sizeof empty, &d) == 0
          && !d.has_money,
          "a zero mask is well-formed and carries no cash");

    /* Bit 0x2 is map (S16LE + S8). Walk it so a later money bit would land. */
    static const u8 map_only[] = { 0x02, 0x00, 0x9f, 0x00, 0x01 };
    CHECK(mmo_game_read_local_character_delta(map_only, sizeof map_only, &d) == 0
          && !d.has_money,
          "a map-only body is walked and has no cash");

    /* Bit 0x40, kind 0 (PU0.Ag): two extra halfwords. */
    static const u8 kind0[] = { 0x40, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00 };
    CHECK(mmo_game_read_local_character_delta(kind0, sizeof kind0, &d) == 0,
          "kind 0 carries two halfwords");

    /* Bit 0x40, kind 0xFF (PU0.Ay1): no extra halfwords. */
    static const u8 kind_def[] = { 0x40, 0x00, 0xff };
    CHECK(mmo_game_read_local_character_delta(kind_def, sizeof kind_def, &d) == 0,
          "kind -1 carries no extra halfwords");

    /* Bit 0x80, signed length: a negative count is the throw f/cd1 would make. */
    static const u8 neg[] = { 0x80, 0x00, 0xff };
    CHECK(mmo_game_read_local_character_delta(neg, sizeof neg, &d) == -1,
          "a negative status-list length is refused");

    CHECK(mmo_game_read_local_character_delta(cash, 2, &d) == -1,
          "a truncated money body is refused");
    CHECK(mmo_game_read_local_character_delta(cash, 1, &d) == -1,
          "a truncated mask is refused");
}

static void test_shop_catalog_decode(void)
{
    printf("Shop catalog decode (s2c 0x23, f/s21):\n");
    mmo_shop_catalog cat;
    mmo_shop_item items[8];

    static const u8 closed[] = { 0xff };
    CHECK(mmo_game_read_shop_catalog(closed, sizeof closed, &cat, items, 8) == 0
              && !cat.open && cat.kind == MMO_SHOP_KIND_CLOSED,
          "a close is the single 0xff byte");

    /* kind 0, flags 0, currency 0, one Potion at 300 with endless stock. */
    static const u8 one[] = {
        0x00, 0x00, 0x00,
        0x01, 0x00,
        0x99, 0x13, 0xff, 0x7f, 0x2c, 0x01, 0x00, 0x00,
    };
    CHECK(mmo_game_read_shop_catalog(one, sizeof one, &cat, items, 8) == 0
              && cat.open && cat.count == 1 && cat.total == 1,
          "a one-line mart opens");
    CHECK(items[0].item_id == 5017 && items[0].stock == 0x7FFF
              && items[0].price == 300,
          "the line is Potion x endless at 300");

    /* flags bit 0x40 carries the clerk entity. */
    static const u8 with_npc[] = {
        0x00, 0x40, 0x00,
        0x00, 0xe0, 0x48, 0xbe, 0x5e, 0xb8, 0xd3, 0x1a,
        0x00, 0x00,
    };
    CHECK(mmo_game_read_shop_catalog(with_npc, sizeof with_npc, &cat, items, 8) == 0
              && cat.has_npc && cat.npc_entity_id == 0x1AD3B85EBE48E000LL
              && cat.count == 0,
          "flags bit 0x40 is the clerk entity");

    /* kind 3: extras instead of a price, plus the trailing pair list. */
    static const u8 pairs[] = {
        0x03, 0x04, 0x00,
        0x01, 0x00,
        0x99, 0x13, 0x01, 0x00, 0x01, 0x11, 0x00, 0x22, 0x00,
        0x01, 0x00, 0x33, 0x00, 0x44, 0x00,
    };
    CHECK(mmo_game_read_shop_catalog(pairs, sizeof pairs, &cat, items, 8) == 0
              && cat.open && cat.kind == MMO_SHOP_KIND_PAIRS
              && cat.count == 1 && items[0].item_id == 5017
              && items[0].price == 0,
          "kind 3 walks extra pairs and stores no price");

    CHECK(mmo_game_read_shop_catalog(one, 3, &cat, items, 8) == -1,
          "a truncated item list is refused");
}

static void test_dialog_action_decode(void)
{
    printf("Dialog action decode (s2c 0x21, f/iq1):\n");
    mmo_dialog_action box;
    u16 bank = 0, entry = 0;
    const char *why = NULL;

    /* Committed close fixture (flags_00_type_64). */
    static const u8 close[] = {
        0x00, 0x64,
        0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00,
        0x00,
    };
    CHECK(mmo_game_read_dialog_action(close, sizeof close, &box) == 0
              && box.close && box.action_type == (s8)MMO_DIALOG_ACTION_CLOSE
              && box.text_id == 0 && box.entity_id == -1
              && box.arg_count == 0,
          "a close is type 0x64, text 0, entity -1");

    /* Committed npc box (flags_0e_type_04): Hoenn text id 0x010006F9. */
    static const u8 npc[] = {
        0x0e, 0x04,
        0xf9, 0x06, 0x00, 0x01,
        0x0a, 0xe0, 0x08, 0xbf, 0x86, 0xc4, 0xc0, 0x1a,
        0xb0, 0x04, 0x00, 0x00,
        0x00,
    };
    CHECK(mmo_game_read_dialog_action(npc, sizeof npc, &box) == 0
              && !box.close && box.action_type == (s8)MMO_DIALOG_ACTION_NPC
              && box.text_id == 0x010006F9
              && box.entity_id == 0x1AC0C486BF08E00ALL
              && box.context_value == 0x04B0,
          "an npc box keeps the captured text id and speaker");

    /* Committed creature_arg: one tag-0 arg, no nested children. */
    static const u8 creature[] = {
        0x0d, 0x04,
        0xf8, 0x06, 0x00, 0x01,
        0x0a, 0xe0, 0x08, 0xbf, 0x86, 0xc4, 0xc0, 0x1a,
        0xb0, 0x04, 0x00, 0x00,
        0x01, 0x00, 0x09, 0x92, 0xd0, 0x03, 0x00,
    };
    CHECK(mmo_game_read_dialog_action(creature, sizeof creature, &box) == 0
              && box.arg_count == 1 && box.detail_len == 0,
          "a creature arg is walked and not left as a tail");

    /* Committed species_arg: tag 2 plus a one-byte tail. */
    static const u8 species[] = {
        0x00, 0x04,
        0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x02, 0x01, 0x01, 0xff, 0x00, 0x00,
    };
    CHECK(mmo_game_read_dialog_action(species, sizeof species, &box) == 0
              && box.arg_count == 1 && box.detail_len == 1
              && box.text_id == 0,
          "a species-name arg is walked and the leftover byte is the tail");

    /* Twinleaf town sign, the pair the engine loader takes. */
    static const u8 sign[] = {
        0x00, 0x03,
        0x0c, 0x00, 0x2a, 0x32,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00,
        0x00,
    };
    CHECK(mmo_game_read_dialog_action(sign, sizeof sign, &box) == 0
              && box.action_type == (s8)MMO_DIALOG_ACTION_SIGN
              && box.text_id == (s32)0x322A000C,
          "a sign box carries the Twinleaf map-sign id");
    CHECK(mmo_id_text_from_server((u32)box.text_id, &bank, &entry, &why) == 0
              && bank == 554 && entry == 12 && why == NULL,
          "that id is bank 554 entry 12");

    /* ours: tag 4, a string-variable slot and the name for it. The DS
     * line that asks for it is "{STRVAR_1 3, 1, 0}", slot 1. */
    static const u8 strvar[] = {
        0x00, 0x04,
        0x07, 0x00, 0x2e, 0x32,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00,
        0x02,
        0x04, 0x00, 'B', 0x00, 'a', 0x00, 'r', 0x00, 'r', 0x00, 'y', 0x00,
        0x00, 0x00,
        0x04, 0x01, 'L', 0x00, 'u', 0x00, 'c', 0x00, 'a', 0x00, 's', 0x00,
        0x00, 0x00,
    };
    CHECK(mmo_game_read_dialog_action(strvar, sizeof strvar, &box) == 0
              && box.arg_count == 2 && box.strvar_count == 2
              && box.strvar[0].slot == 0
              && strcmp(box.strvar[0].text, "Barry") == 0
              && box.strvar[1].slot == 1
              && strcmp(box.strvar[1].text, "Lucas") == 0
              && box.detail_len == 0,
          "a string-variable arg keeps its slot and its name");

    static const u8 bad_tag[] = {
        0x00, 0x04,
        0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x7f,
    };
    CHECK(mmo_game_read_dialog_action(bad_tag, sizeof bad_tag, &box) == -1,
          "an unknown arg tag is refused");
    CHECK(mmo_game_read_dialog_action(close, 10, &box) == -1,
          "a truncated body is refused");

    /* Type 5 is qM1.Bs0. No extra tail. The nurse rest line is bank 213
     * entry 0. */
    static const u8 yesno[] = {
        0x00, 0x05,
        0x00, 0x00, 0xd5, 0x30,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00,
        0x00,
    };
    CHECK(mmo_game_read_dialog_action(yesno, sizeof yesno, &box) == 0
              && box.action_type == (s8)MMO_DIALOG_ACTION_YESNO
              && box.text_id == (s32)0x30D50000
              && box.choice_count == 0 && box.detail_len == 0,
          "a yes/no is type 5 and has no choice tail");
    CHECK(mmo_id_text_from_server((u32)box.text_id, &bank, &entry, &why) == 0
              && bank == 213 && entry == 0 && why == NULL,
          "that id is bank 213 entry 0");

    /* Type 0x23 is qM1.mJ: U8 count then S16LE species ids. */
    static const u8 menu[] = {
        0x00, 0x23,
        0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x03,
        0x83, 0x01,
        0x86, 0x01,
        0x89, 0x01,
    };
    CHECK(mmo_game_read_dialog_action(menu, sizeof menu, &box) == 0
              && box.action_type == (s8)MMO_DIALOG_ACTION_MENU
              && box.text_id == 0 && box.choice_count == 3
              && box.choices[0] == 387 && box.choices[1] == 390
              && box.choices[2] == 393 && box.detail_len == 0,
          "a menu is type 0x23 and the tail is three species");

    static const u8 short_menu[] = {
        0x00, 0x23,
        0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x02,
        0x83, 0x01,
    };
    CHECK(mmo_game_read_dialog_action(short_menu, sizeof short_menu, &box) == -1,
          "a menu whose shorts run out is refused");

    /* Type 0x31 is qM1.b: two unused bytes, S16LE bank, U8 count, S16LE entries. */
    static const u8 list[] = {
        0x00, 0x31,
        0x50, 0x00, 0x17, 0x30,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00,
        0x69, 0x01,
        0x03,
        0x2d, 0x00,
        0x2e, 0x00,
        0x2f, 0x00,
    };
    CHECK(mmo_game_read_dialog_action(list, sizeof list, &box) == 0
              && box.action_type == (s8)MMO_DIALOG_ACTION_LIST
              && box.text_id == (s32)0x30170050
              && box.choice_bank == 361 && box.choice_count == 3
              && box.choices[0] == 0x2d && box.choices[1] == 0x2e
              && box.choices[2] == 0x2f && box.detail_len == 0,
          "a list is type 0x31 and the tail is bank 361 then three entries");

    static const u8 short_list[] = {
        0x00, 0x31,
        0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00,
        0x69, 0x01,
        0x02,
        0x2d, 0x00,
    };
    CHECK(mmo_game_read_dialog_action(short_list, sizeof short_list, &box) == -1,
          "a list whose shorts run out is refused");
}

static void test_dialog_reply_encode(void)
{
    printf("Dialog reply encode (c2s 0x21, f/vG1):\n");
    mmo_wbuf w;
    static const u8 want[] = { 0x0c, 0x00 };

    mmo_wbuf_init(&w);
    mmo_game_write_dialog_reply(&w, 0x0c, 0);
    CHECK(!w.err && w.len == sizeof want
              && memcmp(w.data, want, sizeof want) == 0,
          "a reply is the box flags then a response byte");
    mmo_wbuf_free(&w);

    /* JP1 sends 1 for yes on qM1.Bs0. */
    static const u8 yes[] = { 0x0c, 0x01 };
    mmo_wbuf_init(&w);
    mmo_game_write_dialog_reply(&w, 0x0c, 1);
    CHECK(!w.err && w.len == sizeof yes
              && memcmp(w.data, yes, sizeof yes) == 0,
          "a yes is the same flags then 1");
    mmo_wbuf_free(&w);
}

static void test_entity_interact_encode(void)
{
    printf("Entity interact encode (c2s 0x22, f/com7):\n");
    mmo_wbuf w;
    static const u8 want[] = {
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    mmo_wbuf_init(&w);
    mmo_game_write_entity_interact(&w, 0x0102030405060708LL, 0);
    CHECK(!w.err && w.len == sizeof want
              && memcmp(w.data, want, sizeof want) == 0,
          "an interact is the entity id then a token");
    mmo_wbuf_free(&w);
}

static void test_dialog_state_decode(void)
{
    printf("Dialog state decode (s2c 0x0E, f/j1):\n");
    int on = -1;
    static const u8 locked[] = { 0x01 };
    static const u8 free[] = { 0x00 };
    static const u8 extra[] = { 0x01, 0x00 };

    CHECK(mmo_game_read_dialog_state(locked, sizeof locked, &on) == 0 && on == 1,
          "1 is the lock");
    CHECK(mmo_game_read_dialog_state(free, sizeof free, &on) == 0 && on == 0,
          "0 releases it");
    CHECK(mmo_game_read_dialog_state(extra, sizeof extra, &on) == -1,
          "a trailing byte is refused");
    CHECK(mmo_game_read_dialog_state(locked, 0, &on) == -1,
          "an empty body is refused");
}

static void test_objective_decode(void)
{
    printf("Objective decode (s2c 0xD3 f/uV1, 0xD4 f/Q60):\n");
    mmo_objective one;
    mmo_objective list[4];
    int nent = -1;

    static const u8 single[] = {
        0x01,
        0x03, 0x00, 0x00, 0x00,
        0x05, 0x00,
    };
    CHECK(mmo_game_read_objective(single, sizeof single, &one) == 0
              && one.id == 1 && one.value == 3 && one.count == 5,
          "a single is id, value, count");

    static const u8 empty[] = { 0x00 };
    CHECK(mmo_game_read_objective_bulk(empty, sizeof empty, list, 4, &nent) == 0
              && nent == 0,
          "an empty bulk is a zero count");

    static const u8 bulk[] = {
        0x01,
        0x01,
        0x03, 0x00, 0x00, 0x00,
        0x05, 0x00,
    };
    CHECK(mmo_game_read_objective_bulk(bulk, sizeof bulk, list, 4, &nent) == 0
              && nent == 1 && list[0].id == 1 && list[0].value == 3
              && list[0].count == 5,
          "a one-entry bulk is the count then the same triple");

    static const u8 extra[] = {
        0x01,
        0x03, 0x00, 0x00, 0x00,
        0x05, 0x00,
        0x00,
    };
    static const u8 extra_bulk[] = {
        0x01,
        0x01,
        0x03, 0x00, 0x00, 0x00,
        0x05, 0x00,
        0x00,
    };
    CHECK(mmo_game_read_objective(extra, sizeof extra, &one) == -1,
          "a trailing byte on a single is refused");
    CHECK(mmo_game_read_objective_bulk(extra_bulk, sizeof extra_bulk, list, 4, &nent) == -1,
          "a trailing byte on a bulk is refused");
    CHECK(mmo_game_read_objective(single, 3, &one) == -1,
          "a truncated single is refused");
    CHECK(mmo_game_read_objective_bulk(bulk, 2, list, 4, &nent) == -1,
          "a truncated bulk is refused");
}

static void test_ui_decode(void)
{
    printf("UI decode (s2c 0x58/0x59/0x5A/0x5B/0x5C/0xD6/0xD8/0xD9/0xF1):\n");
    mmo_menu_visibility vis;
    mmo_menu_page page;
    mmo_name_choices names;
    mmo_option_list options;
    mmo_list_window list;
    mmo_confirm_prompt confirm;
    mmo_menu_prompt prompt;
    s8 scale = -1;
    s8 closed = -1;

    static const u8 hide[] = { 0x00 };
    CHECK(mmo_game_read_menu_visibility(hide, sizeof hide, &vis) == 0
              && vis.enabled == 0,
          "0x58 with 0 hides the menu");

    static const u8 show0[] = { 0x01, 0x00 };
    CHECK(mmo_game_read_menu_visibility(show0, sizeof show0, &vis) == 0
              && vis.enabled == 1 && vis.menu_type == 0,
          "0x58 with 1 then type 0 shows it");

    static const u8 show_bad[] = { 0x01, 0x02 };
    CHECK(mmo_game_read_menu_visibility(show_bad, sizeof show_bad, &vis) == -1,
          "0x58 with Ew1 2 is refused");

    static const u8 vis_extra[] = { 0x00, 0x00 };
    CHECK(mmo_game_read_menu_visibility(vis_extra, sizeof vis_extra, &vis) == -1,
          "a trailing byte on 0x58 is refused");

    static const u8 empty0[] = { 0x00, 0x00 };
    CHECK(mmo_game_read_menu_page(empty0, sizeof empty0, &page) == 0
              && page.menu_type == 0 && page.present == 0 && page.len == 0,
          "0x59 type 0 with no blob is the join page");

    static const u8 empty1[] = { 0x01, 0x00 };
    CHECK(mmo_game_read_menu_page(empty1, sizeof empty1, &page) == 0
              && page.menu_type == 1 && page.present == 0,
          "0x59 type 1 with no blob is the other join page");

    static const u8 present_empty[] = {
        0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
    };
    CHECK(mmo_game_read_menu_page(present_empty, sizeof present_empty, &page) == 0
              && page.present == 1 && page.len == 0,
          "0x59 present with a zero-length blob is accepted");

    static const u8 page_bad[] = { 0x02, 0x00 };
    CHECK(mmo_game_read_menu_page(page_bad, sizeof page_bad, &page) == -1,
          "0x59 with Ew1 2 is refused");

    static const u8 no_names[] = { 0x00, 0x00, 0x00 };
    CHECK(mmo_game_read_name_choices(no_names, sizeof no_names, &names) == 0
              && names.count == 0 && names.kind == 0 && names.flag == 0,
          "0x5A with a zero count is empty");

    static const u8 one_name[] = {
        0x01, 0x01, 0x01,
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x52, 0x00, 0x65, 0x00, 0x64, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_name_choices(one_name, sizeof one_name, &names) == 0
              && names.count == 1 && names.flag == 1
              && names.entry[0].entity_id == 42
              && strcmp(names.entry[0].name, "Red") == 0,
          "0x5A one name is the id then the utf16");

    static const u8 no_opts[] = { 0x01, 0x00 };
    CHECK(mmo_game_read_option_list(no_opts, sizeof no_opts, &options) == 0
              && options.kind == 1 && options.count == 0,
          "0x5B with a zero count is empty");

    static const u8 one_opt[] = {
        0x01, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
        0x03, 0x04,
        0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00,
    };
    CHECK(mmo_game_read_option_list(one_opt, sizeof one_opt, &options) == 0
              && options.count == 1
              && options.entry[0].type_id == 3
              && options.entry[0].sub_type == 4
              && options.entry[0].value[0] == 1
              && options.entry[0].value[4] == 5,
          "0x5B one option skips the discarded id and byte");

    static const u8 no_rows[] = {
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x01,
        0x00, 0x00, 0x00,
        0x00, 0x00,
    };
    CHECK(mmo_game_read_list_window(no_rows, sizeof no_rows, &list) == 0
              && list.window_id == 1 && list.first_page == 1
              && list.last_page == 1 && list.count == 0,
          "0x5C with a zero count is an empty page");

    static const u8 one_row[] = {
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x01,
        0x00, 0x00, 0x00,
        0x01, 0x00,
        0x07,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x41, 0x00, 0x00, 0x00,
        0x00, 0x00,
    };
    CHECK(mmo_game_read_list_window(one_row, sizeof one_row, &list) == 0
              && list.count == 1 && list.row[0].row_type == 7
              && strcmp(list.row[0].label, "A") == 0,
          "0x5C one row is the type, five shorts, the utf16, one short");

    static const u8 confirm_off[] = { 0x00 };
    CHECK(mmo_game_read_confirm_prompt(confirm_off, sizeof confirm_off,
                                       &confirm) == 0
              && confirm.visible == 0,
          "0xD9 with 0 hides the confirm");

    static const u8 confirm_on[] = {
        0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1e, 0x00,
        0x1e, 0x00,
    };
    CHECK(mmo_game_read_confirm_prompt(confirm_on, sizeof confirm_on,
                                       &confirm) == 0
              && confirm.visible == 1 && confirm.entity_id == 0
              && confirm.request_s == 30 && confirm.response_s == 30,
          "0xD9 with 1 is the entity then two second counts");

    static const u8 close[] = { 0x02 };
    CHECK(mmo_game_read_menu_prompt_close(close, sizeof close, &closed) == 0
              && closed == 2,
          "0xD8 is one Yr byte");

    static const u8 prompt1[] = { 0x01, 0x04 };
    CHECK(mmo_game_read_menu_prompt_open(prompt1, sizeof prompt1, &prompt) == 0
              && prompt.kind == 1 && prompt.prompt_type == 4,
          "0xD6 kind 1 is the Yr byte");

    static const u8 prompt3[] = { 0x03, 0x04, 0x05, 0x00 };
    CHECK(mmo_game_read_menu_prompt_open(prompt3, sizeof prompt3, &prompt) == 0
              && prompt.kind == 3 && prompt.prompt_type == 4
              && prompt.value == 5,
          "0xD6 kind 3 is the Yr byte then a short");

    static const u8 prompt0[] = { 0x00 };
    CHECK(mmo_game_read_menu_prompt_open(prompt0, sizeof prompt0, &prompt) == 0
              && prompt.kind == 0 && prompt.prompt_type == 0,
          "0xD6 kind 0 has no tail");

    static const u8 scale5[] = { 0x05 };
    CHECK(mmo_game_read_view_scale(scale5, sizeof scale5, &scale) == 0
              && scale == 5,
          "0xF1 is the one scale byte the join sends");

    static const u8 scale_extra[] = { 0x05, 0x00 };
    CHECK(mmo_game_read_view_scale(scale_extra, sizeof scale_extra, &scale) == -1,
          "a trailing byte on 0xF1 is refused");
}

static void test_gm_decode(void)
{
    printf("staff decode (s2c 0xA1/0xA2/0xF7, c2s 0xA2/0xA9):\n");
    mmo_gm_lookup lk;
    mmo_gm_panel panel;
    mmo_gm_panel_entry entry;
    mmo_wbuf w;

    static const u8 lookup_absent[] = { 0x00 };
    CHECK(mmo_game_read_gm_lookup(lookup_absent, sizeof lookup_absent,
                                  &lk) == 0
              && !lk.found && lk.count == 0,
          "0xA1 with a 0 first byte carries nothing else");

    static const u8 gm_lookup[] = {
        0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x00, 0x65,
        0x00, 0x73, 0x00, 0x74, 0x00, 0x00, 0x00, 0x74, 0x00, 0x32, 0x00, 0x00,
        0x00, 0x65, 0x00, 0x00, 0x00, 0x01, 0x66, 0x00, 0x00, 0x00, 0x88, 0x77,
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00, 0xc9,
        0x00, 0x6a, 0x00, 0x00, 0x00, 0x02, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xca, 0x00, 0x05,
        0x6b, 0x00, 0x00, 0x00, 0x06, 0x07, 0x08, 0x00, 0x00, 0x00, 0x09, 0x0a,
        0x0b, 0x00, 0xcb, 0x00, 0xcc, 0x00, 0x00, 0x00, 0xcd, 0x00, 0xce, 0x00,
        0x0c, 0xcf, 0x00, 0xd0, 0x00, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x31, 0x00, 0x32, 0x00, 0x37, 0x00, 0x2e, 0x00, 0x30, 0x00, 0x2e, 0x00,
        0x30, 0x00, 0x2e, 0x00, 0x31, 0x00, 0x00, 0x00, 0x10, 0x0e, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x61, 0x00, 0x00, 0x00,
        0x62, 0x00, 0x00, 0x00, 0x63, 0x00, 0x00, 0x00, 0x01, 0x2a, 0x00, 0x00,
        0x00, 0x77, 0x00, 0x68, 0x00, 0x79, 0x00, 0x00, 0x00, 0x01, 0x00, 0x05,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x6c,
        0x00, 0x61, 0x00, 0x62, 0x00, 0x00, 0x00, 0x6e, 0x00, 0x6d, 0x00, 0x00,
        0x00,
    };

    static const u8 gm_lookup_plain[] = {
        0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x00, 0x65,
        0x00, 0x73, 0x00, 0x74, 0x00, 0x00, 0x00, 0x74, 0x00, 0x32, 0x00, 0x00,
        0x00, 0x65, 0x00, 0x00, 0x00, 0x01, 0x66, 0x00, 0x00, 0x00, 0x88, 0x77,
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00, 0xc9,
        0x00, 0x6a, 0x00, 0x00, 0x00, 0x02, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xca, 0x00, 0x05,
        0x6b, 0x00, 0x00, 0x00, 0x06, 0x07, 0x08, 0x00, 0x00, 0x00, 0x09, 0x0a,
        0x0b, 0x00, 0xcb, 0x00, 0xcc, 0x00, 0x00, 0x00, 0xcd, 0x00, 0xce, 0x00,
        0x0c, 0xcf, 0x00, 0xd0, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    };

    CHECK(mmo_game_read_gm_lookup(gm_lookup, sizeof gm_lookup, &lk) == 0
              && lk.found
              && strcmp(lk.session.name, "Test") == 0
              && strcmp(lk.session.secondary, "t2") == 0
              && lk.session.entity_id == 1
              && lk.session.s64_a == 0x1122334455667788LL
              && lk.session.i32[0] == 101 && lk.session.i32[6] == 107
              && lk.session.i16[0] == 201 && lk.session.i16[7] == 208
              && lk.session.i8[0] == 1 && lk.session.i8[10] == 11
              && lk.session.status == 12
              && lk.session.status_count == 2
              && lk.session.status_list[1] == 1,
          "0xA1 walks the whole session record in wire order");
    CHECK(lk.rank == 1 && lk.has_account && lk.account_id == 7
              && lk.playtime == 3600
              && strcmp(lk.address, "127.0.0.1") == 0,
          "a rank above 0 brings the account block with it");
    CHECK(lk.has_detail && lk.detail_value == 42
              && strcmp(lk.detail_text, "why") == 0,
          "the optional detail is an int the official client drops and a string it keeps");
    CHECK(lk.count == 1 && lk.entry[0].entity_id == 5
              && lk.entry[0].value == 6
              && strcmp(lk.entry[0].name_a, "lab") == 0
              && strcmp(lk.entry[0].name_b, "nm") == 0,
          "a character row carries TWO strings, not one");

    CHECK(mmo_game_read_gm_lookup(gm_lookup_plain, sizeof gm_lookup_plain,
                                  &lk) == 0
              && lk.found && lk.rank == 0 && !lk.has_account
              && !lk.has_detail && lk.count == 0,
          "rank 0 skips the account block entirely");

    CHECK(mmo_game_read_gm_lookup(gm_lookup, sizeof gm_lookup - 1, &lk) == -1,
          "a 0xA1 cut short is refused");

    static const u8 panel_note[] = {
        0x01, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x00, 0x61,
        0x00, 0x00, 0x00, 0x62, 0x00, 0x62, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x00,
        0x00,
    };

    static const u8 panel_target[] = {
        0x02, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    static const u8 panel_titled[] = {
        0x65, 0x6c, 0x00, 0x65, 0x00, 0x61, 0x00, 0x64, 0x00, 0x00, 0x00, 0x09,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x00, 0x61, 0x00, 0x00,
        0x00, 0x62, 0x00, 0x62, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x00, 0x00,
    };

    static const u8 panel_summary[] = {
        0x29, 0x0b, 0x00, 0x0c, 0x00, 0x0d, 0x00, 0x04, 0x00, 0x37, 0x00, 0x00,
        0x00, 0x06, 0x07, 0x08,
    };

    static const u8 panel_detail[] = {
        0x28, 0x03, 0x0b, 0x00, 0x0c, 0x00, 0x0d, 0x00, 0x04, 0x00, 0x37, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x06, 0x07, 0x08, 0x02, 0x14, 0x15, 0x16, 0x17,
    };

    static const u8 panel_menu[] = {
        0x66, 0x01, 0x41, 0x00, 0x63, 0x00, 0x74, 0x00, 0x69, 0x00, 0x6f, 0x00,
        0x6e, 0x00, 0x73, 0x00, 0x00, 0x00, 0x02, 0x4b, 0x00, 0x69, 0x00, 0x63,
        0x00, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x75, 0x00, 0x74, 0x00,
        0x65, 0x00, 0x00, 0x00, 0x01,
    };

    static const u8 panel_empty[] = {
        0x1f,
    };

    static const u8 panel_unnamed[] = {
        0x07,
    };

    static const u8 entry_row[] = {
        0x00, 0x53, 0x00, 0x74, 0x00, 0x61, 0x00, 0x74, 0x00, 0x75, 0x00, 0x73,
        0x00, 0x00, 0x00, 0x6f, 0x00, 0x6e, 0x00, 0x6c, 0x00, 0x69, 0x00, 0x6e,
        0x00, 0x65, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    };

    static const u8 entry_clear[] = {
        0x01,
    };

    CHECK(mmo_game_read_gm_panel(panel_note, sizeof panel_note, &panel) == 0
              && panel.known && (u8)panel.variant == 1
              && panel.entity_id == 9 && panel.value == 77
              && strcmp(panel.text_b, "aa") == 0
              && strcmp(panel.text_c, "bb") == 0,
          "0xA2 case 1 is an id, two strings and an int");
    CHECK(mmo_game_read_gm_panel(panel_target, sizeof panel_target,
                                 &panel) == 0
              && panel.known && panel.entity_id == 12,
          "0xA2 case 2 is one id");
    CHECK(mmo_game_read_gm_panel(panel_titled, sizeof panel_titled,
                                 &panel) == 0
              && panel.known && strcmp(panel.text_a, "lead") == 0
              && panel.entity_id == 9 && panel.value == 77
              && strcmp(panel.text_c, "bb") == 0,
          "0xA2 case 101 falls through into case 1 after its own string");
    CHECK(mmo_game_read_gm_panel(panel_summary, sizeof panel_summary,
                                 &panel) == 0
              && panel.known && panel.s16_a == 11 && panel.s16_c == 13
              && panel.s8_b == 4 && panel.s32_a == 55
              && panel.s8_c == 6 && panel.s8_e == 8
              && panel.pair_count == 0,
          "0xA2 case 41 has no lead byte, no booleans and no pair list");
    CHECK(mmo_game_read_gm_panel(panel_detail, sizeof panel_detail,
                                 &panel) == 0
              && panel.known && panel.s8_a == 3 && panel.s16_a == 11
              && panel.s32_a == 55 && panel.flag_a && !panel.flag_b
              && panel.s8_e == 8 && panel.pair_count == 2
              && panel.pair_a[1] == 22 && panel.pair_b[1] == 23,
          "0xA2 case 40 adds the lead byte, two booleans and the pairs");
    CHECK(mmo_game_read_gm_panel(panel_menu, sizeof panel_menu, &panel) == 0
              && panel.known && panel.row_count == 1
              && strcmp(panel.row[0].label, "Actions") == 0
              && panel.row[0].option_count == 2
              && strcmp(panel.row[0].option[0], "Kick") == 0
              && panel.row[0].option_kind[1] == 1,
          "0xA2 case 102 is rows of a label and its own options");
    CHECK(mmo_game_read_gm_panel(panel_empty, sizeof panel_empty, &panel) == 0
              && !panel.known && (u8)panel.variant == 31,
          "0xA2 case 31 is named by the enum and still carries no body");
    CHECK(mmo_game_read_gm_panel(panel_unnamed, sizeof panel_unnamed,
                                 &panel) == 0
              && !panel.known && (u8)panel.variant == 7,
          "a byte the enum does not name falls back to no body at all");

    static const u8 panel_trailing[] = { 0x1f, 0x00 };
    CHECK(mmo_game_read_gm_panel(panel_trailing, sizeof panel_trailing,
                                 &panel) == -1,
          "a body under a no-body case is refused, not skipped");

    CHECK(mmo_game_read_gm_panel_entry(entry_row, sizeof entry_row,
                                       &entry) == 0
              && entry.clear == 0 && entry.count == 1
              && strcmp(entry.label, "Status") == 0
              && strcmp(entry.value, "online") == 0,
          "0xF7 with a 0 first byte is a label, a value and a count");
    CHECK(mmo_game_read_gm_panel_entry(entry_clear, sizeof entry_clear,
                                       &entry) == 0
              && entry.clear == 1 && entry.label[0] == '\0',
          "0xF7 with any other first byte is the clear and stops there");

    mmo_wbuf_init(&w);
    mmo_game_write_admin_note_add(&w, 0x0102030405060708LL, "hi");
    CHECK(w.len == 15 && w.data[0] == 1
              && w.data[1] == 0x08 && w.data[8] == 0x01
              && w.data[9] == 'h' && w.data[10] == 0
              && w.data[11] == 'i' && w.data[12] == 0
              && w.data[13] == 0 && w.data[14] == 0,
          "c2s 0xA2 add is the action byte, the id and a utf16 note");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_admin_note_delete(&w, 1, 2);
    CHECK(w.len == 17 && w.data[0] == 2 && w.data[1] == 1 && w.data[9] == 2,
          "c2s 0xA2 delete is the action byte and two ids");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_moderation_confirm(&w, 3);
    CHECK(w.len == 8 && w.data[0] == 3,
          "c2s 0xA9 is one id and nothing else");
    mmo_wbuf_free(&w);
}

/* The signup is the one competitive request with two shapes on one opcode, and
 * the leading byte is what tells them apart: a count, or a 0 that is not a
 * count. Getting that backwards is what put every tail of c2s 0x4C one case
 * out once already, so it is pinned here rather than trusted. */
static void test_compete_signup_encode(void)
{
    printf("competitive signup encode (c2s 0x48):\n");
    mmo_wbuf b;
    static const s8 queues[2] = { 0, 6 };
    static const s8 slots[2] = { 0, 1 };
    static const u8 want_queues[] = { 0x02, 0x00, 0x00, 0x06, 0x01 };
    static const u8 want_tourney[] = {
        0x00,
        0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03,
    };

    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_queue_signup(&b, queues, slots, 2) == 0
              && !b.err && b.len == sizeof want_queues
              && memcmp(b.data, want_queues, sizeof want_queues) == 0,
          "a queue signup is a count then a queue byte and a party byte each");
    mmo_wbuf_free(&b);

    /* The leading byte is the discriminator as well as the count, so an empty
     * list would read as the tournament form and then run off the end of the
     * body looking for an id. The writer refuses rather than framing it. */
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_queue_signup(&b, NULL, NULL, 0) == -1 && b.len == 0,
          "an empty signup is refused, because a zero count is a tournament");
    mmo_wbuf_free(&b);

    mmo_wbuf_init(&b);
    mmo_game_write_tourney_signup(&b, 7, 3);
    CHECK(!b.err && b.len == sizeof want_tourney
              && memcmp(b.data, want_tourney, sizeof want_tourney) == 0,
          "a tournament signup is a leading 0, the id, and the party byte");
    mmo_wbuf_free(&b);
}

static void test_compete_decode(void)
{
    printf("competitive decode (s2c 0x71/0x75/0x78/0x7B/0xA4):\n");
    mmo_rentals rent;
    mmo_tourney_page page;
    mmo_tourney_count count;
    mmo_matchups bracket;
    mmo_score_board board;

    static const u8 rentals_preview[] = {
        0x02, 0x00, 0x00, 0x00,
        0x02,
        0x19, 0x00, 0x32, 0x00,
        0x06, 0x00, 0x32, 0x00,
    };
    CHECK(mmo_game_read_rentals(rentals_preview, sizeof rentals_preview,
                                &rent) == 0
              && rent.mode == 2 && rent.preview_count == 2
              && rent.monster_count == 0
              && rent.preview[0].species == 25 && rent.preview[0].level == 50
              && rent.preview[1].species == 6,
          "0x71 mode 2 is four bytes then a U8-prefixed list of two shorts");

    /* Only modes 0 and 2 carry a list; the client reads nothing else at all,
     * so a mode 1 body is the four bytes and stops there. */
    static const u8 rentals_other[] = { 0x01, 0x07, 0x08, 0x09 };
    CHECK(mmo_game_read_rentals(rentals_other, sizeof rentals_other,
                                &rent) == 0
              && rent.mode == 1 && rent.param[0] == 7 && rent.param[2] == 9
              && rent.preview_count == 0 && rent.monster_count == 0,
          "0x71 in any other mode carries no list");

    static const u8 rentals_short[] = { 0x02, 0x00, 0x00, 0x00, 0x01 };
    CHECK(mmo_game_read_rentals(rentals_short, sizeof rentals_short,
                                &rent) == -1,
          "a 0x71 whose list runs off the end is refused");

    static const u8 tourney_page[] = {
        0x00,                                            /* tab */
        0x00,                                            /* second list */
        0x01, 0x00,                                      /* page */
        0x01, 0x00, 0x00, 0x00,                          /* total */
        0x01,                                            /* rows */
        0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* id */
        0x02,                                            /* kind */
        0x03,                                            /* type */
        0x43, 0x00, 0x75, 0x00, 0x70, 0x00, 0x00, 0x00,  /* "Cup" */
        0x08, 0x00,                                      /* capacity */
        0x00, 0x00,                                      /* format */
        0x00,                                            /* dropped */
        0x40, 0xe2, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  /* start time */
        0x01,                                            /* mode */
        0x05, 0x00, 0x00, 0x00,
        0x0f,
        0x01, 0x00,                                      /* the two flags */
        0x00,                                            /* dropped */
    };
    CHECK(mmo_game_read_tourney_page(tourney_page, sizeof tourney_page,
                                     &page) == 0
              && page.count == 1 && page.page == 1 && page.total == 1
              && page.entry[0].id == 7 && page.entry[0].kind == 2
              && page.entry[0].type == 3
              && strcmp(page.entry[0].name, "Cup") == 0
              && page.entry[0].capacity == 8
              && page.entry[0].start_time == 123456
              && page.entry[0].mode == 1 && page.entry[0].s32_a == 5
              && page.entry[0].u8_a == 15
              && page.entry[0].flag_a == 1 && page.entry[0].flag_b == 0,
          "0x75 is a tab, a page and a total, then whole tournaments");

    /* Two bytes of the record are read and dropped: the one after the format
     * short and the one after the two booleans. Losing either shifts every
     * field after it, so the row above is also what pins them. */
    CHECK(mmo_game_read_tourney_page(tourney_page, sizeof tourney_page - 1,
                                     &page) == -1,
          "a 0x75 one byte short of the record's tail is refused");

    static const u8 entry_count[] = {
        0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x04, 0x00,
        0xff, 0xff,
    };
    CHECK(mmo_game_read_tourney_count(entry_count, sizeof entry_count,
                                      &count) == 0
              && count.tourney_id == 7 && count.entered == 4
              && count.checked_in == -1,
          "0x78 is the tournament id then two signed shorts");

    static const u8 matchups[] = {
        0x02, 0x00,
        0xff, 0xff, 0x00,
        0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01,
        0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x00,
    };
    CHECK(mmo_game_read_matchups(matchups, sizeof matchups, &bracket) == 0
              && bracket.count == 2
              && bracket.entry[0].winner == -1 && bracket.entry[0].type == 0
              && bracket.entry[0].has_entrants == 0
              && bracket.entry[0].id == 100
              && bracket.entry[1].has_entrants == 1
              && bracket.entry[1].entrant[0] == 0
              && bracket.entry[1].entrant[1] == 1,
          "0x7B takes the two entrant refs only when the type byte is set");

    static const u8 score_board[] = {
        0x00,                                            /* category */
        0x00, 0x00, 0x00, 0x00,                          /* dropped */
        0x01, 0x00, 0x00, 0x00,                          /* rows */
        0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* id */
        0x01, 0x00, 0x00, 0x00,                          /* rank */
        0xd2, 0x04, 0x00, 0x00,                          /* score */
        0x01,                                            /* monsters */
        0x50, 0x00, 0x69, 0x00, 0x00, 0x00,              /* "Pi" */
        0x01,
        0x19, 0x00, 0x00, 0x00,
        0x02,
        0x1b,                                            /* packed 3,2,1,0 */
        0x01, 0x00,
        0xff, 0xff,
        0x02, 0x04,
        0x00, 0x00,
    };
    CHECK(mmo_game_read_score_board(score_board, sizeof score_board,
                                    &board) == 0
              && board.category == 0 && board.count == 1
              && board.row[0].entity_id == 11 && board.row[0].rank == 1
              && board.row[0].score == 1234
              && board.row[0].monster_count == 1
              && strcmp(board.row[0].monster[0].name, "Pi") == 0
              && board.row[0].monster[0].s32_a == 25,
          "0xA4 is a category and a count, then rows of monsters");

    /* Each short is a ten-bit value under a six-bit second field, and the
     * client's own absent is 1023 and 63 rather than zero. */
    CHECK(board.row[0].monster[0].value[0] == 1
              && board.row[0].monster[0].extra[0] == 0
              && board.row[0].monster[0].value[1] == -1
              && board.row[0].monster[0].extra[1] == -1
              && board.row[0].monster[0].value[2] == 2
              && board.row[0].monster[0].extra[2] == 1,
          "a slot of 1023 over 63 is absent, not a value");

    /* The two-bit-per-slot byte is read low slot first. */
    CHECK(board.row[0].monster[0].packed[0] == 3
              && board.row[0].monster[0].packed[1] == 2
              && board.row[0].monster[0].packed[2] == 1
              && board.row[0].monster[0].packed[3] == 0,
          "the packed byte is two bits a slot from the bottom up");
}

static void test_sync_decode(void)
{
    printf("sync decode (s2c 0xA6/0xA7/0xAB/0xAC/0x77/0xF6):\n");
    mmo_digest dig;
    mmo_transfer_begin begin;
    mmo_transfer_append append;
    mmo_stream_chunk stream;
    mmo_image_chunk image;
    u8 plain[32];
    size_t pn = 0;
    mmo_wbuf w;

    static const u8 empty_digest[] = { 0x08, 0x00, 0x01, 0x00, 0x00 };
    CHECK(mmo_game_read_digest(empty_digest, sizeof empty_digest, &dig) == 0
              && dig.bit_count == 8 && dig.len == 1 && dig.bytes[0] == 0,
          "0xA6 is the bit count then a U16LE bitset");

    static const u8 digest_extra[] = { 0x08, 0x00, 0x00, 0x00, 0x00 };
    CHECK(mmo_game_read_digest(digest_extra, sizeof digest_extra, &dig) == -1,
          "a trailing byte on 0xA6 is refused");

    static const u8 begin_empty[] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1b, 0x00, 0x00, 0x00,
        0x00, 0x00,
    };
    CHECK(mmo_game_read_transfer_begin(begin_empty, sizeof begin_empty, &begin) == 0
              && begin.id == 1 && begin.size == 27 && begin.sig_len == 0,
          "0xAB is id, size, then a U16LE signature");

    static const u8 one_chunk[] = { 0x03, 0x00, 'a', 'b', 'c' };
    CHECK(mmo_game_read_transfer_append(one_chunk, sizeof one_chunk, &append) == 0
              && append.len == 3 && append.data[0] == 'a' && append.data[2] == 'c',
          "0xAC is one U16LE-prefixed chunk");

    static const u8 stream_last[] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01,
        0x02, 0x00, 'h', 'i',
    };
    CHECK(mmo_game_read_stream_chunk(stream_last, sizeof stream_last, &stream) == 0
              && stream.id == 2 && stream.last == 1 && stream.len == 2
              && stream.data[0] == 'h',
          "0x77 is the id, the last flag, then the bytes");

    static const u8 image_close[] = { 0x02 };
    CHECK(mmo_game_read_image_chunk(image_close, sizeof image_close, &image) == 0
              && image.present == 0 && image.ctrl == 2,
          "0xF6 control 2 is a close with no tail");

    static const u8 image_last[] = {
        0x00, 0x01, 0x00, 0x01,
        0x03, 0x00, 'i', 'm', 'g',
    };
    CHECK(mmo_game_read_image_chunk(image_last, sizeof image_last, &image) == 0
              && image.present == 1 && image.image_type == 1
              && image.chunk_index == 0 && image.last == 1
              && image.len == 3 && image.data[0] == 'i',
          "0xF6 is type, index, last, then the bytes");

    /* python3 gzip.compress(b'sync-ok', mtime=0), then XOR with JV.tn1 */
    static const u8 wired[] = {
        0x4e, 0x18, 0x37, 0xe0, 0x52, 0x63, 0x74, 0xce,
        0x53, 0x6c, 0x14, 0x4e, 0x9e, 0x28, 0xa2, 0x03,
        0x9e, 0x95, 0x3f, 0xeb, 0x90, 0xa3, 0x1f, 0xc9,
        0x51, 0x93, 0x3f,
    };
    CHECK(mmo_game_transfer_open(wired, sizeof wired, plain, sizeof plain, &pn) == 0
              && pn == 7 && memcmp(plain, "sync-ok", 7) == 0,
          "a finished transfer XOR-then-gunzips to the plaintext");

    mmo_wbuf_init(&w);
    mmo_game_write_digest_empty(&w);
    CHECK(w.err == 0 && w.len == 1 && w.data[0] == 0,
          "the empty digest reply is one zero byte");
    mmo_wbuf_free(&w);
}

static void test_friend_decode(void)
{
    printf("Friend decode (s2c 0x63 f/OE0, 0x64 f/hk, 0x65 f/oo1, 0x66 f/As):\n");
    mmo_friend one;
    mmo_friend list[4];
    int nent = -1;
    u8 mode = 0xff;
    s64 player = 0;
    u8 online = 0xff;
    mmo_wbuf w;

    /* One row: player 42, unknown 0, offline, name "Red", zeros after. */
    static const u8 insert[] = {
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        'R', 0x00, 'e', 0x00, 'd', 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_friend(insert, sizeof insert, &one) == 0
              && one.player == 42 && one.unknown == 0 && one.online == 0
              && strcmp(one.name, "Red") == 0
              && one.unk0 == 0 && one.last_seen == 0
              && one.kind == 0 && one.packed_slots == 0
              && one.sprite[0] == 0 && one.sprite[3] == 0,
          "one row is id, unknown, online, then QL1's Prn");

    static const u8 empty[] = { 0x00, 0x00 };
    CHECK(mmo_game_read_friend_list(empty, sizeof empty, list, 4, &nent, &mode)
              == 0 && nent == 0 && mode == 0,
          "an empty list is mode then a zero count");

    static const u8 bulk[] = {
        0x00, 0x01,
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01,
        'B', 0x00, 'l', 0x00, 'u', 0x00, 'e', 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_friend_list(bulk, sizeof bulk, list, 4, &nent, &mode)
              == 0 && nent == 1 && mode == 0 && list[0].player == 42
              && list[0].online == 1 && strcmp(list[0].name, "Blue") == 0,
          "a one-entry list is mode, count, then the same row");

    static const u8 extra[] = {
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        'R', 0x00, 'e', 0x00, 'd', 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
    };
    CHECK(mmo_game_read_friend(extra, sizeof extra, &one) == -1,
          "a trailing byte on a row is refused");
    CHECK(mmo_game_read_friend_list(bulk, 3, list, 4, &nent, &mode) == -1,
          "a truncated list is refused");

    static const u8 del[] = {
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_friend_delete(del, sizeof del, &player) == 0
              && player == 42,
          "a delete is the player id");

    static const u8 on[] = {
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01,
    };
    CHECK(mmo_game_read_friend_online(on, sizeof on, &player, &online) == 0
              && player == 42 && online == 1,
          "an online bit is the id and the byte compared to 1");

    mmo_wbuf_init(&w);
    mmo_game_write_friend_name(&w, "Green");
    CHECK(!w.err && w.len == 12
              && w.data[0] == 'G' && w.data[1] == 0
              && w.data[10] == 0 && w.data[11] == 0,
          "add/remove write the utf16 name the way the official client does");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_block(&w, "Green", "--");
    CHECK(!w.err && w.len == 18
              && w.data[0] == 'G' && w.data[10] == 0 && w.data[11] == 0
              && w.data[12] == '-' && w.data[14] == '-',
          "block writes name then reason the way the official client does");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_trade_request(&w, "Green");
    CHECK(!w.err && w.len == 12
              && w.data[0] == 'G' && w.data[10] == 0 && w.data[11] == 0,
          "a trade request writes one utf16 name the way the official client does");
    mmo_wbuf_free(&w);
}

static void test_guild_decode(void)
{
    printf("Guild decode (s2c 0x80 f/dn0, 0x81 f/Vb, 0x88 f/uI0):\n");
    mmo_guild_profile prof;
    mmo_guild_member one;
    mmo_guild_member list[4];
    mmo_guild_log_entry log[4];
    int in = -1, nent = -1;
    u8 repl = 0xff, rank = 0xff, online = 0xff;
    s64 member = 0;
    s16 total = -1;
    mmo_wbuf w;

    static const u8 none[] = { 0x00 };
    CHECK(mmo_game_read_guild_membership(none, sizeof none, &in, &prof) == 0
              && in == 0,
          "not in a guild is a single zero");

    static const u8 membership[] = {
        0x01,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        'A', 0x00, 0x00, 0x00,
        'B', 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x05, 0x00, 0x05, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
    };
    CHECK(mmo_game_read_guild_membership(membership, sizeof membership,
                                         &in, &prof) == 0
              && in == 1 && prof.guild_id == 1
              && strcmp(prof.name, "A") == 0 && strcmp(prof.tag, "B") == 0
              && prof.perm[0] == 5 && prof.perm[4] == 0
              && prof.rank_count == 0,
          "in a guild is the flag then gs()");

    CHECK(mmo_game_read_guild_profile(membership + 1, sizeof membership - 1,
                                      &prof) == 0
              && prof.guild_id == 1 && strcmp(prof.name, "A") == 0,
          "0x81 is gs() with no flag");

    static const u8 add[] = {
        0x05,
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        'R', 0x00, 'e', 0x00, 'd', 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01,
    };
    CHECK(mmo_game_read_guild_member(add, sizeof add, &one) == 0
              && one.rank == 5 && one.entity_id == 42 && one.online == 1
              && strcmp(one.appearance.name, "Red") == 0,
          "one member is rank, id, joined-at, QL1, then online");

    static const u8 bulk[] = {
        0x01, 0x01,
        0x05,
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        'R', 0x00, 'e', 0x00, 'd', 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01,
    };
    CHECK(mmo_game_read_guild_members(bulk, sizeof bulk, list, 4, &nent, &repl)
              == 0 && nent == 1 && repl == 1 && list[0].entity_id == 42
              && list[0].online == 1,
          "a one-entry list is replace, count, then the same row");

    static const u8 extra[] = {
        0x05,
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        'R', 0x00, 'e', 0x00, 'd', 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01,
        0x00,
    };
    CHECK(mmo_game_read_guild_member(extra, sizeof extra, &one) == -1,
          "a trailing byte on a member is refused");
    CHECK(mmo_game_read_guild_members(bulk, 3, list, 4, &nent, &repl) == -1,
          "a truncated list is refused");

    static const u8 rankchg[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03,
    };
    CHECK(mmo_game_read_guild_rank_change(rankchg, sizeof rankchg, &member, &rank)
              == 0 && member == 42 && rank == 3,
          "a rank change is a discarded id, the member id and the rank");

    static const u8 drop[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_guild_member_drop(drop, sizeof drop, &member) == 0
              && member == 42,
          "a drop is a discarded id then the member id");

    static const u8 pres[] = {
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01,
    };
    CHECK(mmo_game_read_guild_presence(pres, sizeof pres, &member, &online) == 0
              && member == 42 && online == 1,
          "presence is the id and the byte compared to 1");

    static const u8 logbody[] = {
        0x01, 0x00,
        0x01,
        0x00,
        'A', 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_guild_log(logbody, sizeof logbody, log, 4, &nent, &total)
              == 0 && total == 1 && nent == 1 && log[0].type == 0
              && strcmp(log[0].actor, "A") == 0 && log[0].target[0] == '\0',
          "a log page is total, count, then type / actor / target / time");

    mmo_wbuf_init(&w);
    mmo_game_write_guild_create(&w, "A", "B");
    CHECK(!w.err && w.len == 8
              && w.data[0] == 'A' && w.data[2] == 0
              && w.data[4] == 'B' && w.data[6] == 0,
          "create writes two utf16 names the way the official client does");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_guild_disband(&w, 1, 1);
    CHECK(!w.err && w.len == 9 && w.data[0] == 1 && w.data[1] == 1,
          "disband writes the initiate byte then the guild id");
    mmo_wbuf_free(&w);
}

static void test_mail_decode(void)
{
    printf("Mail decode (s2c 0x97 f/ih, 0x98 f/ia0, 0x96 f/oP1):\n");
    mmo_mail one;
    mmo_mail list[4];
    int nent = -1, present = -1;
    s16 page = -1, inbox = -1, seen = -1, sentc = -1;
    u8 sent = 0xff, code = 0xff;
    mmo_wbuf w;

    static const u8 empty[] = {
        0x00, 0x00,
        0x00,
        0x00, 0x00,
    };
    CHECK(mmo_game_read_mail_page(empty, sizeof empty, list, 4, &nent,
                                  &page, &sent) == 0
              && nent == 0 && page == 0 && sent == 0,
          "an empty inbox page is page, the sent flag, then a zero count");

    static const u8 row[] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
        'R', 0x00, 'e', 0x00, 'd', 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        'H', 0x00, 'i', 0x00, 0x00, 0x00,
        0x01,
        0x00,
    };
    CHECK(mmo_game_read_mail(row, sizeof row, 0, 0, &one) == 0
              && one.mail_id == 1 && one.recipient_id == 2
              && one.sender_id == 3 && strcmp(one.sender, "Red") == 0
              && strcmp(one.subject, "Hi") == 0 && one.unread == 1
              && one.has_attachments == 0 && one.body[0] == '\0',
          "one inbox row is three ids, staff, sender, time, subject, unread");

    static const u8 bulk[] = {
        0x00, 0x00,
        0x00,
        0x01, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
        'R', 0x00, 'e', 0x00, 'd', 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        'H', 0x00, 'i', 0x00, 0x00, 0x00,
        0x01,
        0x00,
    };
    CHECK(mmo_game_read_mail_page(bulk, sizeof bulk, list, 4, &nent,
                                  &page, &sent) == 0
              && nent == 1 && sent == 0 && list[0].mail_id == 1
              && strcmp(list[0].sender, "Red") == 0,
          "a one-entry page is page, flag, count, then the same row");

    static const u8 extra[] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
        'R', 0x00, 'e', 0x00, 'd', 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        'H', 0x00, 'i', 0x00, 0x00, 0x00,
        0x01,
        0x00,
        0x00,
    };
    CHECK(mmo_game_read_mail(extra, sizeof extra, 0, 0, &one) == -1,
          "a trailing byte on a row is refused");
    CHECK(mmo_game_read_mail_page(bulk, 4, list, 4, &nent, &page, &sent) == -1,
          "a truncated page is refused");

    static const u8 sentrow[] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
        'B', 0x00, 'l', 0x00, 'u', 0x00, 'e', 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        'H', 0x00, 'i', 0x00, 0x00, 0x00,
        0x00,
        0x00,
    };
    CHECK(mmo_game_read_mail(sentrow, sizeof sentrow, 1, 0, &one) == 0
              && strcmp(one.recipient, "Blue") == 0
              && one.sender[0] == '\0',
          "a sent row carries the recipient instead of the sender");

    static const u8 detail[] = {
        0x01,
        0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
        'R', 0x00, 'e', 0x00, 'd', 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        'H', 0x00, 'i', 0x00, 0x00, 0x00,
        'H', 0x00, 'e', 0x00, 'y', 0x00, 0x00, 0x00,
        0x01,
        0x00,
        0x00,
    };
    CHECK(mmo_game_read_mail_detail(detail, sizeof detail, &one, &present,
                                    &sent) == 0
              && present == 1 && sent == 0
              && strcmp(one.body, "Hey") == 0
              && strcmp(one.sender, "Red") == 0,
          "a detail is present, the sent flag, the row, the body, then a zero count");

    static const u8 missing[] = { 0x00 };
    CHECK(mmo_game_read_mail_detail(missing, sizeof missing, &one, &present,
                                    &sent) == 0 && present == 0,
          "a missing letter is a single zero");

    static const u8 counts[] = {
        0x01, 0x00,
        0x01, 0x00,
        0x00, 0x00,
    };
    CHECK(mmo_game_read_mail_counts(counts, sizeof counts, &inbox, &seen, &sentc)
              == 0 && inbox == 1 && seen == 1 && sentc == 0,
          "counts are inbox, inbox-seen, sent");

    static const u8 result[] = { 0x00 };
    CHECK(mmo_game_read_mail_result(result, sizeof result, &code) == 0
              && code == 0,
          "a result is one sj1 byte");

    mmo_wbuf_init(&w);
    mmo_game_write_mail_compose(&w, "A", "Hi!", "Hello");
    CHECK(!w.err && w.len == 25
              && w.data[0] == 'A' && w.data[2] == 0
              && w.data[24] == 0,
          "compose writes three utf16 strings then a zero attachment count");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_mail_page_req(&w, 0, 1);
    CHECK(!w.err && w.len == 3 && w.data[0] == 0 && w.data[2] == 1,
          "a page request is the page then the sent flag");
    mmo_wbuf_free(&w);
}

static void test_link_decode(void)
{
    printf("Link decode (s2c 0xD0 f/r9, 0xD1 f/dx1, 0xD2 f/LX0):\n");
    mmo_link_member one;
    mmo_link_member list[4];
    int nent = -1, present = -1;
    s64 leader = -1, gone = -1;
    mmo_wbuf w;

    static const u8 empty[] = { 0x00 };
    CHECK(mmo_game_read_link_snapshot(empty, sizeof empty, list, 4, &nent,
                                      &present, &leader) == 0
              && present == 0 && nent == 0 && leader == 0,
          "an empty link is a single zero");

    static const u8 member[] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        'A', 0x00, 's', 0x00, 'h', 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
    };
    CHECK(mmo_game_read_link_member(member, sizeof member, &one) == 0
              && one.entity_id == 2 && strcmp(one.name, "Ash") == 0,
          "one member is the id, QL1, then an empty Nr1");

    static const u8 bulk[] = {
        0x01,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        'A', 0x00, 's', 0x00, 'h', 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
    };
    CHECK(mmo_game_read_link_snapshot(bulk, sizeof bulk, list, 4, &nent,
                                      &present, &leader) == 0
              && present == 1 && nent == 1 && leader == 1
              && list[0].entity_id == 2 && strcmp(list[0].name, "Ash") == 0,
          "a one-member snapshot is present, leader, count, then the same row");

    static const u8 extra[] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        'A', 0x00, 's', 0x00, 'h', 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00,
    };
    CHECK(mmo_game_read_link_member(extra, sizeof extra, &one) == -1,
          "a trailing byte on a member is refused");
    CHECK(mmo_game_read_link_snapshot(bulk, 2, list, 4, &nent,
                                      &present, &leader) == -1,
          "a truncated snapshot is refused");

    static const u8 drop[] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_link_remove(drop, sizeof drop, &gone, &leader) == 0
              && gone == 2 && leader == 1,
          "a drop is the removed id then the leader");

    static const u8 cap[] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_link_leader(cap, sizeof cap, &leader) == 0
              && leader == 2,
          "a captain packet is one id");

    mmo_wbuf_init(&w);
    mmo_game_write_link_invite(&w, "Gary");
    CHECK(!w.err && w.len == 10
              && w.data[0] == 'G' && w.data[8] == 0 && w.data[9] == 0,
          "invite writes one utf16 name");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_link_id(&w, 2);
    CHECK(!w.err && w.len == 8 && w.data[0] == 2,
          "kick and captain write one id");
    mmo_wbuf_free(&w);
}

static void test_script_move_decode(void)
{
    printf("Scripted movement decode (s2c 0x0D, f/Qr0):\n");
    mmo_script_move seq;

    /* Server-shaped walk_up, walk_up, walk_left on entity 42. */
    static const u8 walks[] = {
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x03, 0x11, 0x11, 0x12,
    };
    CHECK(mmo_game_read_script_move(walks, sizeof walks, &seq) == 0
              && seq.entity_id == 42 && seq.flag == 0 && seq.count == 3
              && seq.actions[0] == 0x11 && seq.actions[1] == 0x11
              && seq.actions[2] == 0x12,
          "a three-step walk keeps the entity, the flag and the bytes");
    CHECK(mmo_game_script_action(0, 0x11) == 12
              && mmo_game_script_action(0, 0x12) == 14,
          "walk_up is engine north and walk_left is engine west");

    /* Server-shaped face_right on entity 7. */
    static const u8 face[] = {
        0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x03,
    };
    CHECK(mmo_game_read_script_move(face, sizeof face, &seq) == 0
              && seq.entity_id == 7 && seq.count == 1
              && seq.actions[0] == 0x03,
          "a face step is one action byte");
    CHECK(mmo_game_script_action(0, 0x03) == 3,
          "face_right is engine east");
    CHECK(mmo_game_script_action(0, 0x00) == 1
              && mmo_game_script_action(0, 0x04) == 1,
          "server face-down 0x00 and official 0x04 both face south");

    /* The official client entity -1 is the local avatar. */
    static const u8 self[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x01, 0x03,
    };
    CHECK(mmo_game_read_script_move(self, sizeof self, &seq) == 0
              && seq.entity_id == -1 && seq.count == 1,
          "entity -1 is carried as the local avatar");

    CHECK(mmo_game_script_action(0, 0x99) == -1,
          "an unknown action byte is not guessed");
    CHECK(mmo_game_script_action(1, 0x03) == -1,
          "the m20=true table is not mapped");

    static const u8 short_body[] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x02, 0x11,
    };
    CHECK(mmo_game_read_script_move(short_body, sizeof short_body, &seq) == -1,
          "fewer bytes than the count is refused");
    static const u8 extra[] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x11, 0x12,
    };
    CHECK(mmo_game_read_script_move(extra, sizeof extra, &seq) == -1,
          "a trailing byte past the count is refused");
    CHECK(mmo_game_read_script_move(walks, 8, &seq) == -1,
          "a truncated header is refused");
}

static void test_shop_trade_encode(void)
{
    printf("Shop buy/sell encode (c2s 0x23 / 0x24):\n");
    mmo_wbuf w;
    static const u8 buy_want[] = { 0x8c, 0x13, 0x03, 0x00, 0x00 };
    mmo_wbuf_init(&w);
    mmo_game_write_shop_buy(&w, 5004, 3);
    CHECK(!w.err && w.len == sizeof buy_want
              && memcmp(w.data, buy_want, sizeof buy_want) == 0,
          "a buy is item S16LE, quantity S16LE, type 0");
    mmo_wbuf_free(&w);

    static const u8 sell_want[] = {
        0x00, 0x50, 0x99, 0x13, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    };
    mmo_wbuf_init(&w);
    mmo_game_write_shop_sell(&w, 0x13995000LL, 2);
    CHECK(!w.err && w.len == sizeof sell_want
              && memcmp(w.data, sell_want, sizeof sell_want) == 0,
          "a sell is the bag stack entity then quantity");
    mmo_wbuf_free(&w);
}

static void test_item_use_encode(void)
{
    printf("Item use encode (c2s 0x26):\n");
    mmo_wbuf w;
    static const u8 want[] = {
        0x99, 0x13,
        0x00, 0xc0, 0x0a, 0x0f, 0xef, 0x29, 0x01, 0x20,
        0x01, 0x00, 0xff, 0x00,
    };
    mmo_wbuf_init(&w);
    mmo_game_write_item_use(&w, 5017, 0x200129ef0f0ac000LL, MMO_ITEM_USE_TRAILER);
    CHECK(!w.err && w.len == sizeof want
              && memcmp(w.data, want, sizeof want) == 0,
          "a Potion use is item, target, then the captured trailer");
    mmo_wbuf_free(&w);
}

static void test_load_map_decode(void)
{
    printf("LoadMap decode (server LoadMapPacketCodec, whole body):\n");
    mmo_load_map lm;

    /*
     * A full GBA map, laid out field for field from LoadMapPacketCodec's GBA branch, modelled
     * on Pallet Town (region 0, bank 3, map 0, 24x20): the scene is REGULAR lighting / REGULAR
     * weather / ROUTE type / RANDOM encounters, and it connects UP to bank 3 map 19 and DOWN
     * to bank 3 map 39 (the two Pallet Town connections measured off the server maps).
     */
    static const u8 gba[] = {
        0x02,                               /* flags: reloadPlayer */
        0x00,                               /* region 0 (a GBA region) */
        0x03,                               /* bank 3 */
        0x00,                               /* map 0 */
        0x00,                               /* reserved */
        0x18,0x00,0x00,0x00,                /* width 24 */
        0x14,0x00,0x00,0x00,                /* height 20 */
        0x00,0x00,0x00,0x00,                /* paletteIdx1 */
        0x00,0x00,0x00,0x00,                /* paletteIdx2 */
        0x02,                               /* borderWidth 2 */
        0x02,                               /* borderHeight 2 */
        0x00,0x00,                          /* unknownShort */
        0x00,                               /* unknownByte */
        0x00,                               /* lighting 0 (REGULAR) */
        0x02,                               /* weather 2 (REGULAR) */
        0x03,                               /* mapType 3 (ROUTE) */
        0x00,                               /* encounterType 0 (RANDOM) */
        0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00, /* 4 border Tile2D (U16LE) */
        0x00,                               /* hasCompressed = false */
        0x02,                               /* 2 connections */
        0x02, 0x00,0x00,0x00,0x00, 0x03, 0x13, /* UP(byte 2), off 0, bank 3, map 19 */
        0x01, 0x00,0x00,0x00,0x00, 0x03, 0x27, /* DOWN(byte 1), off 0, bank 3, map 39 */
        0x00,                               /* hasTrailer = false */
    };
    CHECK(mmo_game_read_load_map(gba, sizeof gba, &lm) == 0, "GBA LoadMap decodes");
    CHECK(!lm.is_nds && lm.region_id == 0 && lm.bank_id == 3 && lm.map_id == 0,
          "GBA LoadMap region/bank/map, GBA branch");
    CHECK(lm.reload_player == 1 && lm.delete_cache == 0, "GBA reloadPlayer set, deleteCache clear");
    CHECK(lm.width == 24 && lm.height == 20, "GBA grid dimensions");
    CHECK(lm.lighting == 0 && lm.weather == 2 && lm.map_type == 3 && lm.encounter_type == 0,
          "GBA scene: lighting/weather/mapType/encounterType");
    CHECK(lm.connection_count == 2 && lm.connection_total == 2, "GBA two connections");
    CHECK(lm.connections[0].direction == 1 && lm.connections[0].offset == 0 &&
          lm.connections[0].target_bank == 3 && lm.connections[0].target_map == 19,
          "connection 0: UP (ordinal 1) -> bank 3 map 19");
    CHECK(lm.connections[1].direction == 0 && lm.connections[1].target_bank == 3 &&
          lm.connections[1].target_map == 39,
          "connection 1: DOWN (ordinal 0) -> bank 3 map 39");

    /* A truncated GBA body (stops inside the connection list) is rejected, not
     * half-read, so a short frame never leaves the reader mid-record. */
    CHECK(mmo_game_read_load_map(gba, sizeof gba - 3, &lm) == -1,
          "a truncated GBA LoadMap is rejected");

    /*
     * An NDS map (region 4 is an NDS region): after the header an unnamed S16LE, a U8-prefixed
     * list of halfword pairs and then the scene triple lighting/weather/mapType, no grid, no
     * connections.
     */
    static const u8 nds[] = {
        0x03,               /* flags: deleteCache | reloadPlayer */
        0x04,               /* region 4 (an NDS region) */
        0x01,               /* bank 1 */
        0x02,               /* map 2 */
        0x00,               /* reserved */
        0x00,0x00,          /* unnamed S16LE */
        0x00,               /* pair count: none */
        0x01,               /* lighting 1 */
        0x10,               /* weather 16 */
        0x08,               /* mapType 8 (INSIDE) */
    };
    CHECK(mmo_game_read_load_map(nds, sizeof nds, &lm) == 0, "NDS LoadMap decodes");
    CHECK(lm.is_nds && lm.region_id == 4 && lm.bank_id == 1 && lm.map_id == 2,
          "NDS LoadMap region/bank/map, NDS branch");
    CHECK(lm.lighting == 1 && lm.weather == 16 && lm.map_type == 8, "NDS scene triple");
    CHECK(lm.width == 0 && lm.connection_count == 0, "NDS map carries no grid or connections");
    CHECK(lm.nds_pair_count == 0, "an empty pair list is counted as empty");

    /* The same map with two pairs in the list: the scene triple has moved eight
     * bytes down the body, and a reader that took the count for a reserved byte
     * reads the first pair as the scene. */
    static const u8 nds_pairs[] = {
        0x03, 0x04, 0x01, 0x02, 0x00,
        0x2c,0x01,          /* unnamed S16LE = 300 */
        0x02,               /* pair count: two */
        0x11,0x00, 0x22,0x00,
        0x33,0x00, 0x44,0x00,
        0x01,               /* lighting 1 */
        0x10,               /* weather 16 */
        0x08,               /* mapType 8 (INSIDE) */
    };
    CHECK(mmo_game_read_load_map(nds_pairs, sizeof nds_pairs, &lm) == 0,
          "an NDS LoadMap carrying pairs decodes");
    CHECK(lm.nds_unknown == 300 && lm.nds_pair_count == 2, "the halfword and the pair count");
    CHECK(lm.lighting == 1 && lm.weather == 16 && lm.map_type == 8,
          "the scene triple is found past the pair list, not inside it");
    CHECK(mmo_game_read_load_map(nds_pairs, sizeof nds_pairs - 1, &lm) == -1,
          "an NDS body that stops inside the pair list is rejected");

    CHECK(mmo_game_read_load_map(gba, 4, &lm) == -1, "a body shorter than the header is rejected");
}

/* The two weather-delta packets, pinned to their server codecs
 * (MapWeatherModeSetPacketCodec, OverworldWeatherControlPacketCodec). */
static void test_weather_decode(void)
{
    printf("weather deltas decode (server codecs):\n");

    /* MapWeatherModeSet: mode S8, enabled U8. */
    static const u8 mode_on[] = { 0x05, 0x01 };
    int mode = 0, en = -1;
    CHECK(mmo_game_read_map_weather_mode(mode_on, sizeof mode_on, &mode, &en) == 0 &&
          mode == 5 && en == 1, "weather mode 5 enabled");
    static const u8 mode_off[] = { 0xFB, 0x00 };   /* mode -5, disabled */
    CHECK(mmo_game_read_map_weather_mode(mode_off, sizeof mode_off, &mode, &en) == 0 &&
          mode == -5 && en == 0, "weather mode is a signed S8, off");
    CHECK(mmo_game_read_map_weather_mode(mode_on, 1, &mode, &en) == -1,
          "a truncated weather-mode packet is rejected");

    /* OverworldWeatherControl: effectType S8, then a type-tagged variant. */
    int eff = -1;
    static const u8 dur[] = { 0x05, 0x0A,0x00 };   /* type 5: duration S16 = 10 */
    CHECK(mmo_game_read_weather_control(dur, sizeof dur, &eff) == 0 && eff == 5,
          "weather control type 5 (duration variant)");
    static const u8 sky[] = { 0x00, 0x01,0x00, 0x02,0x00 }; /* type 0: sky + variant */
    CHECK(mmo_game_read_weather_control(sky, sizeof sky, &eff) == 0 && eff == 0,
          "weather control type 0 (sky variant)");
    static const u8 bare[] = { 0x07 };             /* an untyped effect: no variant */
    CHECK(mmo_game_read_weather_control(bare, sizeof bare, &eff) == 0 && eff == 7,
          "weather control with no variant payload");
    CHECK(mmo_game_read_weather_control(dur, 2, &eff) == -1,
          "a weather control that stops inside its variant is rejected");
}

#ifndef MMO_REPO_ROOT
#define MMO_REPO_ROOT "."
#endif

static size_t load_fx(const char *rel, u8 *out, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/protocols.game/src/test/resources/fixtures/%s",
             MMO_REPO_ROOT, rel);
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(out, 1, cap, f);
    fclose(f);
    return n;
}

static void test_battle_event_decode(void)
{
    printf("battle-event stream (server and later-client fixtures):\n");

    u8 b[128];
    size_t n;

    n = load_fx("game/s2c/35/return_switch_in.bin", b, sizeof b);
    mmo_battle_switch_in sw;
    CHECK(n == 25 && mmo_game_read_battle_switch_in(b, n, &sw) == 0,
          "a return switch-in is the 21-byte active detail");
    CHECK(sw.full_block == 0 && sw.new_slot == 0 && sw.old_slot == 1
              && sw.species == 495 && sw.level == 6,
          "it names Snivy coming back to slot 0");

    n = load_fx("game/s2c/35/opponent_switch_in.bin", b, sizeof b);
    CHECK(n == 55 && mmo_game_read_battle_switch_in(b, n, &sw) == 0,
          "an opponent switch-in walks the full block");
    CHECK(sw.full_block == 1 && sw.side == 1 && sw.species == 13
              && sw.level == 7 && sw.hp == 22 && sw.max_hp == 22,
          "it is the opponent's Weedle at full hp");

    n = load_fx("game/s2c/33/stat_change_growl.bin", b, sizeof b);
    mmo_battle_move_event mv;
    CHECK(n == 29 && mmo_game_read_battle_move_event(b, n, &mv) == 0,
          "the later-client Growl walks the same way as the official client one");
    CHECK(mv.source_move == 45 && mv.n_targets == 1
              && mv.target[0].sub[0].type == MMO_BATTLE_SUB_STAT
              && mv.target[0].sub[0].stages == -1,
          "attack down one, same sub-event");

    /* The flee terminal: phase -1, empty groups, flag 2. */
    static const u8 fled[] = {
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00,
    };
    mmo_battle_bulk_state bs;
    CHECK(mmo_game_read_battle_bulk_state(fled, sizeof fled, &bs) == 0,
          "a fled bulk state walks");
    CHECK(bs.phase == -1 && bs.flag == 2 && bs.prize == 0,
          "phase -1 is a flee");

    CHECK(mmo_game_read_battle_switch_in(b, 4, &sw) == -1,
          "a switch-in shorter than the active detail is refused");
}

static void test_battle_outcome_decode(void)
{
    printf("catch / forced-switch packets walk the official client's readers:\n");

    static const u8 prompt[3] = { 0x00, 0x00, 0x00 };
    mmo_battle_slot_flag fl;
    CHECK(mmo_game_read_battle_slot_flag(prompt, sizeof prompt, &fl) == 0,
          "a forced-switch prompt walks");
    CHECK(fl.slot == 0 && fl.flag == 0 && fl.immediate == 0,
          "immediate 0 is the party screen");

    static const u8 confirm[3] = { 0x00, 0x00, 0x01 };
    CHECK(mmo_game_read_battle_slot_flag(confirm, sizeof confirm, &fl) == 0,
          "a forced-switch confirm walks");
    CHECK(fl.immediate == 1, "immediate 1 is the pick landing");

    CHECK(mmo_game_read_battle_slot_flag(prompt, 2, &fl) == -1,
          "a short 0x36 is refused");

    /* kind 0, item 5004, sub-kind 4, list-type 1, detail 1, the server's
     * ball throw, which is also the layout ZR1 accepts. */
    static const u8 ball[8] = { 0x00, 0x8c, 0x13, 0x04, 0x01, 0x01, 0x00 };
    mmo_battle_list_event le;
    CHECK(mmo_game_read_battle_list_event(ball, sizeof ball, &le) == 0,
          "a ball throw walks");
    CHECK(le.sub_kind == MMO_BATTLE_LIST_CATCH && le.value == 5004
              && le.have_detail && le.list_type == 1 && le.detail_value == 1,
          "sub-kind 4 carries the Poke Ball and the detail");

    static const u8 other[4] = { 0x00, 0x01, 0x00, 0x00 };
    CHECK(mmo_game_read_battle_list_event(other, sizeof other, &le) == 0,
          "a non-catch list event stops after the sub-kind");
    CHECK(le.sub_kind == 0 && !le.have_detail,
          "any other sub-kind has no detail");

    CHECK(mmo_game_read_battle_list_event(ball, 3, &le) == -1,
          "a short 0x37 is refused");
}

static void test_battle_select_encode(void)
{
    printf("BattleActionSelect tails the official client writer would compose:\n");

    static const u8 sw[] = { 0x00, 0x02, 0x01, 0x00 };
    mmo_battle_select sel;
    mmo_wbuf b;

    memset(&sel, 0, sizeof sel);
    sel.action = MMO_BATTLE_ACTION_SWITCH;
    sel.move_or_item = 1;
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_battle_select(&b, &sel) == 0, "a switch encodes");
    CHECK(b.len == sizeof sw && memcmp(b.data, sw, sizeof sw) == 0,
          "SWITCH is slot, kind, little-endian party index");
    mmo_wbuf_free(&b);

    memset(&sel, 0, sizeof sel);
    sel.action = 15; /* the official client's Bl1: a short tail, no capture, no name here */
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_battle_select(&b, &sel) == -1 && b.len == 0,
          "an unestablished kind writes nothing");
    mmo_wbuf_free(&b);
}

/* The battle-open packet the client reads as a server-driven encounter, pinned to
 * the server's own BattleFieldStatePacketCodec golden fixtures (the same bytes the
 * server test round-trips). Only the header is decoded; wild vs trainer is the
 * OpposingSide byte at a fixed offset. */
static void test_battle_field_state_decode(void)
{
    printf("battle field state decodes wild vs trainer (server fixtures):\n");

    /* 236 bytes: the server's BattleFieldStatePacketCodec golden fixture */
    static const u8 wild[] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0xff,
        0x00, 0x20, 0x00, 0x06, 0x54, 0x00, 0x65, 0x00, 0x73, 0x00, 0x74, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
        0x00, 0x02, 0x4c, 0x03, 0x1a, 0xac, 0x0f, 0x00, 0x03, 0x80, 0x01, 0xa4,
        0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x01,
        0x00, 0xc0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xef, 0x01, 0x06, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x03, 0x01, 0x41, 0x00, 0x21, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x01, 0x00, 0xc0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xf8, 0x01, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x0e,
        0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0x01, 0x32, 0x00, 0x21, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0xef, 0x01, 0x06, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00, 0x66,
        0x66, 0x66, 0x66, 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
        0x00, 0x00, 0x01, 0x00, 0xc0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8,
        0x01, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x0e, 0x00,
        0x00, 0x00, 0x00, 0xff, 0x03, 0x00, 0x01, 0x00, 0x00, 0xf8, 0x01, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00,
        0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00,
    };
    /* 280 bytes: the server's BattleFieldStatePacketCodec golden fixture */
    static const u8 trainer[] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x04,
        0x00, 0x20, 0x00, 0x06, 0x54, 0x00, 0x65, 0x00, 0x73, 0x00, 0x74, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x90, 0x08, 0x4c, 0x92, 0xf6, 0xcf, 0x1a, 0x04,
        0x00, 0x00, 0x4c, 0x03, 0x22, 0x50, 0x0f, 0x00, 0x03, 0x9c, 0x00, 0x30,
        0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x01,
        0x00, 0xc0, 0x08, 0x04, 0x98, 0x35, 0xd0, 0x1a, 0x04, 0x00, 0x0c, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x03, 0x01, 0x42, 0x00, 0xe1, 0x00, 0x2d, 0x00, 0x6c, 0x00, 0x34,
        0x00, 0x00, 0x01, 0x01, 0x00, 0xc0, 0x48, 0x90, 0x61, 0x25, 0xd1, 0x1a,
        0x13, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x11,
        0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0x01, 0x3e, 0x00, 0x21, 0x00, 0x27,
        0x00, 0x62, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x05, 0xc0, 0x08, 0x2e,
        0xf2, 0x25, 0xd1, 0x1a, 0x10, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0x01, 0x4d,
        0x00, 0x21, 0x00, 0x2d, 0x00, 0x1c, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00,
        0x04, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff, 0x00,
        0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x02, 0x06, 0x00, 0x68, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00,
        0xc0, 0xc8, 0xb1, 0xd0, 0x34, 0xd1, 0x1a, 0x0d, 0x00, 0x09, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0xff,
        0x03, 0x00, 0x01, 0x00, 0x00, 0x0d, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66,
        0x00, 0x00, 0x00, 0x00,
    };

    mmo_battle_field_state bf;
    CHECK(mmo_game_read_battle_field_state(wild, sizeof wild, &bf) == 0 &&
          bf.wild == 1 && bf.opposing == MMO_BATTLE_OPPOSING_WILD && bf.background == 0,
          "wild field state decodes as a wild encounter");
    CHECK(mmo_game_read_battle_foe(wild, sizeof wild, &bf) == 0 && bf.foe_species > 0
              && bf.foe_level > 0,
          "wild field state names the opposing monster");
    CHECK(mmo_game_read_battle_field_state(trainer, sizeof trainer, &bf) == 0 &&
          bf.wild == 0 && bf.opposing == MMO_BATTLE_OPPOSING_TRAINER,
          "trainer field state decodes as a trainer battle");

    /* A body too short to hold the header is rejected, not read past. */
    CHECK(mmo_game_read_battle_field_state(wild, 20, &bf) == -1,
          "a truncated battle field state is rejected");

    /*
     * The side byte is reached by walking, so a body the walk cannot cross must trap rather
     * than read offset 23 and call it a side.
     */
    u8 shape[sizeof wild];
    memcpy(shape, wild, sizeof wild);
    shape[18] = 0x01;
    CHECK(mmo_game_read_battle_field_state(shape, sizeof shape, &bf) == -1,
          "a non-empty monster-block list is refused, not walked past");
    memcpy(shape, wild, sizeof wild);
    shape[19] = 0x14;                       /* 0x16 without the side bit */
    CHECK(mmo_game_read_battle_field_state(shape, sizeof shape, &bf) == -1,
          "a presence word with no side bit is refused, not read at a guess");

    /* An unknown OpposingSide byte is rejected rather than silently called a
     * trainer. */
    static u8 weird[24];
    memcpy(weird, wild, sizeof weird);
    weird[23] = 0x07;
    CHECK(mmo_game_read_battle_field_state(weird, sizeof weird, &bf) == -1,
          "an unknown opposing-side byte is rejected");
}

static void test_pokemon_container_decode(void)
{
    printf("the party container decodes the way the game client reads it:\n");

    /*
     * Two monster records the server sent, of different lengths, the trainer names differ, 
     * so a walk that ends one byte out reads the second as rubbish.
     */
    static const u8 recA[] = {
        0x00, 0xc0, 0x0a, 0x0f, 0xef, 0x29, 0x01, 0x20, 0x00, 0x00, 0x00, 0x90,
        0x09, 0x52, 0x1a, 0x29, 0x01, 0x20, 0x00, 0x90, 0x09, 0x52, 0x1a, 0x29,
        0x01, 0x20, 0x01, 0x00, 0x00, 0x01, 0x00, 0xb7, 0xd3, 0x3b, 0x82, 0x00,
        0x90, 0x09, 0x52, 0x1a, 0x29, 0x01, 0x20, 0x50, 0x00, 0x6c, 0x00, 0x61,
        0x00, 0x79, 0x00, 0x65, 0x00, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x07, 0x18, 0x00, 0x00, 0x00, 0xfb, 0x00, 0x00, 0x00, 0x00, 0x32,
        0x00, 0x21, 0x00, 0x2d, 0x00, 0x49, 0x00, 0x00, 0x00, 0x16, 0x28, 0x0a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x05, 0x02,
        0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x96, 0xa7, 0xe5, 0x12, 0x00, 0x00,
        0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x0d, 0x7f,
        0x6a, 0x00, 0x00, 0xff, 0xff, 0x00,
    };
    static const u8 recB[] = {
        0x00, 0xc0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x00, 0xef, 0x01, 0xba, 0x70, 0x7d, 0xdb, 0x00,
        0x90, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x00, 0x65, 0x00, 0x73,
        0x00, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x14, 0x00,
        0x00, 0x00, 0xa5, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x21, 0x00, 0x2b,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x05, 0x02, 0xff, 0xff, 0xff, 0xff,
        0x03, 0x00, 0xef, 0xbd, 0xf7, 0x1e, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x86, 0x53, 0x6a, 0x00, 0x00, 0xff,
        0xff, 0x00,
    };

    u8 body[8 + sizeof recA + sizeof recB];
    mmo_pokemon_container ct;
    mmo_monster mons[6];

    /* One party member: the shape every join carries. */
    body[0] = MMO_CONTAINER_PARTY;
    body[1] = 0x01;                         /* hasChange */
    body[2] = 0x01;                         /* one record */
    memcpy(body + 3, recA, sizeof recA);
    size_t n = 3 + sizeof recA;
    CHECK(mmo_game_read_pokemon_container(body, n, &ct, mons, 6) == 0,
          "a one-member party container decodes");
    CHECK(ct.container == MMO_CONTAINER_PARTY && ct.has_change && !ct.deleted,
          "the container is the party, replacing what was there");
    CHECK(ct.total == 1 && ct.count == 1, "one record, one stored");
    CHECK(ct.trailing == 0, "the record ends exactly where the client stops");
    CHECK(mons[0].dex_id == 1 && mons[0].level == 7 && mons[0].hp == 24,
          "species, level and HP are the server's");
    CHECK(strcmp(mons[0].ot, "Player") == 0 && mons[0].nickname[0] == '\0',
          "the trainer name reads, and an unnicknamed monster is empty");
    CHECK(mons[0].move_id[0] == 33 && mons[0].move_pp[0] == 22 &&
          mons[0].move_id[3] == 0, "four move slots, empty ones included");
    CHECK(mons[0].container == MMO_CONTAINER_PARTY && mons[0].slot == 0,
          "the record names its own container and slot");

    /* Two records back to back. The second is a different length, so it only
     * decodes if the first ended where the client ends it. */
    body[2] = 0x02;
    memcpy(body + 3 + sizeof recA, recB, sizeof recB);
    n = 3 + sizeof recA + sizeof recB;
    CHECK(mmo_game_read_pokemon_container(body, n, &ct, mons, 6) == 0,
          "a two-member party container decodes");
    CHECK(ct.count == 2 && ct.trailing == 0, "both records, nothing left over");
    CHECK(mons[1].dex_id == 495 && mons[1].level == 5 && mons[1].hp == 20 &&
          strcmp(mons[1].ot, "Test") == 0,
          "the second record starts where the first one ended");

    /* One byte too many in the first record, the defect that locked a character
     * out of the game, is visible as a second record read off the wrong offset,
     * not as a clean decode. */
    u8 shifted[sizeof body + 1];
    memcpy(shifted, body, 3 + sizeof recA);
    shifted[3 + sizeof recA] = 0x00;
    memcpy(shifted + 4 + sizeof recA, recB, sizeof recB);
    mmo_monster bad[6];
    int rc = mmo_game_read_pokemon_container(shifted, sizeof shifted, &ct, bad, 6);
    CHECK(rc == -1, "a record written one byte long is refused, not decoded "
                    "into a party that looks right");

    /* The third flag bit gates a halfword the client reads and never uses. No
     * capture sets it; the reader still has to cross it, or every record after it
     * lands two bytes early. */
    u8 worded[3 + 2 + sizeof recA];
    worded[0] = MMO_CONTAINER_PARTY;
    worded[1] = 0x01 | 0x04;
    worded[2] = 0x34;
    worded[3] = 0x12;
    worded[4] = 0x01;
    memcpy(worded + 5, recA, sizeof recA);
    CHECK(mmo_game_read_pokemon_container(worded, sizeof worded, &ct, mons, 6) == 0,
          "the flagged halfword is crossed, not read as a record");
    CHECK(ct.has_unknown_word && ct.unknown_word == 0x1234,
          "the halfword is carried verbatim");
    CHECK(ct.count == 1 && ct.trailing == 0 && mons[0].dex_id == 1,
          "the record after it still lands");

    /* A delete carries no count and no records: the client drops the container
     * and stops reading there. */
    u8 del[2] = { MMO_CONTAINER_PARTY, 0x01 | 0x02 };
    CHECK(mmo_game_read_pokemon_container(del, sizeof del, &ct, mons, 6) == 0 &&
          ct.deleted && ct.total == 0,
          "a delete empties the container and reads nothing after the flags");

    /*
     * The record's own trailing list is sized from a signed byte on the client side, so 0x80
     * is a negative array size there, the exception that leaves a character stuck on the
     * loading screen. A body that would do it is refused here.
     */
    body[2] = 0x01;
    u8 tailed[3 + sizeof recA];
    memcpy(tailed, body, sizeof tailed);
    tailed[sizeof tailed - 1] = 0x80;       /* the record's trailing list length */
    CHECK(mmo_game_read_pokemon_container(tailed, sizeof tailed, &ct, mons, 6) == -1,
          "a trailing list length above 127 is refused");

    /* A body that runs out mid-record is a failure, not a short party. */
    CHECK(mmo_game_read_pokemon_container(body, 40, &ct, mons, 6) == -1,
          "a truncated container is refused");

    /* 128 records: above what a signed byte holds and well within what the
     * client's own `new gT0[get() & 255]` sizes. */
    size_t big_n = 3 + 128 * sizeof recA;
    u8 *big = malloc(big_n);
    CHECK(big != NULL, "the 128-record container allocates");
    if (big) {
        big[0] = MMO_CONTAINER_PARTY;
        big[1] = 0x01;
        big[2] = 128;
        for (int i = 0; i < 128; i++)
            memcpy(big + 3 + (size_t)i * sizeof recA, recA, sizeof recA);
        CHECK(mmo_game_read_pokemon_container(big, big_n, &ct, mons, 6) == 0 &&
              ct.total == 128 && ct.count == 6 && ct.trailing == 0,
              "a container of 128 records decodes, storing what fits");
        free(big);
    }
}

static void test_monster_detail_fields(void)
{
    printf("the record's detail fields are the ones the game client reads:\n");

    /* The same one-member party the walk above decodes, so the offsets these
     * assertions land on are the ones that test already pins. */
    static const u8 recA[] = {
        0x00, 0xc0, 0x0a, 0x0f, 0xef, 0x29, 0x01, 0x20, 0x00, 0x00, 0x00, 0x90,
        0x09, 0x52, 0x1a, 0x29, 0x01, 0x20, 0x00, 0x90, 0x09, 0x52, 0x1a, 0x29,
        0x01, 0x20, 0x01, 0x00, 0x00, 0x01, 0x00, 0xb7, 0xd3, 0x3b, 0x82, 0x00,
        0x90, 0x09, 0x52, 0x1a, 0x29, 0x01, 0x20, 0x50, 0x00, 0x6c, 0x00, 0x61,
        0x00, 0x79, 0x00, 0x65, 0x00, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x07, 0x18, 0x00, 0x00, 0x00, 0xfb, 0x00, 0x00, 0x00, 0x00, 0x32,
        0x00, 0x21, 0x00, 0x2d, 0x00, 0x49, 0x00, 0x00, 0x00, 0x16, 0x28, 0x0a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x05, 0x02,
        0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x96, 0xa7, 0xe5, 0x12, 0x00, 0x00,
        0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x0d, 0x7f,
        0x6a, 0x00, 0x00, 0xff, 0xff, 0x00,
    };

    u8 body[3 + sizeof recA];
    body[0] = MMO_CONTAINER_PARTY;
    body[1] = 0x01;
    body[2] = 0x01;
    memcpy(body + 3, recA, sizeof recA);

    mmo_pokemon_container ct;
    mmo_monster m[1];
    CHECK(mmo_game_read_pokemon_container(body, sizeof body, &ct, m, 1) == 0 &&
          ct.count == 1, "the record decodes");

    /* The seed's top bit is set, so a nature reduced as a signed value comes out
     * negative and lands on a different nature entirely. The client reduces the
     * seed as unsigned. */
    CHECK(m[0].seed == (s32)0x823bd3b7, "the seed is the record's");
    CHECK(m[0].nature == 3, "the nature is the seed reduced unsigned, mod 25");

    /* Six 5-bit stats, indexed by the client's own stat order: the same order the
     * EVs go on the wire in, and the engine's `enum PokemonStat`. */
    CHECK(m[0].iv_bits == 0x12e5a796, "the IV word is carried whole");
    CHECK(m[0].iv[0] == 22 && m[0].iv[1] == 28 && m[0].iv[2] == 9 &&
          m[0].iv[3] == 11 && m[0].iv[4] == 14 && m[0].iv[5] == 9,
          "the IVs unpack hp/atk/def/speed/spAtk/spDef");
    CHECK(m[0].ev[3] == 4 && m[0].ev[0] == 0 && m[0].ev[4] == 0,
          "the EVs are in that same order, this one has four speed");

    CHECK(m[0].friendship == 50, "friendship reads where the summary reads it");
    CHECK(m[0].form == 0, "the forme is the ordinary one");
    CHECK(m[0].ability_slot == 0, "the ability slot is the record's");
    CHECK(m[0].move_pp_up[0] == 0 && m[0].move_pp_up[3] == 0,
          "no PP Ups on any slot");

    /* Two bits per slot, low slot first. */
    u8 upped[sizeof body];
    memcpy(upped, body, sizeof body);
    upped[3 + 70] = 0x39;                   /* 00 11 10 01 */
    CHECK(mmo_game_read_pokemon_container(upped, sizeof upped, &ct, m, 1) == 0 &&
          m[0].move_pp_up[0] == 1 && m[0].move_pp_up[1] == 2 &&
          m[0].move_pp_up[2] == 3 && m[0].move_pp_up[3] == 0,
          "PP Ups are two bits per move slot");

    /* A hidden ability slot outside the containers that deal in battle monsters
     * is only kept when the rarity word says the monster has one, the client
     * reads it back as slot 0 otherwise, and a party that did not would draw an
     * ability the engine has no name for. */
    u8 hidden[sizeof body];
    memcpy(hidden, body, sizeof body);
    hidden[3 + 118] = 2;
    CHECK(mmo_game_read_pokemon_container(hidden, sizeof hidden, &ct, m, 1) == 0 &&
          m[0].ability_slot == 0,
          "a hidden slot without the hidden-ability bit reads back as slot 0");
    hidden[3 + 127] = MMO_RARITY_HIDDEN_ABILITY;
    CHECK(mmo_game_read_pokemon_container(hidden, sizeof hidden, &ct, m, 1) == 0 &&
          m[0].ability_slot == MMO_ABILITY_SLOT_HIDDEN &&
          (m[0].rarity & MMO_RARITY_HIDDEN_ABILITY),
          "with the bit set the hidden slot stands");

    /*
     * The contest half of the record. Six of the nine bytes after the EVs have names: the game
     * client's own reader keeps five of them, indexed by its contest-type enum, and discards
     * the sixth, which is where Gen 4 puts sheen.
     */
    CHECK(recA[105] == 4 && recA[106] == 5 && recA[107] == 2,
          "the three unnamed bytes sit where this reading says they do");
    CHECK(m[0].cond[0] == 0 && m[0].cond[4] == 0 && m[0].sheen == 0,
          "an unentered monster has no conditions and no sheen");

    u8 conds[sizeof body];
    memcpy(conds, body, sizeof body);
    for (int i = 0; i < MMO_MON_CONDITIONS; i++)
        conds[3 + 99 + i] = (u8)(10 * (i + 1));
    conds[3 + 104] = 200;
    CHECK(mmo_game_read_pokemon_container(conds, sizeof conds, &ct, m, 1) == 0 &&
          m[0].cond[0] == 10 && m[0].cond[1] == 20 && m[0].cond[2] == 30 &&
          m[0].cond[3] == 40 && m[0].cond[4] == 50 && m[0].sheen == 200,
          "the five conditions read in contest-type order, then sheen");
    CHECK(m[0].form == 0 && m[0].iv_bits == 0x12e5a796,
          "and the fields after them are still where they were");

    /* The ribbon mask has no field of its own, so it rides the record's
     * trailing list as a tag/length/value entry (game.h). Cool at Great rank is
     * type 0 rank 1, and Tough at Master is type 4 rank 3. */
    static const u8 ribbon_tail[] = {
        MMO_MON_TLV_RIBBONS_SUPER, 8,
        0x02, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    u8 ribboned[sizeof body + sizeof ribbon_tail];
    memcpy(ribboned, body, sizeof body);
    ribboned[sizeof body - 1] = (u8)sizeof ribbon_tail;
    memcpy(ribboned + sizeof body, ribbon_tail, sizeof ribbon_tail);
    CHECK(mmo_game_read_pokemon_container(ribboned, sizeof ribboned, &ct, m, 1) == 0 &&
          ct.trailing == 0,
          "a record carrying a ribbon entry still ends where the client ends it");
    CHECK(m[0].ribbons_super == (MMO_MON_RIBBON_BIT(0, 1) | MMO_MON_RIBBON_BIT(4, 3)),
          "the ribbon mask reads back as the bits that were set");

    /* A tag this build does not know is stepped over by its length, so a newer
     * server can add one without stranding an older client, and the entry
     * after it still reads. */
    static const u8 mixed_tail[] = {
        99, 3, 0xAA, 0xBB, 0xCC,
        MMO_MON_TLV_RIBBONS_SUPER, 8,
        0x02, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    u8 mixed[sizeof body + sizeof mixed_tail];
    memcpy(mixed, body, sizeof body);
    mixed[sizeof body - 1] = (u8)sizeof mixed_tail;
    memcpy(mixed + sizeof body, mixed_tail, sizeof mixed_tail);
    CHECK(mmo_game_read_pokemon_container(mixed, sizeof mixed, &ct, m, 1) == 0 &&
          ct.trailing == 0 &&
          m[0].ribbons_super == (MMO_MON_RIBBON_BIT(0, 1) | MMO_MON_RIBBON_BIT(4, 3)),
          "an unknown entry is crossed by its length, not read as the end");

    /* An entry that claims more than the list holds is a malformed record, not a
     * short read to shrug at: the next record in the container would start at
     * the wrong offset, which is the failure the whole walk exists to catch. */
    u8 lying[sizeof body + 3];
    memcpy(lying, body, sizeof body);
    lying[sizeof body - 1] = 3;
    lying[sizeof body + 0] = MMO_MON_TLV_RIBBONS_SUPER;
    lying[sizeof body + 1] = 8;             /* eight bytes, one byte left */
    lying[sizeof body + 2] = 0x02;
    CHECK(mmo_game_read_pokemon_container(lying, sizeof lying, &ct, m, 1) == -1,
          "an entry longer than the list it sits in is refused");
}

static void test_pokemon_move_encode(void)
{
    printf("PokemonMovePacket body (the game client's own pair layout):\n");

    /* A deposit: party slot 2 onto PC slot 517. 517 is 0x0205, so the slot's two
     * bytes differ and a big-endian write would not match. */
    static const u8 deposit[7] = { 0x01, 0x01, 0x02, 0x00, 0x00, 0x05, 0x02 };
    mmo_pokemon_move mv = { MMO_CONTAINER_PARTY, 2, MMO_CONTAINER_PC, 517 };
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_pokemon_move(&b, &mv, 1) == 0, "one pair encodes");
    CHECK(b.len == sizeof deposit && memcmp(b.data, deposit, sizeof deposit) == 0,
          "a deposit matches the pair the game client writes");
    mmo_wbuf_free(&b);

    /* A batch is the same pair repeated behind one count. */
    mmo_pokemon_move batch[2] = {
        { MMO_CONTAINER_PC, 0, MMO_CONTAINER_PARTY, 5 },
        { MMO_CONTAINER_PARTY, 5, MMO_CONTAINER_PC, 0 },
    };
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_pokemon_move(&b, batch, 2) == 0, "a two-pair batch encodes");
    CHECK(b.len == 1 + 2 * 6, "the batch is one count byte and six bytes a pair");
    CHECK(b.len > 0 && b.data[0] == 2, "the count is the pair count");
    mmo_wbuf_free(&b);

    /* The batches the game client will not send. */
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_pokemon_move(&b, batch, 0) == -1, "an empty batch is refused");
    CHECK(mmo_game_write_pokemon_move(&b, batch, MMO_POKEMON_MOVE_MAX + 1) == -1,
          "a batch past the count byte's own limit is refused");
    mmo_pokemon_move noop = { MMO_CONTAINER_PC, 3, MMO_CONTAINER_PC, 3 };
    CHECK(mmo_game_write_pokemon_move(&b, &noop, 1) == -1,
          "a pair that lands where it started is refused");
    mmo_pokemon_move negative = { MMO_CONTAINER_PARTY, 0, MMO_CONTAINER_PC, -1 };
    CHECK(mmo_game_write_pokemon_move(&b, &negative, 1) == -1,
          "a negative slot is refused");
    CHECK(b.len == 0, "a refused batch writes nothing");
    mmo_wbuf_free(&b);
}

static void test_move_learn_codec(void)
{
    printf("MoveLearnPrompt/Reply (one move and a signed slot):\n");

    /* Monster 0x1ACEADEF2AC8C000, move 154. The slot is signed: 0xFF is "there
     * was no room for it", the case that asks the player a question. */
    static const u8 offer[11] = { 0x00, 0xC0, 0xC8, 0x2A, 0xEF, 0xAD, 0xCE, 0x1A,
                                  0xFF, 0x9A, 0x00 };
    mmo_move_learn ml;
    CHECK(mmo_game_read_move_learn(offer, sizeof offer, &ml) == 0, "an offer decodes");
    CHECK(ml.monster_id == (s64)0x1ACEADEF2AC8C000LL, "the monster id is a little-endian s64");
    CHECK(ml.slot == MMO_MOVE_LEARN_NO_SLOT, "0xFF is no slot, not slot 255");
    CHECK(ml.move_id == 154, "the move id is a little-endian s16");

    /* The same packet with a slot: the server already put the move there. */
    static const u8 learned[11] = { 0x00, 0xC0, 0xC8, 0x2A, 0xEF, 0xAD, 0xCE, 0x1A,
                                    0x02, 0x9A, 0x00 };
    CHECK(mmo_game_read_move_learn(learned, sizeof learned, &ml) == 0, "a learn decodes");
    CHECK(ml.slot == 2, "a slot byte reads as the slot the move went into");

    CHECK(mmo_game_read_move_learn(offer, sizeof offer - 1, NULL) == -1,
          "a body one byte short is refused");

    /* The answer is the same three fields back. */
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_move_learn_reply(&b, (s64)0x1ACEADEF2AC8C000LL, 1, 154) == 0,
          "an answer encodes");
    static const u8 reply[11] = { 0x00, 0xC0, 0xC8, 0x2A, 0xEF, 0xAD, 0xCE, 0x1A,
                                  0x01, 0x9A, 0x00 };
    CHECK(b.len == sizeof reply && memcmp(b.data, reply, sizeof reply) == 0,
          "the answer matches the layout the game client writes");
    mmo_wbuf_free(&b);

    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_move_learn_reply(&b, 1, MMO_MOVE_LEARN_NO_SLOT, 154) == 0,
          "keeping the moveset encodes");
    CHECK(b.len == 11 && b.data[8] == 0xFF, "keeping it writes -1 in the slot byte");
    mmo_wbuf_free(&b);

    /* The chooser only ever offers the four slots a monster has. */
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_move_learn_reply(&b, 1, MMO_MOVE_SLOTS, 154) == -1,
          "a slot past the moveset is refused");
    CHECK(mmo_game_write_move_learn_reply(&b, 1, -2, 154) == -1,
          "a slot below -1 is refused");
    CHECK(b.len == 0, "a refused answer writes nothing");
    mmo_wbuf_free(&b);
}

static void test_incubator_decode(void)
{
    printf("EggIncubatorSlots (an id it drops and a wear count it keeps):\n");

    static const u8 slots[21] = {
        0x02,
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0xF4, 0x01,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x00
    };
    mmo_incubators in;
    CHECK(mmo_game_read_incubators(slots, sizeof slots, &in) == 0,
          "an incubator list decodes");
    CHECK(in.total == 2 && in.count == 2, "the byte count is the slot count");
    CHECK(in.slot[0].id == (s64)0x1122334455667788LL,
          "the id the game client drops is carried at full width");
    CHECK(in.slot[0].uses_left == MMO_INCUBATOR_USES_FULL,
          "a fresh temporary incubator is 500 uses");
    CHECK(in.slot[1].uses_left == 13, "a worn one is what the server says");
    CHECK(in.trailing == 0, "the reader crosses the body exactly");

    CHECK(mmo_game_read_incubators(slots, sizeof slots - 1, &in) == -1,
          "a slot one byte short is refused");

    static const u8 none[1] = { 0x00 };
    CHECK(mmo_game_read_incubators(none, sizeof none, &in) == 0 && in.count == 0,
          "an empty list decodes");
}

static void test_breeding_codecs(void)
{
    printf("Breeding forecast and the two requests:\n");

    static const u8 forecast[] = {
        /* the two parents */
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x01,                                     /* hasPreview */
        0x19, 0x00,                               /* species 25 */
        0x00,                                     /* form */
        0x02,                                     /* two stat entries */
        0x01, 0xFA, 0x00, 0x01,                   /* guaranteed, item 250, 1 outcome */
        0x1F, 0x00, 0x00, 0x80, 0x3F, 0xED, 0x09, 0x00, 0x00, /* 31 @ 100%, "High pass" */
        0x00, 0x00, 0x00, 0x02,                   /* rolled, no item, 2 outcomes */
        0x05, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, /* 5 @ 50%, no label */
        0x14, 0x00, 0x00, 0x00, 0x3F, 0xF0, 0x09, 0x00, 0x00, /* 20 @ 50%, "Average" */
        0x01, 0x03,                               /* one shininess kind */
        0x02, 0x21, 0x00, 0x2D, 0x00,             /* two move ids … */
        0x03, 0x00,                               /* … then their two sources */
        0x01,                                     /* OT: the player, unmarked */
        0x07, 0x00,                               /* nature 7 */
        0x01,                                     /* the gender may be chosen */
        0x88, 0x13, 0x00, 0x00,                   /* 5000 for the first */
        0x10, 0x27, 0x00, 0x00                    /* 10000 for the second */
    };
    mmo_breeding_forecast f;
    CHECK(mmo_game_read_breeding_forecast(forecast, sizeof forecast, &f) == 0,
          "a forecast decodes");
    CHECK(f.trailing == 0, "the reader crosses the body exactly");
    CHECK(f.parent[0] == (s64)0x0102030405060708LL &&
          f.parent[1] == (s64)0x1122334455667788LL, "both parents come back");
    CHECK(f.has_preview && f.species == 25 && f.form == 0, "the egg's species");
    CHECK(f.stat_count == 2, "one entry per stat, in the record's own stat order");
    CHECK(f.stat[0].guaranteed && f.stat[0].item_id == 250,
          "a guaranteed IV names the item that guaranteed it");
    CHECK(f.stat[0].outcome_count == 1 && f.stat[0].outcome[0].value == 31 &&
          f.stat[0].outcome[0].chance == 1.0f && f.stat[0].outcome[0].label == 2541,
          "its one outcome is a value, a float share and a text id");
    CHECK(!f.stat[1].guaranteed && f.stat[1].item_id == 0 &&
          f.stat[1].outcome_count == 2 && f.stat[1].outcome[1].value == 20 &&
          f.stat[1].outcome[1].chance == 0.5f,
          "a rolled IV lists every outcome it could take");
    CHECK(f.shininess_count == 1 && f.shininess[0] == 3, "the rarity kinds");
    CHECK(f.move_count == 2 && f.move_id[0] == 33 && f.move_id[1] == 45,
          "the move ids come as one run …");
    CHECK(f.move_source[0] == MMO_BREED_MOVE_EGG &&
          f.move_source[1] == MMO_BREED_MOVE_LEVEL_UP,
          "… and their sources as a second, not interleaved");
    CHECK(f.ot == MMO_BREED_OT_SELF_UNMARKED, "the OT the egg would carry");
    CHECK(f.nature == 7, "the nature");
    CHECK(f.gender_selectable && f.gender_cost[0] == 5000 &&
          f.gender_cost[1] == 10000, "what each gender button would cost");

    /* A pairing the server will not breed stops at the flag byte. */
    static const u8 refused[17] = {
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x00
    };
    CHECK(mmo_game_read_breeding_forecast(refused, sizeof refused, &f) == 0,
          "a refused pairing decodes");
    CHECK(!f.has_preview && f.trailing == 0 && f.species == 0,
          "and carries nothing past the flag");

    CHECK(mmo_game_read_breeding_forecast(forecast, sizeof forecast - 1, &f) == -1,
          "a forecast one byte short is refused");

    /* The request: two parents and the gender byte, which is signed. */
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_breeding_assign(&b, 0x0102030405060708LL,
                                         0x1122334455667788LL,
                                         MMO_BREED_GENDER_ANY) == 0,
          "a pairing request encodes");
    static const u8 assign[17] = {
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0xFF
    };
    CHECK(b.len == sizeof assign && memcmp(b.data, assign, sizeof assign) == 0,
          "it matches the layout the game client writes");
    mmo_wbuf_free(&b);

    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_breeding_assign(&b, 1, 2, 2) == -1,
          "a gender past the two buttons is refused");
    CHECK(b.len == 0, "a refused request writes nothing");
    mmo_wbuf_free(&b);

    /* The submit: a session byte, both parents with no count in front of them,
     * the gender and the selected item's key. */
    static const s64 parents[MMO_BREED_PARENTS] = {
        0x0102030405060708LL, 0x1122334455667788LL
    };
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_breeding_submit(&b, 3, parents,
                                         MMO_BREED_GENDER_SECOND,
                                         MMO_BREED_NO_ITEM) == 0,
          "a submit encodes");
    static const u8 submit[19] = {
        0x03,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x01, 0xFF
    };
    CHECK(b.len == sizeof submit && memcmp(b.data, submit, sizeof submit) == 0,
          "the parents ride unprefixed between the session byte and the trailing two");
    mmo_wbuf_free(&b);

    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_breeding_submit(&b, 3, parents, -2,
                                         MMO_BREED_NO_ITEM) == -1,
          "a gender below the three the buttons send is refused");
    mmo_wbuf_free(&b);
}

static void test_evolution_codecs(void)
{
    printf("EvolutionPrompt/Response (a species and whether it can be stopped):\n");

    /* Monster 0x1ACEADEF2AC8C000 evolving into #134, stoppable. */
    static const u8 prompt[11] = { 0x00, 0xC0, 0xC8, 0x2A, 0xEF, 0xAD, 0xCE, 0x1A,
                                   0x86, 0x00, 0x01 };
    mmo_evolution e;
    CHECK(mmo_game_read_evolution(prompt, sizeof prompt, &e) == 0, "a prompt decodes");
    CHECK(e.monster_id == (s64)0x1ACEADEF2AC8C000LL, "the monster id is a little-endian s64");
    CHECK(e.species == 134, "the species is a little-endian s16");
    CHECK(e.cancelable == 1, "1 is a prompt the player may stop");

    /* The game client tests the byte against 1 rather than for non-zero, so
     * anything else hides its cancel button. */
    static const u8 forced[11] = { 0x00, 0xC0, 0xC8, 0x2A, 0xEF, 0xAD, 0xCE, 0x1A,
                                   0x86, 0x00, 0x00 };
    CHECK(mmo_game_read_evolution(forced, sizeof forced, &e) == 0, "a forced one decodes");
    CHECK(e.cancelable == 0, "0 is a prompt the player may not stop");
    static const u8 odd[11] = { 0x00, 0xC0, 0xC8, 0x2A, 0xEF, 0xAD, 0xCE, 0x1A,
                                0x86, 0x00, 0x02 };
    CHECK(mmo_game_read_evolution(odd, sizeof odd, &e) == 0, "a byte that is neither decodes");
    CHECK(e.cancelable == 0, "only 1 reads as cancelable, the way the game client compares it");

    CHECK(mmo_game_read_evolution(prompt, sizeof prompt - 1, NULL) == -1,
          "a body one byte short is refused");

    /* The answer carries the monster and whether it went through. */
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_evolution_reply(&b, (s64)0x1ACEADEF2AC8C000LL, 1) == 0,
          "an answer encodes");
    static const u8 accepted[9] = { 0x00, 0xC0, 0xC8, 0x2A, 0xEF, 0xAD, 0xCE, 0x1A,
                                    0x01 };
    CHECK(b.len == sizeof accepted && memcmp(b.data, accepted, sizeof accepted) == 0,
          "the answer matches the layout the game client writes");
    mmo_wbuf_free(&b);

    mmo_wbuf_init(&b);
    CHECK(mmo_game_write_evolution_reply(&b, (s64)0x1ACEADEF2AC8C000LL, 0) == 0,
          "stopping it encodes");
    CHECK(b.len == 9 && b.data[8] == 0x00, "a stopped evolution writes a zero byte");
    mmo_wbuf_free(&b);
}

static void test_chat_decode(void)
{
    printf("ChatMessage decode (the official client f/lU1):\n");
    mmo_chat ch;

    /* A player line: type NORMAL, sender id 0x1234, "Dawn", English, unknown
     * -1, "hello". */
    static const u8 line[] = {
        0x00,
        0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x44, 0x00, 0x61, 0x00, 0x77, 0x00, 0x6e, 0x00, 0x00, 0x00,
        0x00,
        0xff,
        0x68, 0x00, 0x65, 0x00, 0x6c, 0x00, 0x6c, 0x00, 0x6f, 0x00,
        0x00, 0x00,
    };
    CHECK(mmo_game_read_chat(line, sizeof line, &ch) == 0, "a player line decodes");
    CHECK(ch.type == MMO_CHAT_NORMAL && ch.sender_id == 0x1234 &&
          ch.language == 0 && ch.unknown == -1, "player line header");
    CHECK(strcmp(ch.sender, "Dawn") == 0 && strcmp(ch.text, "hello") == 0,
          "player line sender and text");

    /* System announcements (wire 16) are just the message. */
    static const u8 sys[] = {
        0x10,
        0x68, 0x00, 0x69, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_chat(sys, sizeof sys, &ch) == 0, "a system line decodes");
    CHECK(ch.type == MMO_CHAT_SYSTEM && ch.sender_id == 0 && ch.language == -1,
          "system line has no sender");
    CHECK(strcmp(ch.text, "hi") == 0, "system line text");

    /* A game notice: type 17, empty sender, zero id, English, unknown -1. */
    static const u8 notice[] = {
        0x11,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00,
        0xff,
        0x57, 0x00, 0x65, 0x00, 0x6c, 0x00, 0x63, 0x00, 0x6f, 0x00,
        0x6d, 0x00, 0x65, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_chat(notice, sizeof notice, &ch) == 0,
          "a notice decodes");
    CHECK(ch.type == MMO_CHAT_NOTICE && ch.sender[0] == '\0' &&
          ch.sender_id == 0 && strcmp(ch.text, "Welcome") == 0,
          "notice type, empty sender, text");

    CHECK(mmo_game_read_chat(line, 1, &ch) == -1, "a truncated line is refused");

    mmo_wbuf w;
    mmo_wbuf_init(&w);
    mmo_game_write_whisper(&w, "Dawn", "hi");
    CHECK(!w.err && w.len == 17 && w.data[0] == MMO_CHAT_WHISPER
              && w.data[1] == 'D' && w.data[9] == 0 && w.data[10] == 0
              && w.data[11] == 'h' && w.data[13] == 'i',
          "a whisper is mode 4, target, then message");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_chat_mode(&w, MMO_CHAT_TRADE, "wts");
    CHECK(!w.err && w.len == 9 && w.data[0] == MMO_CHAT_TRADE
              && w.data[1] == 'w' && w.data[3] == 't' && w.data[5] == 's'
              && w.data[7] == 0 && w.data[8] == 0,
          "a channel line is its mode byte, then the line as the target");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_chat(&w, "hi");
    CHECK(!w.err && w.data[0] == MMO_CHAT_NORMAL,
          "the plain writer is the local channel");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_battle_chat(&w, "gg");
    CHECK(!w.err && w.len == 7 && w.data[0] == 0 && w.data[1] == 'g'
              && w.data[3] == 'g' && w.data[5] == 0 && w.data[6] == 0,
          "a battle line is slot 0, then the line");
    mmo_wbuf_free(&w);
}

static void test_trade_codecs(void)
{
    printf("the direct trade's three small bodies:\n");

    mmo_wbuf w;
    mmo_wbuf_init(&w);
    mmo_game_write_trade_action(&w, MMO_TRADE_ACTION_CONFIRM);
    CHECK(!w.err && w.len == 1 && w.data[0] == 0x02,
          "a trade action is its one byte");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_trade_select(&w, 3);
    static const u8 sel[4] = { 0x03, 0x00, 0x00, 0x00 };
    CHECK(!w.err && w.len == 4 && memcmp(w.data, sel, 4) == 0,
          "a pick is its slot as a little-endian word");
    mmo_wbuf_free(&w);

    static const u8 open_state[] = {
        0x01, 0x01, 0x01, 'B', 0x00, 'l', 0x00, 'u', 0x00, 'e', 0x00,
        0x00, 0x00,
    };
    mmo_trade_state st;
    CHECK(mmo_game_read_trade_state(open_state, sizeof open_state, &st) == 0 &&
              st.state == MMO_TRADE_STATE_OPEN && st.role == 1 &&
              st.peer_gender == 1 && strcmp(st.peer, "Blue") == 0,
          "an OPEN carries the chair, the peer's gender and name");

    static const u8 gone[] = { 0x03, 0x00, 0x00, 0x00, 0x00 };
    CHECK(mmo_game_read_trade_state(gone, sizeof gone, &st) == 0 &&
              st.state == MMO_TRADE_STATE_CANCELLED && st.peer[0] == '\0',
          "a CANCELLED carries an empty name");

    CHECK(mmo_game_read_trade_state(open_state, 2, &st) == -1,
          "a truncated state body is refused");

    /* The scene's relayed traffic: channel, command, counted bytes. */
    mmo_wbuf w2;
    mmo_wbuf_init(&w2);
    static const u8 blob[3] = { 0xAA, 0xBB, 0xCC };
    mmo_game_write_trade_comm(&w2, MMO_TRADE_CHANNEL_COMMAND, 22, blob, 3);
    static const u8 comm_body[] = { 0x00, 0x16, 0x00, 0x03, 0x00,
                                    0xAA, 0xBB, 0xCC };
    CHECK(!w2.err && w2.len == sizeof comm_body &&
              memcmp(w2.data, comm_body, sizeof comm_body) == 0,
          "a relayed command is its id and counted bytes");
    mmo_trade_comm msg;
    CHECK(mmo_game_read_trade_comm(comm_body, sizeof comm_body, &msg) == 0 &&
              msg.channel == MMO_TRADE_CHANNEL_COMMAND && msg.cmd == 22 &&
              msg.len == 3 && msg.data[2] == 0xCC,
          "and reads back whole");
    CHECK(mmo_game_read_trade_comm(comm_body, sizeof comm_body - 1, &msg) ==
              -1,
          "a byte short is refused");
    u8 trailing2[sizeof comm_body + 1];
    memcpy(trailing2, comm_body, sizeof comm_body);
    trailing2[sizeof comm_body] = 0x00;
    CHECK(mmo_game_read_trade_comm(trailing2, sizeof trailing2, &msg) == -1,
          "a trailing byte is refused");
    mmo_wbuf_free(&w2);

    mmo_wbuf_init(&w2);
    mmo_game_write_trade_comm(&w2, MMO_TRADE_CHANNEL_SYNC, 19, NULL, 0);
    static const u8 sync_body[] = { 0x01, 0x13, 0x00, 0x00, 0x00 };
    CHECK(!w2.err && w2.len == sizeof sync_body &&
              memcmp(w2.data, sync_body, sizeof sync_body) == 0,
          "a sync marker is its number and no bytes");
    mmo_wbuf_free(&w2);
}

static void test_gtl_codecs(void)
{
    printf("the trade link's requests and pages:\n");

    mmo_wbuf w;
    mmo_wbuf_init(&w);
    mmo_game_write_gtl_open(&w, 0x0123456789ABCDEFLL);
    static const u8 open_body[9] = {
        0x00, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
    };
    CHECK(!w.err && w.len == 9 && memcmp(w.data, open_body, 9) == 0,
          "the session open is the kind byte and the echoed timestamp");
    mmo_wbuf_free(&w);

    /* An unfiltered own-listings request is the 6-byte header the official
     * capture shows. */
    mmo_wbuf_init(&w);
    mmo_game_write_gtl_search_req(&w, 3, MMO_GTL_KIND_OWN,
                                  MMO_GTL_SORT_NEWEST, 2, NULL);
    static const u8 own_req[6] = { 0x03, 0x02, 0x00, 0x02, 0x00, 0x00 };
    CHECK(!w.err && w.len == 6 && memcmp(w.data, own_req, 6) == 0,
          "an unfiltered request is the six-byte header");
    mmo_wbuf_free(&w);

    /* The category byte is the sort order on this server. */
    mmo_wbuf_init(&w);
    mmo_game_write_gtl_search_req(&w, 3, MMO_GTL_KIND_ITEM,
                                  MMO_GTL_SORT_PRICE_DESC, 0, NULL);
    CHECK(!w.err && w.len == 6 && w.data[2] == 0x03,
          "the sort order rides the category byte");
    mmo_wbuf_free(&w);

    /* A species and a price ceiling: the two filter shapes (self-sized and
     * fixed) in one list. */
    mmo_gtl_search filter = { 132, -1, -1, -1, -1, -1, 5000 };
    mmo_wbuf_init(&w);
    mmo_game_write_gtl_search_req(&w, 1, MMO_GTL_KIND_POKEMON,
                                  MMO_GTL_SORT_NEWEST, 0, &filter);
    static const u8 filtered[] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x01, 0x00, 0x84, 0x00,
        0x0a, 0x88, 0x13, 0x00, 0x00,
    };
    CHECK(!w.err && w.len == sizeof filtered &&
              memcmp(w.data, filtered, sizeof filtered) == 0,
          "a species filter self-sizes and a price filter is fixed");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_gtl_create_mon(&w, 0x2001291A52099000LL, 4999);
    static const u8 mon_create[] = {
        0x00, 0x00, 0x90, 0x09, 0x52, 0x1a, 0x29, 0x01, 0x20,
        0x87, 0x13, 0x00, 0x00, 0x01, 0x00,
    };
    CHECK(!w.err && w.len == sizeof mon_create &&
              memcmp(w.data, mon_create, sizeof mon_create) == 0,
          "a monster listing is kind, id, unit price, one unit");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_gtl_create_item(&w, 17, 3, 900);
    static const u8 item_create[] = {
        0x01, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x84, 0x03, 0x00, 0x00, 0x03, 0x00,
    };
    CHECK(!w.err && w.len == sizeof item_create &&
              memcmp(w.data, item_create, sizeof item_create) == 0,
          "an item listing is kind, item id, unit price, quantity");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_gtl_cancel(&w, 12345);
    static const u8 cancel_body[] = {
        '1', 0x00, '2', 0x00, '3', 0x00, '4', 0x00, '5', 0x00, 0x00, 0x00,
    };
    CHECK(!w.err && w.len == sizeof cancel_body &&
              memcmp(w.data, cancel_body, sizeof cancel_body) == 0,
          "a take-back names its listing in decimal");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_gtl_buy(&w, 7, 3);
    CHECK(!w.err && w.len == 10 && w.data[0] == 0x07 && w.data[8] == 0x03,
          "a purchase is the id and the unit count");
    mmo_wbuf_free(&w);

    static const s64 claim_ids[2] = { 7, 9 };
    mmo_wbuf_init(&w);
    mmo_game_write_gtl_claim(&w, claim_ids, 2);
    CHECK(!w.err && w.len == 17 && w.data[0] == 0x02 && w.data[1] == 0x07 &&
              w.data[9] == 0x09,
          "a claim is a counted list of listing ids");
    mmo_wbuf_free(&w);

    mmo_wbuf_init(&w);
    mmo_game_write_gtl_price(&w, 7, 4000);
    CHECK(!w.err && w.len == 12 && w.data[0] == 0x07 && w.data[8] == 0xa0 &&
              w.data[9] == 0x0f,
          "a price change is the id and the new price");
    mmo_wbuf_free(&w);

    /* One shelf verb's answer: code, then the two blanks. */
    static const u8 result_body[] = {
        0x0b, 0x88, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00,
    };
    mmo_gtl_result result;
    CHECK(mmo_game_read_gtl_result(result_body, sizeof result_body,
                                   &result) == 0 &&
              result.code == MMO_GTL_R_CLAIMED && result.a == 5000 &&
              result.b == 2,
          "a result is its code and the two blanks");
    CHECK(mmo_game_read_gtl_result(result_body, sizeof result_body - 1,
                                   &result) == -1,
          "a short result is refused");

    /* The session answer: entry kind, timestamp, counted flag bytes. */
    static const u8 flags_body[] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0xaa, 0xbb, 0xcc,
    };
    mmo_gtl_flags flags;
    CHECK(mmo_game_read_gtl_flags(flags_body, sizeof flags_body, &flags) == 0 &&
              flags.entry_kind == 1 && flags.flag_count == 3,
          "the category flags are counted and crossed");
    CHECK(mmo_game_read_gtl_flags(flags_body, sizeof flags_body - 1, &flags) ==
              -1,
          "a flag byte short is refused");

    /* An item page: one row, one quote. */
    static const u8 item_page[] = {
        0x03, 0x01, 0x00, 0x00, 0x49, 0x00, 0x00, 0x00, 0x01,
        /* row: listing 9, kind item, price 1900, listed 100, expires 200,
         * qty 2, item 1111, state 0 */
        0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x6c, 0x07, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x57, 0x04, 0x00,
        /* one quote: item 1111 at 1900 */
        0x01, 0x57, 0x04, 0x6c, 0x07, 0x00, 0x00,
    };
    mmo_gtl_page page;
    CHECK(mmo_game_read_gtl_page(item_page, sizeof item_page, &page) == 0,
          "an item page with its quote strip decodes");
    CHECK(page.request_id == 3 && page.kind == MMO_GTL_KIND_ITEM &&
              page.total == 0x49 && page.count == 1,
          "the header is the request's echo and the total");
    CHECK(page.rows[0].listing_id == 9 && page.rows[0].price == 1900 &&
              page.rows[0].quantity == 2 && page.rows[0].item_id == 1111,
          "the item row carries id, price, quantity and item");
    CHECK(page.quote_count == 1 && page.quotes[0].item_id == 1111 &&
              page.quotes[0].price == 1900,
          "the quote strip follows an item page");

    u8 trailing[sizeof item_page + 1];
    memcpy(trailing, item_page, sizeof item_page);
    trailing[sizeof item_page] = 0x00;
    CHECK(mmo_game_read_gtl_page(trailing, sizeof trailing, &page) == -1,
          "a trailing byte is refused");

    /* A monster page. The record is the committed 0x13 capture, so the row
     * walk is the party container's walk. */
    static const u8 rec[] = {
        0x00, 0xc0, 0x0a, 0x0f, 0xef, 0x29, 0x01, 0x20, 0x00, 0x00, 0x00, 0x90,
        0x09, 0x52, 0x1a, 0x29, 0x01, 0x20, 0x00, 0x90, 0x09, 0x52, 0x1a, 0x29,
        0x01, 0x20, 0x01, 0x00, 0x00, 0x01, 0x00, 0xb7, 0xd3, 0x3b, 0x82, 0x00,
        0x90, 0x09, 0x52, 0x1a, 0x29, 0x01, 0x20, 0x50, 0x00, 0x6c, 0x00, 0x61,
        0x00, 0x79, 0x00, 0x65, 0x00, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x07, 0x18, 0x00, 0x00, 0x00, 0xfb, 0x00, 0x00, 0x00, 0x00, 0x32,
        0x00, 0x21, 0x00, 0x2d, 0x00, 0x49, 0x00, 0x00, 0x00, 0x16, 0x28, 0x0a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x05, 0x02,
        0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x96, 0xa7, 0xe5, 0x12, 0x00, 0x00,
        0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x0d, 0x7f,
        0x6a, 0x00, 0x00, 0xff, 0xff, 0x00,
    };
    u8 mon_page[9 + 23 + 1 + sizeof rec + 12];
    u8 *p = mon_page;
    *p++ = 0x01; *p++ = 0x00;                   /* request 1, kind pokemon */
    *p++ = 0x00; *p++ = 0x00;                   /* page 0 */
    *p++ = 0x01; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; /* total 1 */
    *p++ = 0x01;                                /* one row */
    *p++ = 0x2a; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; /* listing 42 */
    *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    *p++ = 0x00;                                /* row kind pokemon */
    *p++ = 0x88; *p++ = 0x13; *p++ = 0x00; *p++ = 0x00; /* price 5000 */
    *p++ = 0x64; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; /* listed 100 */
    *p++ = 0xc8; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; /* expires 200 */
    *p++ = 0x01; *p++ = 0x00;                   /* quantity 1 */
    *p++ = 0x01;                                /* a record follows */
    memcpy(p, rec, sizeof rec);
    p += sizeof rec;
    for (int s = 0; s < 6; s++) {               /* stats 24,20,21,22,23,25 */
        *p++ = (u8)(24 + s);
        *p++ = 0x00;
    }
    CHECK(mmo_game_read_gtl_page(mon_page, (size_t)(p - mon_page), &page) == 0,
          "a monster page walks its record to the last byte");
    CHECK(page.rows[0].listing_id == 42 && page.rows[0].price == 5000 &&
              page.rows[0].have_mon && page.rows[0].mon.dex_id == 1 &&
              page.rows[0].mon.level == 7 &&
              strcmp(page.rows[0].mon.ot, "Player") == 0,
          "the row's monster is the record the party container pins");
    CHECK(page.rows[0].stats[0] == 24 && page.rows[0].stats[5] == 29,
          "the six-stat strip follows the record");
    CHECK(page.quote_count == 0, "no quote strip on a monster page");

    /* An own page's rows carry the standing tail: state, remaining,
     * unclaimed. */
    static const u8 own_page[] = {
        0x05, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        /* the same item row as above... */
        0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x6c, 0x07, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00,
        0x05, 0x00, 0x57, 0x04, 0x00,
        /* ...then the tail: active, 2 up, 3 unclaimed. */
        0x00, 0x02, 0x00, 0x03, 0x00,
    };
    CHECK(mmo_game_read_gtl_page(own_page, sizeof own_page, &page) == 0 &&
              page.kind == MMO_GTL_KIND_OWN &&
              page.rows[0].own_state == MMO_GTL_ST_ACTIVE &&
              page.rows[0].own_remaining == 2 &&
              page.rows[0].own_unclaimed == 3,
          "an own row tells where the listing stands");
    CHECK(mmo_game_read_gtl_page(own_page, sizeof own_page - 1, &page) == -1,
          "an own row without its tail is refused");

    /* The trade log: an empty reply, then a full fill with its moment. */
    static const u8 empty_log[1] = { 0x00 };
    mmo_gtl_log log;
    CHECK(mmo_game_read_gtl_log(empty_log, 1, &log) == 0 && log.count == 0,
          "the captured empty trade log reads as zero rows");
    static const u8 one_log[] = {
        0x01, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x02,
        /* frame 0: item 17, qty 3, $2700, bought */
        0x11, 0x00, 0x03, 0x00, 0x8c, 0x0a, 0x00, 0x00, 0x01,
        /* frame 1: the moment */
        0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00,
    };
    CHECK(mmo_game_read_gtl_log(one_log, sizeof one_log, &log) == 0 &&
              log.count == 1 && log.rows[0].sale_id == 7 &&
              log.rows[0].type == 3 && log.rows[0].what == 17 &&
              log.rows[0].amount == 3 && log.rows[0].total == 2700 &&
              log.rows[0].bought == 1 && log.rows[0].epoch == 100,
          "a fill's two frames land in one log row");
    CHECK(mmo_game_read_gtl_log(one_log, sizeof one_log - 2, &log) == -1,
          "a truncated log is refused");
}

int game_tests_run(void)
{
    failures = 0;
    test_join_encode();
    test_movement_encode();
    test_face_encode();
    test_send_is_uncompressed();
    test_recv_decompresses();
    test_join_response();
    test_select_character_encode();
    test_first_character_id();
    test_create_character_encode();
    test_pick_character();
    test_selected_character();
    test_presence_decode();
    test_world_state_decode();
    test_item_stack_update_decode();
    test_local_character_delta_decode();
    test_shop_catalog_decode();
    test_shop_trade_encode();
    test_dialog_action_decode();
    test_dialog_reply_encode();
    test_entity_interact_encode();
    test_dialog_state_decode();
    test_script_move_decode();
    test_objective_decode();
    test_ui_decode();
    test_sync_decode();
    test_compete_decode();
    test_compete_signup_encode();
    test_gm_decode();
    test_friend_decode();
    test_guild_decode();
    test_mail_decode();
    test_link_decode();
    test_item_use_encode();
    test_load_map_decode();
    test_weather_decode();
    test_battle_field_state_decode();
    test_battle_event_decode();
    test_battle_outcome_decode();
    test_battle_select_encode();
    test_pokemon_container_decode();
    test_monster_detail_fields();
    test_pokemon_move_encode();
    test_move_learn_codec();
    test_incubator_decode();
    test_breeding_codecs();
    test_evolution_codecs();
    test_chat_decode();
    test_trade_codecs();
    test_gtl_codecs();

    if (failures)
        printf("game: %d check(s) FAILED\n", failures);
    else
        printf("game: all checks passed\n");
    return failures;
}
