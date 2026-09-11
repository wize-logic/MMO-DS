/* Game server transport (compression framing) and JoinPacket. See
 * game.h. */
#include "game.h"
#include "idmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void mmo_game_stream_init(mmo_game_stream *g)
{
    mmo_inflate_init(&g->infl);
}

void mmo_game_send(mmo_session_crypto *c,
                   u8 opcode, const u8 *body, size_t bodylen, mmo_wbuf *out)
{
    /* The client never compresses, the server installs no inbound decompressor.
     * So a game packet is just opcode||body through the session envelope. */
    mmo_session_send_app(c, opcode, body, bodylen, out);
}

size_t mmo_game_recv(mmo_session_crypto *c, mmo_game_stream *g,
                     const u8 *payload, size_t n,
                     u8 *opcode_out, u8 *body, size_t cap)
{
    u8 *plain = malloc(n ? n : 1);
    if (!plain)
        return (size_t)-1;
    size_t pn = mmo_session_recv_app(c, payload, n, plain, n);
    if (pn == (size_t)-1 || pn < 2) {
        free(plain);
        return (size_t)-1;
    }

    *opcode_out = plain[0];
    u8 flag = plain[1];
    const u8 *seg = plain + 2;
    size_t seglen = pn - 2;

    size_t out;
    if (flag == 0) {
        if (seglen > cap) { free(plain); return (size_t)-1; }
        if (seglen)
            memcpy(body, seg, seglen);
        out = seglen;
    } else if (flag == 1) {
        out = mmo_inflate_segment(&g->infl, seg, seglen, body, cap);
    } else {
        out = (size_t)-1;                   /* unknown compression flag */
    }
    free(plain);
    return out;
}

/* --- application packets ------------------------------------------------- */

void mmo_game_write_join(mmo_wbuf *body, s32 user_id,
                         const u8 *token, size_t token_len)
{
    static const u8 zero6[6]   = { 0 };
    static const u8 zero32[32] = { 0 };

    /* authData = choose(tag=U8): 0x00 NewAuthData. Tag 0x01 (reconnect) is
     * not written; game.h says what that would need. */
    mmo_put_u8(body, 0x00);
    mmo_put_s32le(body, user_id);
    mmo_put_bytes_u8(body, token, token_len);

    mmo_put_bytes(body, zero6, sizeof zero6);  /* mac: fixedBytes(6) */
    mmo_put_s32le(body, 0);                     /* clientRevision */
    mmo_put_s32le(body, 0);                     /* installationRevision */
    mmo_put_u8(body, 0);                        /* currentChatLanguage (S8) */
    mmo_put_s16le(body, 0);                     /* chatLanguages */
    mmo_put_s16le(body, 0);                     /* matchmakingLanguages */
    mmo_put_u8(body, 0);                        /* romMask (S8) */
    mmo_put_u8(body, 0);                        /* roms: listPrefixed(U8) = 0 */
    mmo_put_u8(body, 0);                        /* clientInfo: listPrefixed(U8) = 0 */
    mmo_put_u8(body, 0x01);                     /* platform: LINUX */
    mmo_put_u8(body, 0x00);                     /* arch: X86 (ordinal 0) */
    mmo_put_u8(body, 0x00);                     /* bitness: _32 (ordinal 0) */
    mmo_put_bytes_u8(body, NULL, 0);            /* unk1: bytesPrefixed(U8) empty */
    mmo_put_bytes(body, zero32, sizeof zero32); /* unk2: fixedBytes(32) */
}

void mmo_game_write_select_character(mmo_wbuf *body, s64 character_id, s64 character_id_hash)
{
    mmo_put_s64le(body, character_id);
    mmo_put_s64le(body, character_id_hash);
}

void mmo_game_write_delete_character(mmo_wbuf *body, s64 character_id)
{
    mmo_put_s64le(body, character_id);
}

int mmo_game_read_delete_result(const u8 *body, size_t n, int *out_result,
                                s64 *out_id)
{
    mmo_rbuf r;
    int result;
    s64 id;

    if (!body)
        return -1;
    mmo_rbuf_init(&r, body, n);
    result = (int)mmo_get_u8(&r);
    id = mmo_get_s64le(&r);
    if (r.err)
        return -1;
    if (out_result)
        *out_result = result;
    if (out_id)
        *out_id = id;
    return 0;
}

int mmo_game_read_transportation(const u8 *body, size_t n, u32 *out_id,
                                 int *out_transportation)
{
    mmo_rbuf r;
    u32 id;
    int mount;

    if (!body)
        return -1;
    mmo_rbuf_init(&r, body, n);
    id = (u32)mmo_get_s64le(&r);
    mount = (int)mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (out_id)
        *out_id = id;
    if (out_transportation)
        *out_transportation = mount;
    return 0;
}

u8 mmo_game_dir_from_ds(int ds_dir)
{
    switch (ds_dir) {
    case 0: return 1;  /* NORTH -> UP    */
    case 1: return 0;  /* SOUTH -> DOWN  */
    case 2: return 2;  /* WEST  -> LEFT  */
    case 3: return 3;  /* EAST  -> RIGHT */
    default: return 0; /* out of range: DOWN */
    }
}

int mmo_game_dir_to_ds(u8 wire_dir)
{
    switch (wire_dir & 0xff) {
    case 1: return 0;  /* UP    -> NORTH */
    case 0: return 1;  /* DOWN  -> SOUTH */
    case 2: return 2;  /* LEFT  -> WEST  */
    case 3: return 3;  /* RIGHT -> EAST  */
    default: return 1; /* out of range: SOUTH (a downward idle) */
    }
}

void mmo_game_write_movement(mmo_wbuf *body, s16 x, s16 y, u8 wire_dir,
                             int running, int tiles)
{
    int span = tiles < 1 ? 1 : tiles > 3 ? 3 : tiles;

    mmo_put_s16le(body, x);
    mmo_put_s16le(body, y);
    mmo_put_u8(body, (u8)((wire_dir & 0x03) | (running ? 0x80 : 0)
                          | ((span - 1) << 2)));
}

void mmo_game_write_face(mmo_wbuf *body, u8 wire_dir)
{
    mmo_put_u8(body, (u8)(wire_dir & 0x03));
}

void mmo_game_write_chat(mmo_wbuf *w, const char *text)
{
    mmo_game_write_chat_mode(w, MMO_CHAT_NORMAL, text);
}

void mmo_game_write_chat_mode(mmo_wbuf *w, int mode, const char *text)
{
    /* ChatMessageSendPacketCodec: mode S8, target UTF-16LE-NUL. Every mode but
     * the whisper's (4, which mmo_game_write_whisper frames) leaves off the
     * separate message field; the server reads `message ?: target`, so the
     * whole line rides in the target. */
    mmo_put_u8(w, (u8)mode);
    mmo_put_utf16_nt(w, text);
}

void mmo_game_write_battle_chat(mmo_wbuf *w, const char *text)
{
    /* BattleChatMessagePacketCodec: channelOrSlot S8, message UTF-16LE-NUL.
     * The server finds the battle from the session, so the byte is 0. */
    mmo_put_u8(w, 0);
    mmo_put_utf16_nt(w, text);
}

void mmo_game_write_whisper(mmo_wbuf *w, const char *name, const char *text)
{
    mmo_put_u8(w, (u8)MMO_CHAT_WHISPER);
    mmo_put_utf16_nt(w, name ? name : "");
    mmo_put_utf16_nt(w, text ? text : "");
}

void mmo_game_write_trade_request(mmo_wbuf *w, const char *name)
{
    mmo_put_utf16_nt(w, name ? name : "");
}

void mmo_game_write_block(mmo_wbuf *w, const char *name, const char *reason)
{
    mmo_put_utf16_nt(w, name ? name : "");
    mmo_put_utf16_nt(w, reason ? reason : "");
}

int mmo_game_read_chat(const u8 *body, size_t n, mmo_chat *out)
{
    mmo_chat sink;
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);
    out->language = -1;
    out->unknown = -1;

    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    out->type = (int)mmo_get_u8(&r);
    if (r.err)
        return -1;

    if (out->type == MMO_CHAT_SYSTEM) {
        mmo_get_utf16_nt(&r, out->text, sizeof out->text);
        return r.err ? -1 : 0;
    }

    out->sender_id = mmo_get_s64le(&r);
    mmo_get_utf16_nt(&r, out->sender, sizeof out->sender);
    out->language = (int)mmo_get_u8(&r);
    out->unknown = (int)(s8)mmo_get_u8(&r);
    mmo_get_utf16_nt(&r, out->text, sizeof out->text);
    return r.err ? -1 : 0;
}

int mmo_game_write_battle_select(mmo_wbuf *w, const mmo_battle_select *sel)
{
    if (!w || !sel) {
        if (w)
            w->err = 1;
        return -1;
    }
    switch (sel->action) {
    case MMO_BATTLE_ACTION_MOVE:
    case MMO_BATTLE_ACTION_ITEM:
    case MMO_BATTLE_ACTION_SWITCH:
    case MMO_BATTLE_ACTION_RUN:
        break;
    default:
        /* Sixteen more kinds exist in the official client's enum. Their tails are not
         * established by a capture, so they are refused rather than guessed. */
        w->err = 1;
        return -1;
    }
    mmo_put_u8(w, sel->slot_ref);
    mmo_put_u8(w, (u8)sel->action);
    switch (sel->action) {
    case MMO_BATTLE_ACTION_MOVE:
        mmo_put_s16le(w, sel->move_or_item);
        mmo_put_u8(w, sel->extra);
        break;
    case MMO_BATTLE_ACTION_ITEM:
        mmo_put_s16le(w, sel->move_or_item);
        mmo_put_s64le(w, sel->target);
        mmo_put_u8(w, sel->extra);
        break;
    case MMO_BATTLE_ACTION_SWITCH:
        mmo_put_s16le(w, sel->move_or_item);
        break;
    case MMO_BATTLE_ACTION_RUN:
        break;
    }
    return w->err ? -1 : 0;
}

void mmo_game_write_battle_run(mmo_wbuf *w)
{
    mmo_battle_select sel;

    memset(&sel, 0, sizeof sel);
    sel.action = MMO_BATTLE_ACTION_RUN;
    mmo_game_write_battle_select(w, &sel);
}

int mmo_game_write_pokemon_move(mmo_wbuf *w, const mmo_pokemon_move *moves,
                                int count)
{
    if (!moves || count <= 0 || count > MMO_POKEMON_MOVE_MAX)
        return -1;
    for (int i = 0; i < count; i++) {
        if (moves[i].from_slot < 0 || moves[i].to_slot < 0)
            return -1;
        /* The game client drops a batch whose pair does not move anything rather
         * than sending a no-op, so a request that names one slot twice is a
         * caller's mistake and is refused here for the same reason. */
        if (moves[i].from_container == moves[i].to_container &&
            moves[i].from_slot == moves[i].to_slot)
            return -1;
    }
    mmo_put_u8(w, (u8)count);
    for (int i = 0; i < count; i++) {
        mmo_put_u8(w, moves[i].from_container);
        mmo_put_s16le(w, moves[i].from_slot);
        mmo_put_u8(w, moves[i].to_container);
        mmo_put_s16le(w, moves[i].to_slot);
    }
    return 0;
}

int mmo_game_read_move_learn(const u8 *body, size_t n, mmo_move_learn *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    s64 monster_id = mmo_get_s64le(&r);
    /* The slot is signed: -1 is "there was no room", not slot 255. */
    int slot = (s8)mmo_get_u8(&r);
    int move_id = mmo_get_s16le(&r);
    if (r.err)
        return -1;
    if (out) {
        out->monster_id = monster_id;
        out->slot = slot;
        out->move_id = move_id;
    }
    return 0;
}

int mmo_game_write_move_learn_reply(mmo_wbuf *w, s64 monster_id, int slot,
                                    int move_id)
{
    if (slot != MMO_MOVE_LEARN_NO_SLOT && (slot < 0 || slot >= MMO_MOVE_SLOTS))
        return -1;
    mmo_put_s64le(w, monster_id);
    mmo_put_u8(w, (u8)(s8)slot);
    mmo_put_s16le(w, (s16)move_id);
    return 0;
}

int mmo_game_read_evolution(const u8 *body, size_t n, mmo_evolution *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    s64 monster_id = mmo_get_s64le(&r);
    int species = mmo_get_s16le(&r);
    /* The game client compares the byte against 1 rather than testing it, so a
     * value that is neither 0 nor 1 reads as "not cancelable" there too. */
    int cancelable = mmo_get_u8(&r) == 1;
    if (r.err)
        return -1;
    if (out) {
        out->monster_id = monster_id;
        out->species = species;
        out->cancelable = cancelable;
    }
    return 0;
}

int mmo_game_write_evolution_reply(mmo_wbuf *w, s64 monster_id, int accepted)
{
    mmo_put_s64le(w, monster_id);
    mmo_put_u8(w, accepted ? 1 : 0);
    return 0;
}

int mmo_game_read_incubators(const u8 *body, size_t n, mmo_incubators *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    int total = mmo_get_u8(&r);
    if (r.err)
        return -1;
    out->total = total;
    for (int i = 0; i < total; i++) {
        /* The game client reads the id at full width and throws it away, so it is
         * kept here rather than named. */
        s64 id = mmo_get_s64le(&r);
        int uses = mmo_get_s16le(&r);
        if (r.err)
            return -1;
        if (out->count < MMO_INCUBATOR_SLOTS_MAX) {
            out->slot[out->count].id = id;
            out->slot[out->count].uses_left = uses;
            out->count++;
        }
    }
    out->trailing = (int)mmo_rbuf_remaining(&r);
    return 0;
}

int mmo_game_read_breeding_forecast(const u8 *body, size_t n,
                                    mmo_breeding_forecast *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->parent[0] = mmo_get_s64le(&r);
    out->parent[1] = mmo_get_s64le(&r);
    out->has_preview = mmo_get_u8(&r) == 1;
    if (r.err)
        return -1;
    if (!out->has_preview) {
        /* A pairing the server will not breed stops here: the game client draws
         * "these two can not be bred together" and reads nothing further. */
        out->trailing = (int)mmo_rbuf_remaining(&r);
        return 0;
    }

    out->species = mmo_get_s16le(&r);
    out->form = (s8)mmo_get_u8(&r);

    int stats = mmo_get_u8(&r);
    if (r.err)
        return -1;
    out->stat_total = stats;
    for (int i = 0; i < stats; i++) {
        mmo_breed_stat st;
        memset(&st, 0, sizeof st);
        st.guaranteed = mmo_get_u8(&r) == 1;
        st.item_id = mmo_get_s16le(&r);
        int outcomes = mmo_get_u8(&r);
        if (r.err)
            return -1;
        st.outcome_total = outcomes;
        for (int j = 0; j < outcomes; j++) {
            int value = (s8)mmo_get_u8(&r);
            u32 bits = mmo_get_u32le(&r);
            s32 label = mmo_get_s32le(&r);
            if (r.err)
                return -1;
            if (st.outcome_count < MMO_BREED_CONTRIB_MAX) {
                float chance;
                memcpy(&chance, &bits, sizeof chance);
                st.outcome[st.outcome_count].value = value;
                st.outcome[st.outcome_count].chance = chance;
                st.outcome[st.outcome_count].label = label;
                st.outcome_count++;
            }
        }
        if (out->stat_count < MMO_BREED_STATS_MAX)
            out->stat[out->stat_count++] = st;
    }

    int shinies = mmo_get_u8(&r);
    if (r.err)
        return -1;
    out->shininess_total = shinies;
    for (int i = 0; i < shinies; i++) {
        int kind = (s8)mmo_get_u8(&r);
        if (r.err)
            return -1;
        if (out->shininess_count < MMO_BREED_SHININESS_MAX)
            out->shininess[out->shininess_count++] = kind;
    }

    /* The moves come as two runs of the same length, not as pairs: every move id
     * first, then every source byte. */
    int moves = mmo_get_u8(&r);
    if (r.err)
        return -1;
    out->move_total = moves;
    for (int i = 0; i < moves; i++) {
        int id = mmo_get_s16le(&r);
        if (r.err)
            return -1;
        if (i < MMO_BREED_MOVES_MAX) {
            out->move_id[i] = id;
            out->move_count = i + 1;
        }
    }
    for (int i = 0; i < moves; i++) {
        int src = (s8)mmo_get_u8(&r);
        if (r.err)
            return -1;
        if (i < MMO_BREED_MOVES_MAX)
            out->move_source[i] = src;
    }

    /* The game client reads this as an index into a three-entry table and falls
     * back to the last entry for anything outside it. */
    int ot = (s8)mmo_get_u8(&r);
    out->ot = (ot >= MMO_BREED_OT_SELF && ot <= MMO_BREED_OT_UNKNOWN)
                  ? ot : MMO_BREED_OT_UNKNOWN;
    out->nature = mmo_get_s16le(&r);
    out->gender_selectable = mmo_get_u8(&r) == 1;
    out->gender_cost[0] = mmo_get_s32le(&r);
    out->gender_cost[1] = mmo_get_s32le(&r);
    if (r.err)
        return -1;
    out->trailing = (int)mmo_rbuf_remaining(&r);
    return 0;
}

int mmo_game_write_breeding_assign(mmo_wbuf *w, s64 own_id, s64 partner_id,
                                   int gender)
{
    if (gender < MMO_BREED_GENDER_ANY || gender > MMO_BREED_GENDER_SECOND)
        return -1;
    mmo_put_s64le(w, own_id);
    mmo_put_s64le(w, partner_id);
    mmo_put_u8(w, (u8)(s8)gender);
    return 0;
}

int mmo_game_write_breeding_submit(mmo_wbuf *w, int session,
                                   const s64 parent[MMO_BREED_PARENTS],
                                   int gender, int item_key)
{
    if (gender < MMO_BREED_GENDER_ANY || gender > MMO_BREED_GENDER_SECOND)
        return -1;
    mmo_put_u8(w, (u8)session);
    /* No count: the array is written straight out and the server takes its length
     * from what is left after the two trailing bytes. */
    for (int i = 0; i < MMO_BREED_PARENTS; i++)
        mmo_put_s64le(w, parent[i]);
    mmo_put_u8(w, (u8)gender);
    mmo_put_u8(w, (u8)(s8)item_key);
    return 0;
}

int mmo_game_read_first_character_id(const u8 *body, size_t n, s64 *out_id)
{
    mmo_character_ref ref;
    if (mmo_game_read_first_character(body, n, &ref) != 0)
        return -1;
    if (ref.count >= 1 && out_id)
        *out_id = ref.id;
    return ref.count;
}

int mmo_game_read_first_character(const u8 *body, size_t n, mmo_character_ref *out)
{
    mmo_character_ref sink;
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);
    out->gender = -1;

    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    int count = mmo_get_u8(&r);
    if (r.err)
        return -1;
    out->count = count;
    if (count < 1)
        return 0;

    out->id = mmo_get_s64le(&r);
    if (r.err)
        return -1;
    if (mmo_rbuf_remaining(&r) == 0)
        return 0;

    mmo_get_utf16_nt(&r, out->name, sizeof out->name);
    if (r.err)
        return -1;

    /* rivalSex sits after namePrefix (UTF-16) and userId (S32). A body that
     * ends before those is still a valid count/id/name read. */
    if (mmo_rbuf_remaining(&r) < 2 + 4 + 1)
        return 0;
    char prefix[8];
    mmo_get_utf16_nt(&r, prefix, sizeof prefix);
    (void)mmo_get_s32le(&r);
    int gender = (int)(s8)mmo_get_u8(&r);
    if (!r.err)
        out->gender = gender;
    return 0;
}

/* Pack one populated slot's type (bits 0-9) and colour (bits 10-15). */
static u16 pack_skin_word(u16 type, u8 color)
{
    return (u16)((type & MMO_SKIN_TYPE_MASK) |
                 (((u16)color & MMO_SKIN_COLOR_MASK) << MMO_SKIN_COLOR_SHIFT));
}

static void write_skin_set(mmo_wbuf *w, const mmo_skin_set *skin)
{
    u16 mask = 0;
    for (int i = 0; i < MMO_SKIN_SLOTS; i++) {
        if (skin->slot[i].present)
            mask = (u16)(mask | (1u << i));
    }
    mmo_put_u8(w, (u8)skin->region_selection_index);
    mmo_put_u8(w, (u8)(mask & 0xff));
    mmo_put_u8(w, (u8)(mask >> 8));
    for (int i = 0; i < MMO_SKIN_SLOTS; i++) {
        if (!skin->slot[i].present)
            continue;
        u16 word = pack_skin_word(skin->slot[i].type, skin->slot[i].color);
        mmo_put_u8(w, (u8)(word & 0xff));
        mmo_put_u8(w, (u8)(word >> 8));
    }
}

int mmo_game_write_create_character(mmo_wbuf *w, const mmo_create_character *in)
{
    if (!w || !in || !in->name)
        return -1;
    if (in->name[0] == '\0' ||
        mmo_utf16_units(in->name) > MMO_CHAR_NAME_MAX)
        return -1;
    if (in->gender != 0 && in->gender != 1)
        return -1;
    if (in->starting_region < 0 || in->starting_region > 255)
        return -1;
    if (in->appearance.region_selection_index < 0 ||
        in->appearance.region_selection_index > 255)
        return -1;
    for (int i = 0; i < MMO_SKIN_SLOTS; i++) {
        if (!in->appearance.slot[i].present)
            continue;
        if (in->appearance.slot[i].type > MMO_SKIN_TYPE_MASK)
            return -1;
        if (in->appearance.slot[i].color > MMO_SKIN_COLOR_MASK)
            return -1;
    }

    mmo_put_utf16_nt(w, in->name);
    mmo_put_u8(w, (u8)in->gender);
    mmo_put_u8(w, (u8)in->starting_region);
    write_skin_set(w, &in->appearance);
    return w->err ? -1 : 0;
}

/*
 * Read a SkinSet: a U8 regionSelectionIndex, a U16LE slot mask, then one U16LE compressed word
 * (type in bits 0-9, colour in bits 10-15) per populated slot.
 */
#define SKIN_SLOT_MASK 0x0FFFu            /* twelve cosmetic slots */
#define SKIN_HAS_OVERRIDES 0x8000u        /* one extra byte per populated slot */
static void read_skin_set(mmo_rbuf *r, mmo_skin_set *out, int *count, int leading)
{
    u16 mask;
    int overrides;
    int i;
    int c = 0;

    memset(out, 0, sizeof *out);
    out->region_selection_index = leading ? (int)mmo_get_u8(r) : 0;
    mask = (u16)(mmo_get_u8(r) | (mmo_get_u8(r) << 8));
    overrides = (mask & SKIN_HAS_OVERRIDES) != 0;
    for (i = 0; i < MMO_SKIN_SLOTS; i++) {
        u16 word;
        u8 ov;

        if ((mask & (u16)(1u << i)) == 0)
            continue;
        word = (u16)(mmo_get_u8(r) | (mmo_get_u8(r) << 8));
        out->slot[i].present = 1;
        out->slot[i].type = (u16)(word & MMO_SKIN_TYPE_MASK);
        out->slot[i].color = (u8)((word >> MMO_SKIN_COLOR_SHIFT) &
                                  MMO_SKIN_COLOR_MASK);
        if (overrides) {
            ov = mmo_get_u8(r);
            if (ov)
                out->slot[i].type = ov;
        }
        c++;
    }
    *count = c;
}

int mmo_game_read_load_entity(const u8 *body, size_t n, mmo_load_entity *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->entity_id = (u32)mmo_get_s64le(&r);
    out->gender = mmo_get_u8(&r);
    read_skin_set(&r, &out->appearance, &out->skin_count, 1);
    out->skin_region = out->appearance.region_selection_index;
    mmo_get_utf16_nt(&r, out->name, sizeof out->name);
    out->region_id = mmo_get_u8(&r);
    out->bank_id = mmo_get_u8(&r);
    out->map_id = mmo_get_u8(&r);
    out->x = mmo_get_s16le(&r);
    out->y = mmo_get_s16le(&r);
    out->z = mmo_get_u8(&r);

    int heading = mmo_get_u8(&r);
    out->facing = heading & 0x03;
    out->facing_flag = (heading & 0x08) != 0;
    out->transportation = mmo_get_u8(&r);
    out->entity_state = mmo_get_u8(&r);

    /* The flags byte and its conditional trailer. */
    int flags = mmo_get_u8(&r);
    if (flags & 0x01)
        mmo_get_u8(&r);                   /* S8 */
    if (flags & 0x02) {
        mmo_get_u8(&r);                   /* S8 */
        mmo_get_s16le(&r);                /* U16LE */
    }
    if (flags & 0x04) {
        out->has_follower = 1;
        out->follower_dex = mmo_get_s16le(&r);
    }
    if (flags & 0x08)
        mmo_get_u8(&r);                   /* S8 */
    if (flags & 0x10)
        mmo_get_utf16_nt(&r, out->name_prefix, sizeof out->name_prefix);
    if (flags & 0x20) {
        out->follower_form = mmo_get_u8(&r);
        out->follower_gender = mmo_get_u8(&r);
        out->follower_shiny = mmo_get_u8(&r) != 0;
    }
    return r.err ? -1 : 0;
}

int mmo_game_read_gba_move(const u8 *body, size_t n, mmo_gba_move *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->entity_id = (u32)mmo_get_s64le(&r);
    out->bank_id = mmo_get_u8(&r);
    out->map_id = mmo_get_u8(&r);
    out->x = mmo_get_u8(&r);
    out->y = mmo_get_u8(&r);
    out->movement_mode = mmo_get_u8(&r);
    out->dir_flags = mmo_get_u8(&r);
    out->direction = out->dir_flags & 0x03;
    return r.err ? -1 : 0;
}

int mmo_game_read_entity_move(const u8 *body, size_t n, mmo_entity_move *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->entity_id = (u32)mmo_get_s64le(&r);
    out->x = mmo_get_s16le(&r);
    out->y = mmo_get_s16le(&r);
    out->dir_flags = mmo_get_u8(&r);
    out->direction = out->dir_flags & 0x03;
    return r.err ? -1 : 0;
}

int mmo_game_read_entity_presence(const u8 *body, size_t n,
                                  mmo_entity_presence *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->entity_id = (u32)mmo_get_s64le(&r);
    out->status = mmo_get_u8(&r);
    return r.err ? -1 : 0;
}

int mmo_game_read_entity_snap(const u8 *body, size_t n, mmo_entity_snap *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->entity_id = (u32)mmo_get_s64le(&r);
    out->region_id = mmo_get_u8(&r);
    out->bank_id = mmo_get_u8(&r);
    out->map_id = mmo_get_u8(&r);
    out->x = mmo_get_s16le(&r);
    out->y = mmo_get_s16le(&r);
    out->movement_mode = mmo_get_u8(&r);
    out->dir_flags = mmo_get_u8(&r);
    out->direction = out->dir_flags & 0x03;
    return r.err ? -1 : 0;
}

int mmo_game_read_face_turn(const u8 *body, size_t n, u32 *out_id, int *out_facing)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    u32 id = (u32)mmo_get_s64le(&r);
    int facing = (s8)mmo_get_u8(&r);      /* signed: -1 means "face the player" */
    if (r.err)
        return -1;
    *out_id = id;
    *out_facing = facing;
    return 0;
}

/* Regions the server keeps as native DS maps rather than GBA tile blocks
 * (isNdsRegion, common/utils/Regions.kt): an NDS LoadMap body is just the scene
 * triple, a GBA one the full tile block. */
static int load_map_is_nds_region(int region_id)
{
    return region_id == 2 || region_id == 3 || region_id == 4 || region_id == 10;
}

int mmo_game_read_load_map(const u8 *body, size_t n, mmo_load_map *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    int flags = mmo_get_u8(&r);           /* deleteCache=1, reloadPlayer=2 */
    out->delete_cache = (flags & 0x01) != 0;
    out->reload_player = (flags & 0x02) != 0;
    out->region_id = mmo_get_u8(&r);
    out->bank_id = mmo_get_u8(&r);
    out->map_id = mmo_get_u8(&r);
    mmo_get_u8(&r);                        /* reserved(S8=0) */

    out->is_nds = load_map_is_nds_region(out->region_id);
    if (out->is_nds) {
        /* NdsMapData: an unnamed halfword, then a U8-prefixed list of halfword
         * pairs, then lighting/weather/mapType. The count is a count, an empty
         * list is the only shape that has ever been sent, which is why it read as
         * a second reserved byte. */
        out->nds_unknown = mmo_get_s16le(&r);
        int pairs = mmo_get_u8(&r);
        out->nds_pair_count = pairs;
        for (int i = 0; i < pairs; i++) {
            mmo_get_u16le(&r);
            mmo_get_u16le(&r);
        }
        out->lighting = mmo_get_u8(&r);
        out->weather = mmo_get_u8(&r);
        out->map_type = mmo_get_u8(&r);
        return r.err ? -1 : 0;
    }

    /* GbaMapData: dimensions, then the scene, the border tiles, an optional gzip
     * blob, the connection list and an optional trailer. Everything past the
     * scene is consumed to keep the stream framed; only the grid, encounter type
     * and connections are surfaced (a seamless crossing needs the last two). */
    out->width = mmo_get_s32le(&r);
    out->height = mmo_get_s32le(&r);
    mmo_get_s32le(&r);                    /* paletteIdx1 */
    mmo_get_s32le(&r);                    /* paletteIdx2 */
    int border_w = mmo_get_u8(&r);
    int border_h = mmo_get_u8(&r);
    mmo_get_s16le(&r);                    /* unknownShort */
    mmo_get_u8(&r);                       /* unknownByte */
    out->lighting = mmo_get_u8(&r);
    out->weather = mmo_get_u8(&r);
    out->map_type = mmo_get_u8(&r);
    out->encounter_type = mmo_get_u8(&r);
    for (int i = 0; i < border_w * border_h; i++)
        mmo_get_u16le(&r);               /* Tile2D each */
    if (mmo_get_bool(&r)) {              /* optional compressed inner blob */
        s32 sz = mmo_get_s32le(&r);
        for (s32 i = 0; i < sz; i++)
            mmo_get_u8(&r);
    }
    int conns = mmo_get_u8(&r);          /* GbaConnectionListPrefixedU8 */
    out->connection_total = conns;
    for (int i = 0; i < conns; i++) {
        /* ConnectionDirectionCodec writes ordinal+1; normalise back to 0..3. */
        int dir = mmo_get_u8(&r) - 1;
        int off = mmo_get_s32le(&r);
        int tbank = mmo_get_u8(&r);
        int tmap = mmo_get_u8(&r);
        if (i < MMO_MAP_MAX_CONNECTIONS) {
            out->connections[i].direction = dir;
            out->connections[i].offset = off;
            out->connections[i].target_bank = tbank;
            out->connections[i].target_map = tmap;
            out->connection_count = i + 1;
        }
    }
    if (mmo_get_bool(&r)) {              /* optional trailer */
        mmo_get_s64le(&r);
        char sink[2];
        mmo_get_utf16_nt(&r, sink, sizeof sink);
    }
    return r.err ? -1 : 0;
}

int mmo_game_read_map_weather_mode(const u8 *body, size_t n,
                                   int *out_mode, int *out_enabled)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    int mode = (s8)mmo_get_u8(&r);       /* S8 on the wire */
    int enabled = mmo_get_u8(&r) == 1;
    if (r.err)
        return -1;
    *out_mode = mode;
    *out_enabled = enabled;
    return 0;
}

int mmo_game_read_weather_control(const u8 *body, size_t n, int *out_effect_type)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    int effect = (s8)mmo_get_u8(&r);     /* S8 on the wire */
    /* The variant payload depends on the effect type; consume it so the stream
     * stays framed (OverworldWeatherControlPacketCodec). */
    switch (effect) {
    case 0:
        mmo_get_s16le(&r);               /* skyId */
        mmo_get_s16le(&r);               /* skyVariant */
        break;
    case 4:
        mmo_get_s16le(&r);               /* particleId */
        mmo_get_s16le(&r);               /* particleVariant */
        mmo_get_u32le(&r);               /* paramA (F32LE) */
        mmo_get_u32le(&r);               /* paramB */
        mmo_get_u32le(&r);               /* paramC */
        mmo_get_u32le(&r);               /* paramD */
        mmo_get_s16le(&r);               /* duration */
        break;
    case 5:
        mmo_get_s16le(&r);               /* duration */
        break;
    default:
        break;                           /* no variant payload */
    }
    if (r.err)
        return -1;
    *out_effect_type = effect;
    return 0;
}

int mmo_game_read_entity_leave(const u8 *body, size_t n, u32 *out_id)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    u32 id = (u32)mmo_get_s64le(&r);
    if (r.err)
        return -1;
    *out_id = id;
    return 0;
}

int mmo_game_read_entity_id64(const u8 *body, size_t n, s64 *out_id)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    s64 id = mmo_get_s64le(&r);
    if (r.err)
        return -1;
    *out_id = id;
    return 0;
}

int mmo_game_read_npc_spawn(const u8 *body, size_t n, mmo_npc_spawn *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->entity_id = mmo_get_s64le(&r);
    out->sprite_region = mmo_get_u8(&r);
    out->graphics_id = mmo_get_u16le(&r);
    out->unk3 = mmo_get_u16le(&r);
    out->unk4 = mmo_get_u16le(&r);
    out->region_id = mmo_get_u8(&r);
    out->bank_id = mmo_get_u8(&r);
    out->map_id = mmo_get_u8(&r);
    out->x = mmo_get_u16le(&r);
    out->y = mmo_get_u16le(&r);
    out->unk5 = mmo_get_u8(&r);
    out->facing = mmo_get_u8(&r);
    out->unk6 = mmo_get_u16le(&r);
    out->movement = (out->unk3 >> 8) & 0xff;
    return r.err ? -1 : 0;
}

int mmo_game_read_entity_despawn(const u8 *body, size_t n, u32 *out_id)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    mmo_get_u8(&r);                       /* reserved(byte=0) */
    u32 id = (u32)(u16)mmo_get_s16le(&r);
    if (r.err)
        return -1;
    *out_id = id;
    return 0;
}

/* The side byte is optional: it rides behind a bit in the presence word, and the
 * presence word sits behind two variable-length lists. Walking to it is the only
 * way to read it, its offset is fixed only for the one shape the server sends. */
#define MMO_BATTLE_PRESENT_SIDE 0x02

int mmo_game_read_battle_field_state(const u8 *body, size_t n,
                                     mmo_battle_field_state *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    /* The fixed lead-in: seven bytes, an int and a short. Only the backdrop at
     * offset 5 is named; the rest are enum selectors and ids whose meanings are
     * not established, so they are consumed and not surfaced. */
    for (int i = 0; i < 5; i++)
        mmo_get_u8(&r);
    int background = mmo_get_u8(&r);
    mmo_get_u8(&r);
    mmo_get_s32le(&r);
    mmo_get_s16le(&r);
    mmo_get_u8(&r);                       /* a second side-ish enum, unread */
    mmo_get_u8(&r);
    mmo_get_u8(&r);

    /* The field descriptor: a tag byte and a U8-prefixed list of ten-byte rows. */
    mmo_get_u8(&r);
    int rows = mmo_get_u8(&r);
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < 10; j++)
            mmo_get_u8(&r);

    /* A U8-prefixed list of monster blocks. Nothing here can walk one yet, so a
     * non-empty list traps rather than reading the presence word off a guess. */
    int mons = mmo_get_u8(&r);
    if (r.err || mons != 0)
        return -1;

    s32 present = mmo_get_s32le(&r);
    if (r.err || !(present & MMO_BATTLE_PRESENT_SIDE))
        return -1;                        /* no side byte in this shape */
    int opposing = mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (opposing != MMO_BATTLE_OPPOSING_WILD && opposing != MMO_BATTLE_OPPOSING_TRAINER)
        return -1;

    out->opposing = opposing;
    out->wild = (opposing == MMO_BATTLE_OPPOSING_WILD);
    out->background = background;
    out->foe_species = 0;
    out->foe_level = 0;
    return 0;
}

static void skip_n(mmo_rbuf *r, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        mmo_get_u8(r);
}

static int skip_full_block(mmo_rbuf *r)
{
    int present;

    skip_n(r, 1);          /* slot */
    skip_n(r, 1);          /* constant 1 */
    mmo_get_s64le(r);      /* entity */
    skip_n(r, 2);          /* species */
    skip_n(r, 1);          /* level */
    skip_n(r, 2);          /* pad */
    skip_n(r, 1);          /* gender */
    skip_n(r, 3);          /* pad */
    skip_n(r, 2);          /* current hp */
    skip_n(r, 2);          /* max hp */
    skip_n(r, 5);          /* MOVES_HEADER */
    present = mmo_get_u8(r);
    if (present)
        skip_n(r, 2 + 8);  /* ability + four moves */
    return r->err ? -1 : 0;
}

int mmo_game_read_battle_foe(const u8 *body, size_t n,
                             mmo_battle_field_state *out)
{
    mmo_rbuf r;
    char name[64];
    int party_count, i, tag, opp_count, kind;
    s16 species;
    u8 level;

    if (!out)
        return -1;
    mmo_rbuf_init(&r, body, n);
    skip_n(&r, 5);         /* HEAD */
    skip_n(&r, 1);         /* background */
    skip_n(&r, 17);        /* AFTER_BACKGROUND */
    skip_n(&r, 1);         /* opposing */
    skip_n(&r, 4);         /* AFTER_OPPOSING */
    mmo_get_utf16_nt(&r, name, sizeof name);
    skip_n(&r, 1);         /* reserved */
    mmo_get_s64le(&r);     /* player id */
    skip_n(&r, 1);         /* opposing again */
    skip_n(&r, 14);        /* appearance */
    skip_n(&r, 5);         /* padding */
    skip_n(&r, 1);         /* constant 1 */
    party_count = mmo_get_u8(&r);
    skip_n(&r, 1);         /* reserved */
    for (i = 0; i < party_count; i++) {
        if (skip_full_block(&r) != 0)
            return -1;
        skip_n(&r, 1);     /* last-block flag */
    }
    skip_n(&r, 21);        /* active detail */
    tag = mmo_get_u8(&r);
    skip_n(&r, 1);         /* constant 6 */
    skip_n(&r, 1);         /* reserved */
    if (tag == 2)
        skip_n(&r, 2);     /* trainer id */
    skip_n(&r, 4);
    if (tag == 2)
        skip_n(&r, 2);
    skip_n(&r, 1);         /* constant 1 */
    opp_count = mmo_get_u8(&r);
    skip_n(&r, 1);         /* reserved */
    if (r.err || opp_count < 1)
        return -1;
    skip_n(&r, 1);         /* slot */
    kind = mmo_get_u8(&r);
    if (r.err || kind != 1)
        return -1;
    mmo_get_s64le(&r);
    species = mmo_get_s16le(&r);
    level = mmo_get_u8(&r);
    if (r.err || species <= 0)
        return -1;
    out->foe_species = species;
    out->foe_level = level;
    return 0;
}


static u32 battle_entity(mmo_rbuf *r)
{
    return (u32)mmo_get_s64le(r);
}

static int skip_utf16(mmo_rbuf *r)
{
    char tmp[256];
    mmo_get_utf16_nt(r, tmp, sizeof tmp);
    return r->err ? -1 : 0;
}

static int skip_battle_arg(mmo_rbuf *r)
{
    mmo_get_u8(r);                        /* id */
    int type = mmo_get_u8(r);
    if (type & 0x80) {
        type &= 0x7F;
        mmo_get_u8(r);                    /* extra */
    }
    switch (type) {
    case 5:
    case 18:
        return skip_utf16(r);
    case 28:
        return 0;
    case 30:
        mmo_get_s64le(r);
        return r->err ? -1 : 0;
    case 9:
    case 10:
    case 17:
        mmo_get_s32le(r);
        return r->err ? -1 : 0;
    default: {
        int n = mmo_get_u8(r);
        for (int i = 0; i < n; i++)
            mmo_get_s16le(r);
        return r->err ? -1 : 0;
    }
    }
}

static int skip_battle_entry(mmo_rbuf *r)
{
    int tag = (s8)mmo_get_u8(r);
    switch (tag) {
    case -1:
        return 0;
    case 0: {
        mmo_get_s32le(r);
        int n = mmo_get_u8(r);
        for (int i = 0; i < n; i++)
            if (skip_battle_arg(r) != 0)
                return -1;
        return r->err ? -1 : 0;
    }
    case 1: {
        mmo_get_u8(r);
        int n = mmo_get_u8(r);
        for (int i = 0; i < n; i++)
            mmo_get_s16le(r);
        return r->err ? -1 : 0;
    }
    case 2:
        mmo_get_s16le(r);
        mmo_get_u8(r);
        mmo_get_u8(r);
        return r->err ? -1 : 0;
    case 3:
        mmo_get_u8(r);
        mmo_get_u8(r);
        mmo_get_s16le(r);
        mmo_get_s16le(r);
        return r->err ? -1 : 0;
    default:
        return -1;
    }
}

static int skip_battle_entries(mmo_rbuf *r)
{
    int n = mmo_get_u8(r);
    for (int i = 0; i < n; i++)
        if (skip_battle_entry(r) != 0)
            return -1;
    return r->err ? -1 : 0;
}

int mmo_game_read_battle_entity_delta(const u8 *body, size_t n,
                                      mmo_battle_entity_delta *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    memset(out, 0, sizeof *out);
    out->xp_level = -1;
    out->xp = -1;
    out->level = -1;
    out->hp = -1;
    out->faint = -1;
    out->species = -1;
    out->happiness = -1;

    out->entity_id = battle_entity(&r);
    u32 mask = (u32)mmo_get_s32le(&r);
    out->mask = mask;
    if (mask & ~0x00FFFFFFu)
        return -1;

    if (mask & 0x000001) {                /* EXPERIENCE */
        out->xp_level = (s8)mmo_get_u8(&r);
        out->xp = mmo_get_s32le(&r);
    }
    if (mask & 0x000002) {                /* STATS: six shorts, RC0.Vq0 */
        out->have_stats = 1;
        for (int i = 0; i < MMO_BATTLE_STATS; i++)
            out->stats[i] = mmo_get_s16le(&r);
    }
    if (mask & 0x000004) {                /* MOVES: four (id, pp) then pp-ups */
        out->have_moves = 1;
        for (int i = 0; i < MMO_BATTLE_MOVES; i++) {
            out->move_id[i] = mmo_get_s16le(&r);
            out->move_pp[i] = mmo_get_u8(&r);
        }
        out->pp_ups = mmo_get_u8(&r);
    }
    if (mask & 0x000008)                  /* CURRENT_HP */
        out->hp = mmo_get_s16le(&r);
    if (mask & 0x000010)                  /* FAINT */
        out->faint = (s8)mmo_get_u8(&r);
    if (mask & 0x000020) {                /* SPECIES */
        out->species = mmo_get_s16le(&r);
        out->forme = (s8)mmo_get_u8(&r);
    }
    if (mask & 0x000040) {                /* LISTING: facing/heading, unread */
        mmo_get_u8(&r);
        mmo_get_s16le(&r);
    }
    if (mask & 0x000080) {                /* EVS: six shorts */
        out->have_evs = 1;
        for (int i = 0; i < MMO_BATTLE_STATS; i++)
            out->evs[i] = mmo_get_s16le(&r);
    }
    if (mask & 0x000100)                  /* LEVEL */
        out->level = mmo_get_s16le(&r);
    if (mask & 0x000200)                  /* HAPPINESS */
        out->happiness = mmo_get_s16le(&r);
    if (mask & 0x000400) {                /* POSITION */
        mmo_get_s32le(&r);
        mmo_get_s32le(&r);
        mmo_get_u8(&r);
        mmo_get_u8(&r);
        mmo_get_u8(&r);
    }
    if (mask & 0x000800)                  /* STATUS_FLAGS */
        mmo_get_s16le(&r);
    if (mask & 0x001000) {                /* MAP_POSITION */
        mmo_get_s16le(&r);
        mmo_get_s16le(&r);
    }
    if (mask & 0x002000)                  /* NATURE */
        mmo_get_u8(&r);
    if (mask & 0x004000)                  /* VALUE_64 */
        mmo_get_s64le(&r);
    if (mask & 0x200000)                  /* PACKED_IVS: out of bit order */
        mmo_get_s32le(&r);
    if (mask & 0x008000) {                /* ORIGIN */
        for (int i = 0; i < 4; i++)
            mmo_get_s16le(&r);
        mmo_get_s64le(&r);
        if (skip_utf16(&r) != 0)
            return -1;
        mmo_get_s32le(&r);
    }
    if (mask & 0x010000)                  /* SHININESS */
        mmo_get_u8(&r);
    if (mask & 0x020000)                  /* GENDER */
        mmo_get_u8(&r);
    if (mask & 0x040000) {                /* IVS: five shorts, mm0.pF1 */
        for (int i = 0; i < 5; i++)
            mmo_get_s16le(&r);
    }
    if (mask & 0x080000)                  /* CAUGHT_BALL */
        mmo_get_u8(&r);
    if (mask & 0x100000)                  /* STATUS_FLAGS_2 */
        mmo_get_s16le(&r);
    if (mask & 0x400000) {                /* STATUS_LIST */
        int nlist = mmo_get_u8(&r);
        for (int i = 0; i < nlist; i++)
            mmo_get_u8(&r);
    }
    if (mask & 0x800000)                  /* STATUS */
        mmo_get_u8(&r);

    if (r.err)
        return -1;
    return 0;
}

static int read_sub_event(mmo_rbuf *r, mmo_battle_sub_event *s)
{
    memset(s, 0, sizeof *s);
    s->type = mmo_get_u8(r);
    int flags = mmo_get_u8(r);
    if (flags & 0x01) {
        s->have_a = 1;
        s->entity_a = battle_entity(r);
    }
    if (flags & 0x02) {
        s->have_b = 1;
        s->entity_b = battle_entity(r);
    }
    switch (s->type) {
    case MMO_BATTLE_SUB_HP:
        s->hp = mmo_get_s16le(r);
        break;
    case MMO_BATTLE_SUB_STAT:
        s->change_type = (s8)mmo_get_u8(r);
        s->stat = mmo_get_u8(r) & 0x7F;
        s->stages = (s8)mmo_get_u8(r);
        mmo_get_u8(r);                    /* 0xFF in every capture; unread */
        break;
    case MMO_BATTLE_SUB_EFFECT:
        break;
    case MMO_BATTLE_SUB_FAINT:
        s->faint_anim = mmo_get_u8(r);
        break;
    case MMO_BATTLE_SUB_FAIL:
        s->fail_move = mmo_get_s16le(r);
        break;
    default:
        return -1;                        /* a body nothing here can walk */
    }
    return r->err ? -1 : 0;
}

int mmo_game_read_battle_move_event(const u8 *body, size_t n,
                                    mmo_battle_move_event *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    memset(out, 0, sizeof *out);

    out->source_entity = battle_entity(&r);
    out->source_move = mmo_get_s16le(&r);
    out->kind = (s8)mmo_get_u8(&r);
    int nt = mmo_get_u8(&r);
    if (r.err)
        return -1;
    for (int i = 0; i < nt; i++) {
        u32 id = battle_entity(&r);
        int outcome = mmo_get_s16le(&r);
        int ns = mmo_get_u8(&r);
        if (r.err)
            return -1;
        mmo_battle_effect_target *t = (i < MMO_BATTLE_MOVE_TARGETS)
                                          ? &out->target[out->n_targets++]
                                          : NULL;
        mmo_battle_effect_target drop;
        if (!t) {
            memset(&drop, 0, sizeof drop);
            t = &drop;
        }
        t->entity_id = id;
        t->outcome = outcome;
        for (int j = 0; j < ns; j++) {
            mmo_battle_sub_event sub;
            if (read_sub_event(&r, &sub) != 0)
                return -1;
            if (t->n_subs < MMO_BATTLE_MOVE_SUBS)
                t->sub[t->n_subs++] = sub;
        }
    }
    return r.err ? -1 : 0;
}

int mmo_game_read_battle_stat_counters(const u8 *body, size_t n,
                                       mmo_battle_stat_counters *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    memset(out, 0, sizeof *out);

    out->entity_id = battle_entity(&r);
    out->base = mmo_get_s32le(&r);
    int flags = mmo_get_u8(&r);
    for (int i = 0; i < 6; i++) {
        if (flags & (1 << i)) {
            out->have[i] = 1;
            out->counter[i] = mmo_get_s32le(&r);
        }
    }
    if (r.err)
        return -1;
    return 0;
}

int mmo_game_read_battle_bulk_state(const u8 *body, size_t n,
                                    mmo_battle_bulk_state *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    memset(out, 0, sizeof *out);

    out->phase = (s8)mmo_get_u8(&r);
    if (skip_battle_entries(&r) != 0 || skip_battle_entries(&r) != 0)
        return -1;
    out->prize = mmo_get_s32le(&r);
    out->value_b = mmo_get_s32le(&r);
    out->flag = (s8)mmo_get_u8(&r);
    if (skip_battle_entries(&r) != 0)
        return -1;
    if (r.err)
        return -1;
    return 0;
}

int mmo_game_read_battle_queued(const u8 *body, size_t n,
                                mmo_battle_queued *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    int packed = mmo_get_u8(&r);
    if (r.err)
        return -1;
    out->value = packed & 0x7F;
    out->flag = (packed & 0x80) != 0;
    return 0;
}

int mmo_game_read_battle_slot_event(const u8 *body, size_t n,
                                    mmo_battle_slot_event *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    out->slot = (s8)mmo_get_u8(&r);
    out->event_type = (s8)mmo_get_u8(&r);
    if (r.err)
        return -1;
    return 0;
}

#define BATTLE_ACTIVE_DETAIL 21

static int read_active_detail(mmo_rbuf *r, mmo_battle_switch_in *out)
{
    mmo_get_u8(r);                        /* reserved */
    int slot = (s8)mmo_get_u8(r);
    int species = mmo_get_s16le(r);
    int level = (s8)mmo_get_u8(r);
    for (int i = 0; i < 4; i++)
        mmo_get_u8(r);                    /* padding */
    int gender = (s8)mmo_get_u8(r);
    mmo_get_u8(r);                        /* reserved */
    for (int i = 0; i < 10; i++)
        mmo_get_u8(r);                    /* ACTIVE_TAIL, unread */
    if (r->err)
        return -1;
    if (!out->full_block) {
        out->new_slot = slot;
        out->species = species;
        out->level = level;
        out->gender = gender;
    }
    (void)slot;
    return 0;
}

int mmo_game_read_battle_switch_in(const u8 *body, size_t n,
                                   mmo_battle_switch_in *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    memset(out, 0, sizeof *out);

    out->side = (s8)mmo_get_u8(&r);
    mmo_get_u8(&r);                       /* reserved(0) */
    out->new_slot = mmo_get_u8(&r);
    out->old_slot = mmo_get_u8(&r);
    if (r.err)
        return -1;
    size_t left = mmo_rbuf_remaining(&r);
    if (left < BATTLE_ACTIVE_DETAIL)
        return -1;
    out->full_block = left > BATTLE_ACTIVE_DETAIL;
    if (out->full_block) {
        out->new_slot = (s8)mmo_get_u8(&r);
        mmo_get_u8(&r);                   /* constant 1 */
        out->entity_id = battle_entity(&r);
        out->species = mmo_get_s16le(&r);
        out->level = (s8)mmo_get_u8(&r);
        mmo_get_u8(&r);
        mmo_get_u8(&r);
        out->gender = (s8)mmo_get_u8(&r);
        mmo_get_u8(&r);
        mmo_get_u8(&r);
        mmo_get_u8(&r);
        out->hp = mmo_get_s16le(&r);
        out->max_hp = mmo_get_s16le(&r);
        for (int i = 0; i < 5; i++)
            mmo_get_u8(&r);               /* MOVES_HEADER */
        int moves = mmo_get_u8(&r);
        if (moves) {
            mmo_get_s16le(&r);            /* ability */
            for (int i = 0; i < MMO_BATTLE_MOVES; i++)
                mmo_get_s16le(&r);
        }
        mmo_get_u8(&r);                   /* last-block flag */
        if (r.err)
            return -1;
    }
    return read_active_detail(&r, out);
}

int mmo_game_read_battle_slot_flag(const u8 *body, size_t n,
                                   mmo_battle_slot_flag *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    memset(out, 0, sizeof *out);
    out->slot = (s8)mmo_get_u8(&r);
    out->flag = mmo_get_u8(&r) == 1;
    out->immediate = mmo_get_u8(&r) == 1;
    if (r.err)
        return -1;
    return 0;
}

int mmo_game_read_battle_list_event(const u8 *body, size_t n,
                                    mmo_battle_list_event *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    memset(out, 0, sizeof *out);
    out->kind = (s8)mmo_get_u8(&r);
    out->value = mmo_get_s16le(&r);
    out->sub_kind = (s8)mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (out->sub_kind == MMO_BATTLE_LIST_CATCH) {
        out->have_detail = 1;
        out->list_type = (s8)mmo_get_u8(&r);
        out->detail_value = mmo_get_s16le(&r);
        if (r.err)
            return -1;
    }
    return 0;
}

int mmo_game_read_local_player_state(const u8 *body, size_t n,
                                     mmo_local_player_state *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->region = mmo_get_u8(&r);
    out->map_id = (u16)mmo_get_s16le(&r);
    mmo_get_u32le(&r);                    /* moveSpeed (F32LE) */
    out->x = mmo_get_s16le(&r);
    out->y = mmo_get_s16le(&r);
    out->z = mmo_get_s16le(&r);
    out->money = mmo_get_s32le(&r);
    out->gender = mmo_get_u8(&r);
    mmo_get_s16le(&r);                    /* skinTone */
    mmo_get_s16le(&r);                    /* hairColor */
    mmo_get_u32le(&r);                    /* playtime (F64LE) low word */
    mmo_get_u32le(&r);                    /* playtime (F64LE) high word */
    mmo_get_u8(&r);                       /* packed flags byte */

    /* partyDex: a U16LE-prefixed S16LE list. Keep the first MMO_WS_PARTY_MAX and
     * record the true on-wire length. */
    int party = mmo_get_u16le(&r);
    out->party_total = party;
    for (int i = 0; i < party; i++) {
        u16 dex = (u16)mmo_get_s16le(&r);
        if (i < MMO_WS_PARTY_MAX) {
            out->party_dex[i] = dex;
            out->party_count = i + 1;
        }
    }
    /* partyForms: a U8-prefixed S8 list (consumed). */
    int forms = mmo_get_u8(&r);
    for (int i = 0; i < forms; i++)
        mmo_get_u8(&r);
    /* pokedexSeen then pokedexCaught: U16LE-prefixed S16LE lists (consumed). */
    for (int pass = 0; pass < 2; pass++) {
        int cnt = mmo_get_u16le(&r);
        for (int i = 0; i < cnt; i++)
            mmo_get_s16le(&r);
    }
    /* badges: a U8-prefixed S16LE list. */
    int badges = mmo_get_u8(&r);
    out->badge_count = badges;
    for (int i = 0; i < badges; i++) {
        s16 id = mmo_get_s16le(&r);
        if (i < MMO_WS_BADGE_MAX)
            out->badges[i] = id;
    }
    /* variables: a U16LE-prefixed list of (key S16LE, value S8). The key is the
     * GBA var offset (gbaVar - 0x4000); keep the first MMO_WS_VAR_MAX. */
    int vars = mmo_get_u16le(&r);
    out->var_count = vars;
    for (int i = 0; i < vars; i++) {
        u16 key = (u16)mmo_get_s16le(&r);
        s8  val = (s8)mmo_get_u8(&r);
        if (i < MMO_WS_VAR_MAX) {
            out->var_key[i] = key;
            out->var_val[i] = val;
            out->var_stored = i + 1;
        }
    }
    return r.err ? -1 : 0;
}

/* ScriptState (0xCB): the engine's VarsFlags block as a sparse delta. Both
 * directions use this one shape, the s2c seat is the same body with every
 * value the server holds in it, and the c2s report is the same body with only
 * what a local script just changed. */
int mmo_game_write_script_state(mmo_wbuf *w,
                                const mmo_script_flag *flags, int nflags,
                                const mmo_script_var *vars, int nvars,
                                const mmo_save_block *blocks, int nblocks)
{
    if (nflags < 0 || nflags > 0xffff || nvars < 0 || nvars > 0xffff)
        return -1;
    if ((nflags > 0 && flags == NULL) || (nvars > 0 && vars == NULL))
        return -1;
    if (nblocks < 0 || nblocks > MMO_SAVE_BLOCK_MAX)
        return -1;
    if (nblocks > 0 && blocks == NULL)
        return -1;
    for (int i = 0; i < nblocks; i++) {
        if (blocks[i].len < 0 || blocks[i].len > MMO_SAVE_BLOCK_BYTES)
            return -1;
        if (blocks[i].len > 0 && blocks[i].data == NULL)
            return -1;
        if (blocks[i].id < 0 || blocks[i].id > 0xff)
            return -1;
    }

    mmo_put_u16le(w, (u16)nflags);
    for (int i = 0; i < nflags; i++) {
        mmo_put_u16le(w, flags[i].id);
        mmo_put_u8(w, flags[i].on ? 1 : 0);
    }
    mmo_put_u16le(w, (u16)nvars);
    for (int i = 0; i < nvars; i++) {
        mmo_put_u16le(w, vars[i].id);
        mmo_put_u16le(w, vars[i].value);
    }
    mmo_put_u8(w, (u8)nblocks);
    for (int i = 0; i < nblocks; i++) {
        mmo_put_u8(w, (u8)blocks[i].id);
        mmo_put_u16le(w, (u16)blocks[i].len);
        for (int k = 0; k < blocks[i].len; k++)
            mmo_put_u8(w, blocks[i].data[k]);
    }
    return w->err ? -1 : 0;
}

/* OfflineSaveReport (c2s 0xCA): one piece of a save report. The pieces are
 * numbered so the far end refuses a report assembled out of order rather than
 * silently mis-joining one, a report joined wrong is a character. */
int mmo_game_write_offline_report(mmo_wbuf *w, int sequence, int last,
                                  const u8 *chunk, size_t len)
{
    if (sequence < 0 || sequence > 0xffff)
        return -1;
    if (len > 0xffff || (len > 0 && chunk == NULL))
        return -1;
    mmo_put_u16le(w, (u16)sequence);
    mmo_put_bool(w, last);
    mmo_put_u16le(w, (u16)len);
    if (len > 0)
        mmo_put_bytes(w, chunk, len);
    return w->err ? -1 : 0;
}

/* One UTF-8 string with a U16LE byte-count prefix, into `dst` (always
 * NUL-terminated). Returns the length the wire claimed, which may be longer
 * than what was kept. */
static int get_text_u16(mmo_rbuf *r, char *dst, size_t cap)
{
    u16 n = mmo_get_u16le(r);
    size_t kept = 0;

    for (u16 i = 0; i < n; i++) {
        u8 ch = mmo_get_u8(r);

        if (dst != NULL && kept + 1 < cap)
            dst[kept++] = (char)ch;
    }
    if (dst != NULL && cap > 0)
        dst[kept] = '\0';
    return r->err ? -1 : (int)n;
}

/* OfflineImportResult (s2c 0xCE): the one answer to a whole report. Notes past
 * `note_cap` are counted in *out_nnotes_sent and dropped, so a caller can say
 * "and eleven more" rather than believing it read them all. */
int mmo_game_read_offline_result(const u8 *body, size_t n, int *out_status,
                                 char *message, size_t message_cap,
                                 char *notes, size_t note_stride, int note_cap,
                                 int *out_nnotes, int *out_nnotes_sent,
                                 int *out_wants_chain)
{
    mmo_rbuf r;
    int status;
    int sent;
    int kept = 0;

    mmo_rbuf_init(&r, body, n);
    status = mmo_get_u8(&r);
    if (get_text_u16(&r, message, message_cap) < 0)
        return -1;
    sent = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    for (int i = 0; i < sent; i++) {
        char *dst = (notes != NULL && kept < note_cap)
                        ? notes + (size_t)kept * note_stride
                        : NULL;

        if (get_text_u16(&r, dst, dst != NULL ? note_stride : 0) < 0)
            return -1;
        if (dst != NULL)
            kept++;
    }
    if (out_status != NULL)
        *out_status = status;
    if (out_nnotes != NULL)
        *out_nnotes = kept;
    if (out_nnotes_sent != NULL)
        *out_nnotes_sent = sent;
    /* Whether this server would look at the session records behind the save.
     * A trailing field, so a server that predates it simply reads as one that
     * wants nothing, which is the honest answer for one that cannot replay
     * anything either. */
    if (out_wants_chain != NULL)
        *out_wants_chain = r.err ? 0 : (mmo_get_u8(&r) != 0 && !r.err);
    return 0;
}

int mmo_game_read_script_state(const u8 *body, size_t n,
                               mmo_script_flag *flags, int flag_cap,
                               int *out_nflags, int *out_flags_stored,
                               mmo_script_var *vars, int var_cap,
                               int *out_nvars, int *out_vars_stored,
                               mmo_save_block *blocks, int block_cap,
                               int *out_nblocks, int *out_blocks_stored)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    int nflags = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    int fstored = 0;
    for (int i = 0; i < nflags; i++) {
        u16 id = mmo_get_u16le(&r);
        u8 on = mmo_get_u8(&r);
        if (r.err)
            return -1;
        if (flags != NULL && fstored < flag_cap) {
            flags[fstored].id = id;
            flags[fstored].on = on ? 1 : 0;
            fstored++;
        }
    }

    int nvars = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    int vstored = 0;
    for (int i = 0; i < nvars; i++) {
        u16 id = mmo_get_u16le(&r);
        u16 value = mmo_get_u16le(&r);
        if (r.err)
            return -1;
        if (vars != NULL && vstored < var_cap) {
            vars[vstored].id = id;
            vars[vstored].value = value;
            vstored++;
        }
    }

    /* The block list. A body that stops before it is an older frame, not a
     * malformed one, there is no third count to read and nothing carried a
     * block, which is exactly what an empty list means. */
    int nblocks = 0, bstored = 0;

    if (mmo_rbuf_remaining(&r) > 0) {
        nblocks = mmo_get_u8(&r);
        if (r.err)
            return -1;
        for (int i = 0; i < nblocks; i++) {
            int id = mmo_get_u8(&r);
            int len = mmo_get_u16le(&r);
            const u8 *at;

            if (r.err || len < 0 || len > MMO_SAVE_BLOCK_BYTES)
                return -1;
            if (mmo_rbuf_remaining(&r) < (size_t)len)
                return -1;
            /* The bytes stay where they are and the entry points at them, so a
             * seat copies them out before the buffer is reused. */
            at = body + r.pos;
            r.pos += (size_t)len;
            if (blocks != NULL && bstored < block_cap) {
                blocks[bstored].id = id;
                blocks[bstored].len = len;
                blocks[bstored].data = at;
                bstored++;
            }
        }
    }

    if (out_nflags)       *out_nflags = nflags;
    if (out_flags_stored) *out_flags_stored = fstored;
    if (out_nvars)        *out_nvars = nvars;
    if (out_vars_stored)  *out_vars_stored = vstored;
    if (out_nblocks)      *out_nblocks = nblocks;
    if (out_blocks_stored) *out_blocks_stored = bstored;
    return 0;
}

int mmo_game_write_contest_comm(mmo_wbuf *w, int kind, int session_id, int seat,
                                const void *payload, int len)
{
    const u8 *p = payload;

    if (len < 0 || len > MMO_CONTEST_DATA_MAX)
        return -1;
    if (len > 0 && p == NULL)
        return -1;
    if (kind < 0 || kind > 0xff)
        return -1;
    if (seat < -128 || seat > 127)
        return -1;

    mmo_put_u8(w, (u8)kind);
    mmo_put_s32le(w, (s32)session_id);
    mmo_put_u8(w, (u8)(s8)seat);
    mmo_put_u16le(w, (u16)len);
    for (int i = 0; i < len; i++)
        mmo_put_u8(w, p[i]);
    return w->err ? -1 : 0;
}

int mmo_game_read_contest_comm(const u8 *body, size_t n, mmo_contest_comm *out)
{
    mmo_rbuf r;

    if (out == NULL)
        return -1;
    memset(out, 0, sizeof *out);
    mmo_rbuf_init(&r, body, n);

    out->kind = mmo_get_u8(&r);
    out->session_id = mmo_get_s32le(&r);
    out->seat = (s8)mmo_get_u8(&r);
    out->len = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    /* Past the engine's own per-command ceiling is not a long message, it is a
     * frame this client cannot be reading correctly. */
    if (out->len > MMO_CONTEST_DATA_MAX)
        return -1;
    if (mmo_rbuf_remaining(&r) < (size_t)out->len)
        return -1;
    for (int i = 0; i < out->len; i++)
        out->data[i] = mmo_get_u8(&r);
    return r.err ? -1 : 0;
}

int mmo_game_read_contest_seat(const u8 *payload, int len, mmo_contest_seat *out)
{
    mmo_rbuf r;

    if (out == NULL || payload == NULL)
        return -1;
    memset(out, 0, sizeof *out);
    mmo_rbuf_init(&r, payload, (size_t)(len < 0 ? 0 : len));

    out->rank = mmo_get_u8(&r);
    out->type = mmo_get_u8(&r);
    out->humans = mmo_get_u8(&r);
    if (r.err)
        return -1;
    /* One human is not a link contest and more than the engine seats is not a
     * contest at all; either way this client must not sit down in it. */
    if (out->humans < 2 || out->humans > MMO_CONTEST_SEATS)
        return -1;
    for (int i = 0; i < out->humans; i++) {
        out->seat[i].flags = mmo_get_u8(&r);
        out->seat[i].gender = mmo_get_u8(&r);
        mmo_get_utf16_nt(&r, out->seat[i].name, sizeof out->seat[i].name);
        if (r.err)
            return -1;
    }
    return 0;
}

/* KeepAlive (0xC2). `canJoin` is false here: the field belongs to the login
 * server's own use of this packet and the game server echoes without reading
 * it, so the only byte with meaning to us is the token behind it. */
void mmo_game_write_keepalive(mmo_wbuf *w, u8 token)
{
    mmo_put_u8(w, 0);
    mmo_put_u8(w, token);
}

int mmo_game_read_keepalive(const u8 *body, size_t n, u8 *out_token)
{
    if (body == NULL || n < 2)
        return -1;
    if (out_token != NULL)
        *out_token = body[1];
    return 0;
}

void mmo_game_write_script_warp(mmo_wbuf *w, s32 header, s16 x, s16 z,
                                u8 wire_dir)
{
    mmo_put_s32le(w, header);
    mmo_put_s16le(w, x);
    mmo_put_s16le(w, z);
    mmo_put_u8(w, (u8)(wire_dir & 0x03));
}

void mmo_game_write_script_owner(mmo_wbuf *w, int client_runs_scripts)
{
    mmo_put_u8(w, client_runs_scripts ? 1 : 0);
}

void mmo_game_write_pokemon_release(mmo_wbuf *w, s64 id)
{
    mmo_put_s64le(w, id);
}

void mmo_game_write_script_grant(mmo_wbuf *w, u16 dex, u8 level, s16 hp,
                                 u8 container, s16 slot, u32 seed,
                                 u32 iv_bits, u8 shiny,
                                 const u8 *nick_utf16le, size_t nick_bytes)
{
    size_t i;

    mmo_put_u16le(w, dex);
    mmo_put_u8(w, level);
    mmo_put_s16le(w, hp);
    mmo_put_u8(w, container);
    mmo_put_s16le(w, slot);
    mmo_put_u32le(w, seed);
    mmo_put_u32le(w, iv_bits);
    mmo_put_u8(w, shiny ? 1 : 0);
    /* At most MON_NAME_LEN (10) code units, stopping at a NUL unit. */
    for (i = 0; nick_utf16le != NULL && i + 1 < nick_bytes && i < 2 * 10;
         i += 2) {
        u16 unit = (u16)(nick_utf16le[i] | ((u16)nick_utf16le[i + 1] << 8));

        if (unit == 0)
            break;
        mmo_put_u16le(w, unit);
    }
    mmo_put_u16le(w, 0);
}

void mmo_game_write_bag_delta(mmo_wbuf *w, u16 item, s16 delta)
{
    mmo_put_u16le(w, item);
    mmo_put_s16le(w, delta);
}

void mmo_game_write_money_delta(mmo_wbuf *w, s32 delta)
{
    mmo_put_s32le(w, delta);
}

void mmo_game_write_registered_item(mmo_wbuf *w, u16 item)
{
    mmo_put_u16le(w, item);
}

int mmo_game_read_registered_item(const u8 *body, size_t n, u16 *item)
{
    mmo_rbuf r;
    u16 v;

    if (body == NULL || item == NULL)
        return -1;
    mmo_rbuf_init(&r, body, n);
    v = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    *item = v;
    return 0;
}

void mmo_game_write_battle_outcome(mmo_wbuf *w,
                                   const mmo_battle_mon_outcome *mons, int n)
{
    int i, s;

    if (n < 0)
        n = 0;
    if (n > 6)
        n = 6;
    mmo_put_u8(w, (u8)n);
    for (i = 0; i < n; i++) {
        mmo_put_s64le(w, mons[i].id);
        mmo_put_u8(w, mons[i].level);
        mmo_put_s32le(w, mons[i].exp);
        mmo_put_s16le(w, mons[i].hp);
        for (s = 0; s < 4; s++) {
            mmo_put_u16le(w, mons[i].move[s]);
            mmo_put_u8(w, mons[i].pp[s]);
        }
        for (s = 0; s < MMO_MON_CONDITIONS; s++)
            mmo_put_u8(w, mons[i].cond[s]);
        mmo_put_u8(w, mons[i].sheen);
        mmo_put_s64le(w, (s64)mons[i].ribbons_super);
        mmo_put_u16le(w, mons[i].species);
        mmo_put_s16le(w, mons[i].friendship);
        mmo_put_s16le(w, mons[i].held_item);
        mmo_put_u8(w, (u8)(mons[i].egg != 0));
        mmo_put_u16le(w, (u16)(mons[i].status & MMO_MON_STATUS_MASK));
    }
}

int mmo_game_read_story_flag(const u8 *body, size_t n,
                             int *out_region, int *out_flag_id, int *out_enabled)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    int region = mmo_get_u8(&r);
    int flag_id = (u16)mmo_get_s16le(&r);
    int enabled = mmo_get_s16le(&r) != 0;
    if (r.err)
        return -1;
    if (out_region)  *out_region = region;
    if (out_flag_id) *out_flag_id = flag_id;
    if (out_enabled) *out_enabled = enabled;
    return 0;
}

int mmo_game_read_world_flag_reset(const u8 *body, size_t n,
                                   u8 *blocks, int block_cap,
                                   int *lens, int group_cap, int *count)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    int groups = mmo_get_u8(&r);          /* listPrefixed(U8) */
    if (r.err)
        return -1;
    if (groups > group_cap)
        return -1;                        /* more groups than we hold: trap loudly */

    int got = 0;
    for (int i = 0; i < groups; i++) {
        u8 comp[512];
        int glen = mmo_get_u16le(&r);     /* bytesPrefixed(U16LE) */
        if (r.err)
            return -1;
        if (glen > (int)sizeof comp)
            return -1;                    /* compressed group over comp[]: trap loudly */
        mmo_get_bytes(&r, comp, (size_t)glen);
        if (r.err)
            return -1;
        u8 *blk = blocks + (size_t)i * block_cap;
        if (glen == 0) {
            lens[i] = 0;                  /* empty group -> empty flag block */
        } else {
            size_t dl = mmo_inflate_zlib(comp, (size_t)glen, blk, (size_t)block_cap);
            if (dl == (size_t)-1)
                return -1;
            /*
             * Inside the stream: a big-endian U16 bit-array length, that many bytes of flag
             * bits, then a U8 count of keyed 32-bit flag words, each a big-endian S16 key and
             * S32 value.
             */
            if (dl < 3)
                return -1;
            size_t blen = ((size_t)blk[0] << 8) | blk[1];
            if (blen + 3 > dl)
                return -1;
            size_t words = blk[2 + blen];
            if (2 + blen + 1 + words * 6 != dl)
                return -1;
            memmove(blk, blk + 2, blen);
            lens[i] = (int)blen;
        }
        got = i + 1;
    }
    if (count)
        *count = got;
    return r.err ? -1 : 0;
}

/* One BattlePartyPokemon / BattleAddPokemon record. The bag reuses this
 * shape: entityId is the stack object id, frontSprite the item id,
 * backSprite the quantity. Returns 0, or -1 if the buffer runs out. */
static int read_item_stack_record(mmo_rbuf *r, mmo_item_stack *out)
{
    int flags = mmo_get_u8(r);
    s64 object_id = mmo_get_s64le(r);
    if (flags & 0x01)
        mmo_get_s64le(r);
    u16 item_id = (u16)mmo_get_s16le(r);
    int quantity = (u16)mmo_get_s16le(r);
    mmo_get_u8(r);                        /* side */
    if (flags & 0x02)
        mmo_get_u8(r);
    if (flags & 0x04)
        mmo_get_u8(r);                    /* slot */
    if (flags & 0x08)
        mmo_get_u8(r);                    /* partyIndex */
    if (flags & 0x10) {                   /* status effect */
        mmo_get_s32le(r);
        mmo_get_u8(r);
        mmo_get_u8(r);
    }
    if (r->err)
        return -1;
    if (out) {
        out->object_id = object_id;
        out->item_id = item_id;
        out->quantity = quantity;
    }
    return 0;
}

int mmo_game_read_item_stacks(const u8 *body, size_t n,
                              mmo_item_stack *out, int cap, int *total,
                              int *replace)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    mmo_get_u8(&r);                       /* side */
    int repl = mmo_get_u8(&r);
    int count = mmo_get_u16le(&r);
    if (total)
        *total = count;
    if (replace)
        *replace = repl != 0;

    int stored = 0;
    for (int i = 0; i < count; i++) {
        /* BattlePartyPokemonCodec: a flags byte gates the optional fields.
         * itemStacksPacket writes slot 0, partyIndex -1 and no status, so flags is
         * always 0 for a real bag stack, but the conditional fields are consumed
         * so a fuller record still lands on the right bytes. */
        mmo_item_stack one;
        if (read_item_stack_record(&r, &one) != 0)
            break;
        if (stored < cap)
            out[stored++] = one;
    }
    return r.err ? -1 : stored;
}

int mmo_game_read_item_stack_update(const u8 *body, size_t n, mmo_item_stack *out)
{
    mmo_rbuf r;
    mmo_item_stack one;

    mmo_rbuf_init(&r, body, n);
    mmo_get_u8(&r);                       /* side */
    if (read_item_stack_record(&r, &one) != 0)
        return -1;
    /* A battle-side add uses the same opcode and the same record. Item
     * object ids end in MMO_ITEM_ENTITY_TAG; monster uids do not. */
    if ((one.object_id & 0xFFFF) != MMO_ITEM_ENTITY_TAG)
        return 0;
    if (out)
        *out = one;
    return 1;
}

int mmo_game_read_local_character_delta(const u8 *body, size_t n,
                                        mmo_local_character_delta *out)
{
    mmo_rbuf r;
    u16 mask;
    s8 kind;
    int i, nstatus;

    mmo_rbuf_init(&r, body, n);
    if (out)
        memset(out, 0, sizeof *out);

    mask = (u16)mmo_get_s16le(&r);
    if (mask & 0x0001) {
        s32 money = mmo_get_s32le(&r);
        if (out) {
            out->has_money = 1;
            out->money = money;
        }
    }
    if (mask & 0x0002) {
        mmo_get_s16le(&r);
        mmo_get_u8(&r);
    }
    if (mask & 0x0004)
        mmo_get_s16le(&r);
    if (mask & 0x0008) {
        mmo_get_u8(&r);
        mmo_get_u8(&r);
        mmo_get_u8(&r);
    }
    if (mask & 0x0010) {
        mmo_get_s16le(&r);
        mmo_get_s16le(&r);
    }
    if (mask & 0x0020)
        mmo_get_s32le(&r);
    if (mask & 0x0040) {
        kind = (s8)mmo_get_u8(&r);
        if (kind == 0 || kind == 1 || kind == 2) {
            mmo_get_s16le(&r);
            mmo_get_s16le(&r);
        }
    }
    if (mask & 0x0080) {
        nstatus = (s8)mmo_get_u8(&r);
        if (nstatus < 0)
            return -1;
        for (i = 0; i < nstatus; i++)
            mmo_get_u8(&r);
    }
    if (mask & 0x0100)
        mmo_get_u8(&r);

    return r.err ? -1 : 0;
}

int mmo_game_read_shop_catalog(const u8 *body, size_t n,
                               mmo_shop_catalog *out,
                               mmo_shop_item *items, int cap)
{
    mmo_rbuf r;
    mmo_shop_catalog sink;
    s8 kind;
    u8 flags;
    int i, total, extra, j;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    kind = (s8)mmo_get_u8(&r);
    if (kind == (s8)MMO_SHOP_KIND_CLOSED) {
        out->kind = MMO_SHOP_KIND_CLOSED;
        return r.err ? -1 : 0;
    }

    out->open = 1;
    out->kind = kind;
    flags = mmo_get_u8(&r);
    out->flags = flags;
    out->currency_kind = mmo_get_u8(&r);

    if (flags & MMO_SHOP_FLAG_PARAM) {
        out->has_param = 1;
        out->param = mmo_get_s32le(&r);
    }
    if (flags & MMO_SHOP_FLAG_TITLE) {
        out->has_title = 1;
        out->title_id = mmo_get_s32le(&r);
    }
    if (flags & MMO_SHOP_FLAG_SUBTITLE) {
        out->has_subtitle = 1;
        out->subtitle_id = mmo_get_s32le(&r);
    }
    if (flags & MMO_SHOP_FLAG_NPC) {
        out->has_npc = 1;
        out->npc_entity_id = mmo_get_s64le(&r);
    }

    total = mmo_get_u16le(&r);
    out->total = total;
    if (cap < 0)
        cap = 0;
    for (i = 0; i < total; i++) {
        u16 item_id = (u16)mmo_get_s16le(&r);
        s16 stock = mmo_get_s16le(&r);
        s32 price = 0;
        if (kind == MMO_SHOP_KIND_PAIRS) {
            extra = mmo_get_u8(&r);
            for (j = 0; j < extra; j++) {
                mmo_get_s16le(&r);
                mmo_get_s16le(&r);
            }
        } else {
            price = mmo_get_s32le(&r);
        }
        if (items && i < cap) {
            items[i].item_id = item_id;
            items[i].stock = stock;
            items[i].price = price;
            out->count++;
        }
    }

    if (kind == MMO_SHOP_KIND_PAIRS && (flags & 0x04)) {
        extra = mmo_get_u16le(&r);
        for (j = 0; j < extra; j++) {
            mmo_get_s16le(&r);
            mmo_get_s16le(&r);
        }
    }

    return r.err ? -1 : 0;
}

/*
 * Read one typed dialog argument. Tags are the official client's the official client: -1 empty, 0 a nested creature
 * record, 1 a move list, 2 a species-name fill, 3 a creature-status fill.
 */
static int read_dialog_arg(mmo_rbuf *r, int depth, mmo_dialog_action *out)
{
    s8 tag;
    int i, n, slot;

    if (depth > 8) {
        r->err = 1;
        return -1;
    }
    tag = (s8)mmo_get_u8(r);
    if (r->err)
        return -1;
    switch (tag) {
    case -1:
        return 0;
    case 0:
        mmo_get_s32le(r);
        n = mmo_get_u8(r);
        for (i = 0; i < n; i++) {
            if (read_dialog_arg(r, depth + 1, out) != 0)
                return -1;
        }
        return r->err ? -1 : 0;
    case 1:
        mmo_get_u8(r);
        n = mmo_get_u8(r);
        for (i = 0; i < n; i++)
            mmo_get_s16le(r);
        return r->err ? -1 : 0;
    case 2:
        mmo_get_u8(r);
        mmo_get_u8(r);
        mmo_get_s16le(r);
        return r->err ? -1 : 0;
    case 3:
        mmo_get_u8(r);
        mmo_get_u8(r);
        mmo_get_s16le(r);
        mmo_get_s16le(r);
        return r->err ? -1 : 0;
    case 4:
        slot = (int)mmo_get_u8(r);
        if (out != NULL && out->strvar_count < MMO_DIALOG_STRVAR_MAX
            && slot < MMO_DIALOG_STRVAR_MAX) {
            mmo_dialog_strvar *v = &out->strvar[out->strvar_count];
            v->slot = slot;
            mmo_get_utf16_nt(r, v->text, sizeof v->text);
            if (!r->err)
                out->strvar_count++;
        } else {
            char sink[MMO_TEXT_BYTES(MMO_DIALOG_STRVAR_CHARS)];
            mmo_get_utf16_nt(r, sink, sizeof sink);
        }
        return r->err ? -1 : 0;
    default:
        r->err = 1;
        return -1;
    }
}

int mmo_game_read_dialog_action(const u8 *body, size_t n,
                                mmo_dialog_action *out)
{
    mmo_rbuf r;
    mmo_dialog_action sink;
    int i, count;
    s16 bank;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->flags = (s8)mmo_get_u8(&r);
    out->action_type = (s8)mmo_get_u8(&r);
    out->text_id = mmo_get_s32le(&r);
    out->entity_id = mmo_get_s64le(&r);
    out->context_value = mmo_get_s32le(&r);
    count = mmo_get_u8(&r);
    out->arg_count = count;
    for (i = 0; i < count; i++) {
        if (read_dialog_arg(&r, 0, out) != 0)
            return -1;
    }
    if (out->action_type == (s8)MMO_DIALOG_ACTION_MENU
        && mmo_rbuf_remaining(&r) > 0) {
        count = mmo_get_u8(&r);
        if (count > MMO_DIALOG_MENU_MAX)
            return -1;
        for (i = 0; i < count; i++)
            out->choices[i] = mmo_get_s16le(&r);
        out->choice_count = count;
        if (r.err || mmo_rbuf_remaining(&r) != 0)
            return -1;
        out->detail_len = 0;
    } else if (out->action_type == (s8)MMO_DIALOG_ACTION_LIST
               && mmo_rbuf_remaining(&r) > 0) {
        (void)mmo_get_u8(&r); /* bG1: unused, carried */
        (void)mmo_get_u8(&r); /* KK0: unused, carried */
        bank = mmo_get_s16le(&r);
        /*
         * An allowlist, not a sign test. The bank is handed straight to the engine's message
         * loader, which indexes the text archive's members with it, so one the archive does
         * not hold is a read past the end of that table.
         */
        if (bank != MMO_DIALOG_BANK_MAP_LABEL && !mmo_id_text_bank_valid(bank))
            return -1;
        count = mmo_get_u8(&r);
        if (count > MMO_DIALOG_MENU_MAX)
            return -1;
        for (i = 0; i < count; i++)
            out->choices[i] = mmo_get_s16le(&r);
        out->choice_bank = bank;
        out->choice_count = count;
        if (r.err || mmo_rbuf_remaining(&r) != 0)
            return -1;
        out->detail_len = 0;
    } else {
        out->detail_len = (int)mmo_rbuf_remaining(&r);
    }
    out->close = (out->action_type == (s8)MMO_DIALOG_ACTION_CLOSE);
    return r.err ? -1 : 0;
}

void mmo_game_write_dialog_reply(mmo_wbuf *w, u8 flags, u8 response)
{
    mmo_put_u8(w, flags);
    mmo_put_u8(w, response);
}

void mmo_game_write_entity_interact(mmo_wbuf *w, s64 entity_id, s64 token)
{
    mmo_put_s64le(w, entity_id);
    mmo_put_s64le(w, token);
}

int mmo_game_read_dialog_state(const u8 *body, size_t n, int *active)
{
    mmo_rbuf r;
    int on;

    mmo_rbuf_init(&r, body, n);
    on = (mmo_get_u8(&r) == 1);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (active)
        *active = on;
    return 0;
}

int mmo_game_read_script_move(const u8 *body, size_t n,
                              mmo_script_move *out)
{
    mmo_rbuf r;
    mmo_script_move sink;
    int count;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->entity_id = mmo_get_s64le(&r);
    out->flag = mmo_get_u8(&r);
    count = mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (count > MMO_SCRIPT_MOVE_MAX)
        return -1;
    if (mmo_rbuf_remaining(&r) != (size_t)count)
        return -1;
    out->count = (u8)count;
    if (count > 0)
        mmo_get_bytes(&r, out->actions, (size_t)count);
    return r.err ? -1 : 0;
}

int mmo_game_read_objective(const u8 *body, size_t n, mmo_objective *out)
{
    mmo_rbuf r;
    mmo_objective sink;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->id = (s8)mmo_get_u8(&r);
    out->value = mmo_get_s32le(&r);
    out->count = mmo_get_s16le(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_objective_bulk(const u8 *body, size_t n,
                                 mmo_objective *out, int cap, int *count)
{
    mmo_rbuf r;
    int i, nent;

    mmo_rbuf_init(&r, body, n);
    nent = mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (nent > cap)
        return -1;
    for (i = 0; i < nent; i++) {
        mmo_objective one;

        one.id = (s8)mmo_get_u8(&r);
        one.value = mmo_get_s32le(&r);
        one.count = mmo_get_s16le(&r);
        if (out)
            out[i] = one;
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (count)
        *count = nent;
    return 0;
}

int mmo_game_script_action(u8 flag, u8 byte)
{
    static const s16 face0[] = {
        1, /* 0x00: server FACE_DOWN, no official vo0 */
        0, /* 0x01 FACE_UP    -> NORTH */
        2, /* 0x02 FACE_LEFT  -> WEST  */
        3, /* 0x03 FACE_RIGHT -> EAST  */
        1, /* 0x04 official face-down    -> SOUTH */
    };
    static const s16 walk0[] = { 13, 12, 14, 15 }; /* S N W E */
    static const s16 fast0[] = { 17, 16, 18, 19 };

    if (flag == 1)
        return -1;
    if (byte < sizeof face0 / sizeof face0[0])
        return face0[byte];
    if (byte >= 0x10 && byte <= 0x13)
        return walk0[byte - 0x10];
    if (byte >= 0x1D && byte <= 0x20)
        return fast0[byte - 0x1D];
    if (byte == 0x1B)
        return 63; /* DELAY_8 */
    if (byte == 0x1C)
        return 65; /* DELAY_16 */
    if (byte == 0x60)
        return 69; /* SET_INVISIBLE */
    return -1;
}

void mmo_game_write_shop_buy(mmo_wbuf *w, s16 item_id, s16 quantity)
{
    mmo_put_s16le(w, item_id);
    mmo_put_s16le(w, quantity);
    mmo_put_u8(w, 0);
}

void mmo_game_write_shop_sell(mmo_wbuf *w, s64 item_entity_id, s16 quantity)
{
    mmo_put_s64le(w, item_entity_id);
    mmo_put_s16le(w, quantity);
}

void mmo_game_write_item_use(mmo_wbuf *w, u16 item_id, s64 target, s32 trailer)
{
    mmo_put_u16le(w, item_id);
    mmo_put_s64le(w, target);
    mmo_put_s32le(w, trailer);
}

/*
 * The record's own trailing list is sized from a *signed* byte on the client side (`new
 * QB[get()]`), so a length with the top bit set is a negative array size there: the exception
 * it throws leaves the character stuck on the loading screen.
 */
#define MMO_MON_TAIL_MAX  127
#define MMO_MON_LIST_MAX  255

int mmo_mon_is_shiny(u32 otid, u32 personality)
{
    return ((((otid >> 16) ^ (otid & 0xFFFFu)
              ^ (personality >> 16) ^ (personality & 0xFFFFu)) & 0xFFFFu) < 8);
}

/* Solving that rule for the personality, with the trainer id fixed (game.h). */
u32 mmo_mon_shiny_personality(u32 base, u32 otid, int shiny, int nature)
{
    u32 low = base & 0xFFu;
    u32 fold = ((otid >> 16) ^ (otid & 0xFFFFu)) & 0xFFFFu;
    int first = shiny ? 0 : 8;
    int last = shiny ? 8 : 16;
    int j, k;

    if (nature < 0 || nature >= MMO_MON_NATURES)
        nature = 0;
    /* The ordinary monster keeps the personality it has always had. */
    if (!shiny == !mmo_mon_is_shiny(otid, base))
        return base;
    for (j = 0; j < 256; j++) {
        u32 lo = low + (u32)j * 256u;

        for (k = first; k < last; k++) {
            u32 hi = (lo ^ fold ^ (u32)k) & 0xFFFFu;

            if ((11u * hi + lo) % 25u == (u32)nature)
                return (hi << 16) | lo;
        }
    }
    return base;
}

/*
 * Walk one monster record, the way the client's own reader walks it. Every field is crossed at
 * its own width, the ones without an established meaning too, since the point of the walk is
 * to end exactly where the client ends.
 */
static int read_monster(mmo_rbuf *r, mmo_monster *out)
{
    memset(out, 0, sizeof *out);
    /* Absent is not the same as region 0 bank 0 map 0, which is a real map. */
    out->caught_region = -1;
    out->caught_bank = -1;
    out->caught_map = -1;
    out->id = mmo_get_s64le(r);
    /* A flagged second id, never set in a capture; nothing follows when clear. */
    if (mmo_get_u8(r) == 1)
        mmo_get_s64le(r);
    mmo_get_u8(r);                        /* a four-valued enum, zero everywhere */
    out->owner_id = mmo_get_s64le(r);
    mmo_get_s64le(r);                     /* a second owner id, read and discarded */
    out->container = mmo_get_u8(r);
    out->slot = mmo_get_s16le(r);
    out->dex_id = mmo_get_u16le(r);
    out->seed = mmo_get_s32le(r);
    mmo_get_s64le(r);                     /* a third owner id */
    mmo_get_utf16_nt(r, out->ot, sizeof out->ot);
    mmo_get_utf16_nt(r, out->nickname, sizeof out->nickname);
    mmo_get_u8(r);
    mmo_get_u8(r);
    out->level = mmo_get_u8(r);
    out->hp = mmo_get_s16le(r);
    mmo_get_s16le(r);
    out->xp = mmo_get_s32le(r);
    /* Two bits per move slot: the PP Ups applied to it. The client's max-PP is
     * `base + floor(base * 0.2 * ups)`, so the moves cannot be shown at their
     * real pp without this byte. */
    int pp_ups = mmo_get_u8(r);
    for (int i = 0; i < MMO_MON_MOVES; i++)
        out->move_pp_up[i] = (u8)((pp_ups >> (i * 2)) & 3);
    out->friendship = (u16)mmo_get_s16le(r);
    for (int i = 0; i < MMO_MON_MOVES; i++)
        out->move_id[i] = mmo_get_u16le(r);
    for (int i = 0; i < MMO_MON_MOVES; i++)
        out->move_pp[i] = mmo_get_u8(r);
    for (int i = 0; i < MMO_MON_MOVES; i++)
        mmo_get_s16le(r);                 /* a second per-move word */
    for (int i = 0; i < MMO_MON_STATS; i++)
        out->ev[i] = mmo_get_u8(r);
    /*
     * Six of the nine bytes here have names, read out of the game client's own reader: it
     * keeps five, indexed by its contest-type enum, and discards the sixth.
     */
    for (int i = 0; i < MMO_MON_CONDITIONS; i++)
        out->cond[i] = mmo_get_u8(r);
    out->sheen = mmo_get_u8(r);
    for (int i = 0; i < 3; i++)
        mmo_get_u8(r);
    mmo_get_s32le(r);
    mmo_get_u8(r);
    out->form = mmo_get_u8(r);            /* the client pairs it with the dex id */
    out->iv_bits = mmo_get_s32le(r);
    for (int i = 0; i < MMO_MON_STATS; i++)
        out->iv[i] = (u8)((out->iv_bits >> (i * 5)) & 31);
    out->ability_slot = mmo_get_u8(r);
    mmo_get_s64le(r);
    out->rarity = mmo_get_u16le(r);
    out->caught_at = mmo_get_s32le(r);
    /* A halfword whose low byte is the egg flag; the rest is zero everywhere. */
    out->egg = (mmo_get_u16le(r) & 1) != 0;
    mmo_get_u8(r);                        /* an optional id, absent when negative */
    mmo_get_u8(r);
    /*
     * The trailing byte list, empty in every capture and this client's one extension point
     * (game.h).
     */
    int tail = mmo_get_u8(r);
    if (tail > MMO_MON_TAIL_MAX)
        return -1;
    while (tail > 0) {
        int tag, len;

        if (tail < 2)
            return -1;
        tag = mmo_get_u8(r);
        len = mmo_get_u8(r);
        tail -= 2;
        if (len > tail)
            return -1;
        tail -= len;
        if (tag == MMO_MON_TLV_RIBBONS_SUPER && len == 8) {
            out->ribbons_super = (u64)mmo_get_s64le(r);
            continue;
        }
        if (tag == MMO_MON_TLV_CAUGHT_WHERE && len == 6) {
            out->caught_region = mmo_get_s16le(r);
            out->caught_bank = mmo_get_s16le(r);
            out->caught_map = mmo_get_s16le(r);
            continue;
        }
        if (tag == MMO_MON_TLV_HELD_ITEM && len == 2) {
            out->held_item = mmo_get_u16le(r);
            continue;
        }
        if (tag == MMO_MON_TLV_CAUGHT_LABEL && len == 2) {
            out->caught_location_label = mmo_get_u16le(r);
            continue;
        }
        if (tag == MMO_MON_TLV_STATUS && len == 2) {
            out->status = mmo_get_u16le(r) & MMO_MON_STATUS_MASK;
            continue;
        }
        for (int i = 0; i < len; i++)
            mmo_get_u8(r);
    }
    if (r->err)
        return -1;

    /* Derived after the walk, because the client derives them after it too: the
     * nature is the seed reduced mod the nature table, and a hidden ability slot
     * is only kept where the client keeps it, with the hidden-ability rarity bit
     * set, or in one of the containers that hold battle monsters. */
    out->nature = (int)(((u32)out->seed) % MMO_MON_NATURES);
    if (out->ability_slot == MMO_ABILITY_SLOT_HIDDEN &&
        !(out->rarity & MMO_RARITY_HIDDEN_ABILITY) &&
        (out->container < 0 || out->container > 15 ||
         !((MMO_CONTAINER_HIDDEN_OK >> out->container) & 1)))
        out->ability_slot = 0;
    return 0;
}

/*
 * CharacterInfoCodecShort: id, name, prefix, userId, rivalSex, lastLogin, createdAt, then the
 * fixed pad/money/permissions/safari/slots/position/ repel/lure block and a U16LE-prefixed
 * trailer.
 */
static int read_character_info(mmo_rbuf *r, mmo_character *out)
{
    char prefix[8];
    u16 trailer;
    int i;

    memset(out, 0, sizeof *out);
    out->gender = -1;
    out->region = -1;

    out->id = mmo_get_s64le(r);
    mmo_get_utf16_nt(r, out->name, sizeof out->name);
    mmo_get_utf16_nt(r, prefix, sizeof prefix);
    (void)mmo_get_s32le(r);               /* userId */
    out->gender = (int)(s8)mmo_get_u8(r); /* rivalSex */
    (void)mmo_get_s32le(r);               /* lastLogin */
    (void)mmo_get_s32le(r);               /* createdAt */
    (void)mmo_get_s32le(r);
    (void)mmo_get_u8(r);
    (void)mmo_get_s32le(r);
    (void)mmo_get_s32le(r);               /* money */
    (void)mmo_get_s16le(r);
    (void)mmo_get_s32le(r);
    (void)mmo_get_u8(r);                  /* permissions */
    (void)mmo_get_u8(r);
    (void)mmo_get_u8(r);
    (void)mmo_get_s32le(r);
    for (i = 0; i < 8; i++)
        (void)mmo_get_u8(r);
    (void)mmo_get_s16le(r);               /* remainingSafariSteps */
    (void)mmo_get_u8(r);                  /* remainingSafariBalls */
    (void)mmo_get_s32le(r);
    (void)mmo_get_u8(r);                  /* pcExtraSlots */
    (void)mmo_get_u8(r);                  /* battleBoxExtraSlots */
    (void)mmo_get_u8(r);                  /* templateAmount */
    (void)mmo_get_u8(r);
    (void)mmo_get_u8(r);                  /* captured region marker */
    (void)mmo_get_u8(r);
    (void)mmo_get_u8(r);
    out->region = (int)(s8)mmo_get_u8(r); /* positionRegionId */
    (void)mmo_get_u8(r);                  /* positionBankId */
    (void)mmo_get_s16le(r);               /* positionMapId */
    (void)mmo_get_s16le(r);               /* positionX */
    (void)mmo_get_s16le(r);               /* positionY */
    (void)mmo_get_u8(r);
    (void)mmo_get_s16le(r);               /* repelLeft */
    (void)mmo_get_s16le(r);               /* repelItemId */
    (void)mmo_get_u8(r);
    (void)mmo_get_s16le(r);               /* lureItemId */
    (void)mmo_get_s16le(r);               /* lureLeft */
    trailer = mmo_get_u16le(r);
    for (i = 0; i < (int)trailer; i++)
        (void)mmo_get_u8(r);
    return r->err ? -1 : 0;
}

static int read_character_entry(mmo_rbuf *r, mmo_character *out)
{
    mmo_skin_set skins;
    int skin_count;
    char guild_name[8];
    int party;
    int i;

    if (read_character_info(r, out) != 0)
        return -1;
    read_skin_set(r, &out->appearance, &skin_count, 1);
    read_skin_set(r, &skins, &skin_count, 0);
    if (mmo_get_u8(r)) {
        mmo_get_utf16_nt(r, guild_name, sizeof guild_name);
        (void)mmo_get_s32le(r);
    }
    party = (int)mmo_get_u8(r);
    if (r->err)
        return -1;
    for (i = 0; i < party; i++) {
        mmo_monster mon;
        if (read_monster(r, &mon) != 0)
            return -1;
    }
    return r->err ? -1 : 0;
}

int mmo_game_read_character_list(const u8 *body, size_t n, mmo_character_list *out)
{
    mmo_character_list sink;
    mmo_rbuf r;
    int i;

    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    mmo_rbuf_init(&r, body, n);
    out->count = (int)mmo_get_u8(&r);
    if (r.err)
        return -1;

    for (i = 0; i < out->count; i++) {
        mmo_character ch;
        if (read_character_entry(&r, &ch) != 0)
            return -1;
        if (out->held < MMO_CHAR_LIST_MAX)
            out->entry[out->held++] = ch;
    }
    return r.err ? -1 : 0;
}

int mmo_game_read_selected_character(const u8 *body, size_t n, mmo_character *out)
{
    mmo_character sink;
    mmo_rbuf r;

    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);
    out->gender = -1;
    out->region = -1;

    if (!body)
        return -1;
    mmo_rbuf_init(&r, body, n);
    if (!mmo_get_u8(&r))
        return r.err ? -1 : 0;
    return read_character_info(&r, out);
}

static void list_names(const mmo_character_list *list, char *dst, size_t cap)
{
    size_t at = 0;
    int i;

    if (!dst || cap == 0)
        return;
    dst[0] = '\0';
    for (i = 0; i < list->held; i++) {
        const char *name = list->entry[i].name[0] ? list->entry[i].name : "?";
        int n = snprintf(dst + at, cap - at, "%s%s", at ? ", " : "", name);
        if (n < 0 || (size_t)n >= cap - at) {
            dst[cap - 1] = '\0';
            return;
        }
        at += (size_t)n;
    }
}

int mmo_game_pick_character(const mmo_character_list *list,
                            const char *name, int index, int region,
                            char *err, size_t errcap)
{
    int have_name = name && name[0];
    int have_index = index >= 0;
    int have_region = region >= 0;
    int selectors = have_name + have_index + have_region;
    char names[96];
    int i, match, found;

    if (err && errcap)
        err[0] = '\0';
    if (!list)
        return -1;

    if (selectors > 1) {
        if (err && errcap)
            snprintf(err, errcap, "say one of --character, --index or --region");
        return -1;
    }
    if (list->count < 1) {
        if (err && errcap)
            snprintf(err, errcap, "this account has no character yet");
        return -1;
    }

    if (have_name) {
        found = -1;
        for (i = 0; i < list->held; i++) {
            if (strcasecmp(list->entry[i].name, name) != 0)
                continue;
            if (found >= 0) {
                if (err && errcap)
                    snprintf(err, errcap,
                             "more than one character is named %s", name);
                return -1;
            }
            found = i;
        }
        if (found < 0) {
            list_names(list, names, sizeof names);
            if (err && errcap)
                snprintf(err, errcap, "no character named %s (have %s)",
                         name, names[0] ? names : "none");
            return -1;
        }
        return found;
    }

    if (have_index) {
        if (index >= list->count) {
            if (err && errcap)
                snprintf(err, errcap, "index %d is past the %d character(s)",
                         index, list->count);
            return -1;
        }
        if (index >= list->held) {
            if (err && errcap)
                snprintf(err, errcap, "index %d is past the %d held character(s)",
                         index, list->held);
            return -1;
        }
        return index;
    }

    if (have_region) {
        found = -1;
        match = 0;
        for (i = 0; i < list->held; i++) {
            if (list->entry[i].region != region)
                continue;
            match++;
            if (found < 0)
                found = i;
        }
        if (match == 0) {
            if (err && errcap)
                snprintf(err, errcap, "no character in that region");
            return -1;
        }
        if (match > 1) {
            if (err && errcap)
                snprintf(err, errcap,
                         "%d characters in that region; say which with --character or --index",
                         match);
            return -1;
        }
        return found;
    }

    return 0;
}

int mmo_game_read_pokemon_container(const u8 *body, size_t n,
                                    mmo_pokemon_container *out,
                                    mmo_monster *mons, int cap)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->container = mmo_get_u8(&r);
    int flags = mmo_get_u8(&r);
    out->has_change = (flags & 0x01) != 0;
    out->deleted = (flags & 0x02) != 0;
    if (r.err)
        return -1;
    if (out->deleted) {
        /* A delete drops the container and stops the reader there: no word, no
         * count, no records. */
        out->trailing = (int)mmo_rbuf_remaining(&r);
        return 0;
    }
    if (flags & 0x04) {
        out->has_unknown_word = 1;
        out->unknown_word = mmo_get_u16le(&r);
    }
    int total = mmo_get_u8(&r);
    if (r.err || total > MMO_MON_LIST_MAX)
        return -1;
    out->total = total;
    for (int i = 0; i < total; i++) {
        mmo_monster m;
        if (read_monster(&r, &m) != 0)
            return -1;
        if (out->count < cap)
            mons[out->count++] = m;
    }
    out->trailing = (int)mmo_rbuf_remaining(&r);
    return 0;
}

int mmo_game_read_monster(const u8 *body, size_t n, mmo_monster *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    if (read_monster(&r, out) != 0)
        return -1;
    return r.err ? -1 : 0;
}

int mmo_game_read_duel_invite(const u8 *body, size_t n, mmo_duel_invite *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->flags = (s8)mmo_get_u8(&r);
    out->request_type = (s8)mmo_get_u8(&r);
    mmo_get_utf16_nt(&r, out->name, sizeof out->name);
    return r.err ? -1 : 0;
}

void mmo_game_write_duel_response(mmo_wbuf *w, int accepted, const char *text)
{
    mmo_put_u8(w, accepted ? 0 : 1);
    mmo_put_utf16_nt(w, text != NULL ? text : "");
}

int mmo_game_read_duel_outcome(const u8 *body, size_t n, int *out_packed)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    int packed = (s8)mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (out_packed != NULL)
        *out_packed = packed;
    return 0;
}

int mmo_game_read_link_battle_open(const u8 *body, size_t n,
                                   mmo_link_battle_open *out,
                                   mmo_monster *mons, int cap)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->battle_id = mmo_get_s32le(&r);
    out->net_id = mmo_get_u8(&r);
    mmo_get_utf16_nt(&r, out->peer_name, sizeof out->peer_name);
    out->peer_gender = mmo_get_u8(&r);
    int total = mmo_get_u8(&r);
    if (r.err || total > MMO_MON_LIST_MAX)
        return -1;
    out->total = total;
    for (int i = 0; i < total; i++) {
        mmo_monster m;
        if (read_monster(&r, &m) != 0)
            return -1;
        if (out->count < cap)
            mons[out->count++] = m;
    }
    if (r.err)
        return -1;
    /* The opponent's body, appended after the party by a server newer than the
     * packet. A server without it leaves nothing here and the seat is still
     * good, the fight is drawn with the gender's own trainer, which is what
     * every client did before the field existed. */
    if (mmo_rbuf_remaining(&r) > 0) {
        int skins = 0;

        read_skin_set(&r, &out->appearance, &skins, 1);
        if (r.err)
            return -1;
        out->has_appearance = 1;
    }
    return 0;
}

int mmo_game_read_link_battle_data(const u8 *body, size_t n,
                                   mmo_link_battle_data *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->battle_id = mmo_get_s32le(&r);
    out->kind = mmo_get_u8(&r);
    int len = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    /* A blob wider than the engine can build is a stream this client cannot be
     * in the middle of, so it is refused whole rather than truncated: half a
     * link message delivered to the battle scene is a fight that never
     * happened. */
    if (len > MMO_LINK_BLOB_MAX)
        return -1;
    mmo_get_bytes(&r, out->data, (size_t)len);
    if (r.err)
        return -1;
    out->len = len;
    return 0;
}

void mmo_game_write_link_battle_data(mmo_wbuf *w, int battle_id, int kind,
                                     const u8 *data, int len)
{
    if (len < 0)
        len = 0;
    if (len > MMO_LINK_BLOB_MAX)
        len = MMO_LINK_BLOB_MAX;
    mmo_put_s32le(w, (s32)battle_id);
    mmo_put_u8(w, (u8)kind);
    mmo_put_u16le(w, (u16)len);
    if (len > 0)
        mmo_put_bytes(w, data, (size_t)len);
}

int mmo_game_read_trade_state(const u8 *body, size_t n, mmo_trade_state *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->state = (s8)mmo_get_u8(&r);
    out->role = (s8)mmo_get_u8(&r);
    out->peer_gender = (s8)mmo_get_u8(&r);
    mmo_get_utf16_nt(&r, out->peer, sizeof out->peer);
    return r.err ? -1 : 0;
}

int mmo_game_read_trade_comm(const u8 *body, size_t n, mmo_trade_comm *out)
{
    mmo_rbuf r;
    size_t len;

    mmo_rbuf_init(&r, body, n);
    memset(out, 0, sizeof *out);
    out->channel = mmo_get_u8(&r);
    out->cmd = mmo_get_s16le(&r);
    len = mmo_get_u16le(&r);
    if (r.err || len > MMO_TRADE_COMM_MAX)
        return -1;
    mmo_get_bytes(&r, out->data, len);
    if (r.err || r.pos != r.len)
        return -1;
    out->len = (int)len;
    return 0;
}

void mmo_game_write_trade_comm(mmo_wbuf *w, int channel, int cmd,
                               const u8 *data, int len)
{
    if (len < 0)
        len = 0;
    if (len > MMO_TRADE_COMM_MAX)
        len = MMO_TRADE_COMM_MAX;
    mmo_put_u8(w, (u8)channel);
    mmo_put_s16le(w, (s16)cmd);
    mmo_put_u16le(w, (u16)len);
    if (len > 0)
        mmo_put_bytes(w, data, (size_t)len);
}

void mmo_game_write_trade_action(mmo_wbuf *w, int action)
{
    mmo_put_u8(w, (u8)action);
}

void mmo_game_write_trade_select(mmo_wbuf *w, int slot)
{
    mmo_put_s32le(w, (s32)slot);
}

void mmo_game_write_gtl_open(mmo_wbuf *w, s64 session_ts)
{
    mmo_put_u8(w, 0);
    mmo_put_s64le(w, session_ts);
}

/* The GtlFilterKind ids this client emits. The server ignores the rest. */
#define GTL_FILTER_SPECIES   0
#define GTL_FILTER_NATURE    2
#define GTL_FILTER_MIN_LEVEL 3
#define GTL_FILTER_MAX_LEVEL 4
#define GTL_FILTER_SHINY     8
#define GTL_FILTER_MIN_PRICE 9
#define GTL_FILTER_MAX_PRICE 10

void mmo_game_write_gtl_search_req(mmo_wbuf *w, int request_id, int kind,
                                   int sort, int page,
                                   const mmo_gtl_search *filter)
{
    static const mmo_gtl_search none = { 0, -1, -1, -1, -1, -1, -1 };
    const mmo_gtl_search *f = filter != NULL ? filter : &none;
    int count = (f->species != 0) + (f->min_level >= 0) + (f->max_level >= 0) +
                (f->shiny >= 0) + (f->nature >= 0) + (f->min_price >= 0) +
                (f->max_price >= 0);

    mmo_put_u8(w, (u8)request_id);
    mmo_put_u8(w, (u8)kind);
    mmo_put_u8(w, (u8)sort);
    mmo_put_s16le(w, (s16)page);
    mmo_put_u8(w, (u8)count);
    if (f->species != 0) {
        mmo_put_u8(w, GTL_FILTER_SPECIES);
        mmo_put_u16le(w, 1);
        mmo_put_s16le(w, (s16)f->species);
    }
    if (f->min_level >= 0) {
        mmo_put_u8(w, GTL_FILTER_MIN_LEVEL);
        mmo_put_u8(w, (u8)f->min_level);
    }
    if (f->max_level >= 0) {
        mmo_put_u8(w, GTL_FILTER_MAX_LEVEL);
        mmo_put_u8(w, (u8)f->max_level);
    }
    if (f->shiny >= 0) {
        mmo_put_u8(w, GTL_FILTER_SHINY);
        mmo_put_u8(w, (u8)(f->shiny ? 1 : 0));
    }
    if (f->nature >= 0) {
        mmo_put_u8(w, GTL_FILTER_NATURE);
        mmo_put_u8(w, (u8)f->nature);
    }
    if (f->min_price >= 0) {
        mmo_put_u8(w, GTL_FILTER_MIN_PRICE);
        mmo_put_s32le(w, f->min_price);
    }
    if (f->max_price >= 0) {
        mmo_put_u8(w, GTL_FILTER_MAX_PRICE);
        mmo_put_s32le(w, f->max_price);
    }
}

void mmo_game_write_gtl_create_mon(mmo_wbuf *w, s64 mon_id, s32 price)
{
    mmo_put_u8(w, MMO_GTL_KIND_POKEMON);
    mmo_put_s64le(w, mon_id);
    mmo_put_s32le(w, price);
    mmo_put_s16le(w, 1);
}

void mmo_game_write_gtl_create_item(mmo_wbuf *w, u16 item_id, s32 quantity,
                                    s32 price)
{
    mmo_put_u8(w, MMO_GTL_KIND_ITEM);
    mmo_put_s64le(w, (s64)item_id);
    mmo_put_s32le(w, price);
    mmo_put_s16le(w, (s16)quantity);
}

void mmo_game_write_gtl_cancel(mmo_wbuf *w, s64 listing_id)
{
    char text[24];
    snprintf(text, sizeof text, "%lld", (long long)listing_id);
    mmo_put_utf16_nt(w, text);
}

void mmo_game_write_gtl_buy(mmo_wbuf *w, s64 listing_id, int quantity)
{
    mmo_put_s64le(w, listing_id);
    mmo_put_s16le(w, (s16)quantity);
}

void mmo_game_write_gtl_claim(mmo_wbuf *w, const s64 *listing_ids, int count)
{
    mmo_put_u8(w, (u8)count);
    for (int i = 0; i < count; i++)
        mmo_put_s64le(w, listing_ids[i]);
}

void mmo_game_write_gtl_price(mmo_wbuf *w, s64 listing_id, s32 new_price)
{
    mmo_put_s64le(w, listing_id);
    mmo_put_s32le(w, new_price);
}

int mmo_game_read_gtl_flags(const u8 *body, size_t n, mmo_gtl_flags *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->entry_kind = mmo_get_u8(&r);
    out->timestamp_ms = mmo_get_s64le(&r);
    out->flag_count = mmo_get_u16le(&r);
    for (int i = 0; i < out->flag_count && !r.err; i++)
        mmo_get_u8(&r);
    return r.err ? -1 : 0;
}

int mmo_game_read_gtl_page(const u8 *body, size_t n, mmo_gtl_page *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->request_id = (s8)mmo_get_u8(&r);
    out->kind = mmo_get_u8(&r);
    out->page = mmo_get_s16le(&r);
    out->total = mmo_get_s32le(&r);
    int count = mmo_get_u8(&r);
    if (r.err || count > MMO_GTL_PAGE_ROWS)
        return -1;
    for (int i = 0; i < count; i++) {
        mmo_gtl_row *row = &out->rows[i];
        row->listing_id = mmo_get_s64le(&r);
        row->kind = mmo_get_u8(&r);
        row->price = mmo_get_s32le(&r);
        row->listed_at = mmo_get_s32le(&r);
        row->expires_at = mmo_get_s32le(&r);
        row->quantity = mmo_get_s16le(&r);
        if (r.err)
            return -1;
        if (row->kind == MMO_GTL_KIND_ITEM) {
            row->item_id = (u16)mmo_get_s16le(&r);
            row->item_state = (s8)mmo_get_u8(&r);
        } else {
            row->have_mon = mmo_get_u8(&r) == 1;
            if (row->have_mon) {
                if (read_monster(&r, &row->mon) != 0)
                    return -1;
                for (int s = 0; s < MMO_MON_STATS; s++)
                    row->stats[s] = mmo_get_s16le(&r);
            }
        }
        if (out->kind == MMO_GTL_KIND_OWN) {
            row->own_state = (s8)mmo_get_u8(&r);
            row->own_remaining = mmo_get_s16le(&r);
            row->own_unclaimed = mmo_get_s16le(&r);
        }
        if (r.err)
            return -1;
    }
    out->count = count;
    if (out->kind == MMO_GTL_KIND_ITEM) {
        int quotes = mmo_get_u8(&r);
        if (r.err || quotes > MMO_GTL_PAGE_ROWS)
            return -1;
        for (int i = 0; i < quotes; i++) {
            out->quotes[i].item_id = (u16)mmo_get_s16le(&r);
            out->quotes[i].price = mmo_get_s32le(&r);
        }
        out->quote_count = quotes;
    }
    if (r.err || r.pos != r.len)
        return -1;
    return 0;
}

void mmo_game_write_gtl_market_buy(mmo_wbuf *w, u16 item_id, int quantity,
                                   s32 budget)
{
    mmo_put_u8(w, MMO_GTL_KIND_ITEM);
    mmo_put_s16le(w, (s16)item_id);
    mmo_put_s16le(w, (s16)quantity);
    mmo_put_s32le(w, budget);
    mmo_put_u8(w, 0xFF);
}

int mmo_game_read_gtl_result(const u8 *body, size_t n, mmo_gtl_result *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->code = (s8)mmo_get_u8(&r);
    out->a = mmo_get_s64le(&r);
    out->b = mmo_get_s32le(&r);
    if (r.err || r.pos != r.len)
        return -1;
    return 0;
}

int mmo_game_read_gtl_log(const u8 *body, size_t n, mmo_gtl_log *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    int objects = mmo_get_u8(&r);
    for (int i = 0; i < objects && !r.err; i++) {
        mmo_gtl_log_row scratch;
        mmo_gtl_log_row *row =
            i < MMO_GTL_LOG_ROWS ? &out->rows[i] : &scratch;
        memset(row, 0, sizeof *row);
        row->sale_id = mmo_get_s64le(&r);
        row->type = mmo_get_u8(&r);
        int frames = mmo_get_u8(&r);
        for (int f = 0; f < frames && !r.err; f++) {
            s16 a = mmo_get_s16le(&r);
            s16 b = mmo_get_s16le(&r);
            s32 c = mmo_get_s32le(&r);
            int flag = mmo_get_u8(&r) != 0;
            if (f == 0) {
                row->what = a;
                row->amount = b;
                row->total = c;
                row->bought = flag;
            } else if (f == 1) {
                row->epoch = c;
            }
        }
    }
    if (r.err)
        return -1;
    out->count = objects < MMO_GTL_LOG_ROWS ? objects : MMO_GTL_LOG_ROWS;
    return 0;
}

int mmo_game_read_underground_talk(const u8 *body, size_t n,
                                   mmo_underground_talk *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    memset(out, 0, sizeof *out);
    out->kind = mmo_get_u8(&r);
    out->entity_id = mmo_get_s64le(&r);
    int len = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    /* Same rule as a link blob: half a comm command handed to the engine is a
     * conversation that never happened, so a body past what the Underground
     * can build is refused whole rather than cut. */
    if (len > MMO_UG_TALK_MAX)
        return -1;
    mmo_get_bytes(&r, out->data, (size_t)len);
    if (r.err)
        return -1;
    out->len = len;
    return 0;
}

void mmo_game_write_underground_talk(mmo_wbuf *w, int kind, s64 entity_id,
                                     const u8 *data, int len)
{
    if (len < 0)
        len = 0;
    if (len > MMO_UG_TALK_MAX)
        len = MMO_UG_TALK_MAX;
    mmo_put_u8(w, (u8)kind);
    mmo_put_s64le(w, entity_id);
    mmo_put_u16le(w, (u16)len);
    if (len > 0)
        mmo_put_bytes(w, data, (size_t)len);
}

int mmo_game_read_join_response(const u8 *body, size_t n, mmo_join_response *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    out->can_join = mmo_get_bool(&r);
    out->playtime = 0;
    out->reward_points = 0;
    out->balance = 0;
    if (r.err)
        return -1;
    if (!out->can_join)
        return 0;

    char empty[8];
    mmo_get_utf16_nt(&r, empty, sizeof empty);  /* trailing "" */
    mmo_get_u8(&r);                             /* S8 pad */
    out->playtime = mmo_get_s32le(&r);
    out->reward_points = mmo_get_s32le(&r);
    out->balance = mmo_get_s32le(&r);
    mmo_get_s32le(&r);                          /* serverDayStartSecond */
    mmo_get_s32le(&r);                          /* serverCurrentSecond */
    return r.err ? -1 : 0;
}

static int read_ql1(mmo_rbuf *r, char *name, size_t name_cap,
                    u8 *unk0, s32 *last_seen, u8 *kind, u8 *packed,
                    s16 *sprite)
{
    int i;

    mmo_get_utf16_nt(r, name, name_cap);
    *unk0 = mmo_get_u8(r);
    *last_seen = mmo_get_s32le(r);
    *kind = mmo_get_u8(r);
    *packed = mmo_get_u8(r);
    for (i = 0; i < MMO_FRIEND_SPRITE_COUNT; i++)
        sprite[i] = mmo_get_s16le(r);
    return r->err ? -1 : 0;
}

static int read_friend_row(mmo_rbuf *r, mmo_friend *out)
{
    memset(out, 0, sizeof *out);
    out->player = mmo_get_s64le(r);
    out->unknown = mmo_get_s32le(r);
    out->online = mmo_get_u8(r) == 1;
    return read_ql1(r, out->name, sizeof out->name, &out->unk0,
                    &out->last_seen, &out->kind, &out->packed_slots,
                    out->sprite);
}

int mmo_game_read_friend(const u8 *body, size_t n, mmo_friend *out)
{
    mmo_rbuf r;
    mmo_friend sink;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    if (read_friend_row(&r, out) != 0)
        return -1;
    if (mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_friend_list(const u8 *body, size_t n,
                              mmo_friend *out, int cap, int *count, u8 *mode)
{
    mmo_rbuf r;
    int i, nent;
    u8 md;

    mmo_rbuf_init(&r, body, n);
    md = mmo_get_u8(&r);
    nent = mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (nent > cap)
        return -1;
    for (i = 0; i < nent; i++) {
        mmo_friend one;

        if (read_friend_row(&r, &one) != 0)
            return -1;
        if (out)
            out[i] = one;
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (mode)
        *mode = md;
    if (count)
        *count = nent;
    return 0;
}

int mmo_game_read_friend_delete(const u8 *body, size_t n, s64 *player)
{
    mmo_rbuf r;
    s64 id;

    mmo_rbuf_init(&r, body, n);
    id = mmo_get_s64le(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (player)
        *player = id;
    return 0;
}

int mmo_game_read_friend_online(const u8 *body, size_t n,
                                s64 *player, u8 *online)
{
    mmo_rbuf r;
    s64 id;
    u8 bit;

    mmo_rbuf_init(&r, body, n);
    id = mmo_get_s64le(&r);
    bit = mmo_get_u8(&r) == 1;
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (player)
        *player = id;
    if (online)
        *online = bit;
    return 0;
}

void mmo_game_write_friend_name(mmo_wbuf *w, const char *name)
{
    mmo_put_utf16_nt(w, name ? name : "");
}

static int read_guild_profile(mmo_rbuf *r, mmo_guild_profile *out)
{
    int i, nrank;

    memset(out, 0, sizeof *out);
    out->guild_id = mmo_get_s64le(r);
    mmo_get_utf16_nt(r, out->name, sizeof out->name);
    mmo_get_utf16_nt(r, out->tag, sizeof out->tag);
    out->founded_at = mmo_get_s32le(r);
    mmo_get_utf16_nt(r, out->message, sizeof out->message);
    out->unknown = mmo_get_s32le(r);
    for (i = 0; i < MMO_GUILD_PERM_COUNT; i++)
        out->perm[i] = mmo_get_s16le(r);
    out->expiry = mmo_get_s32le(r);
    nrank = mmo_get_u8(r);
    if (r->err)
        return -1;
    if (nrank > MMO_GUILD_RANK_MAX)
        return -1;
    out->rank_count = nrank;
    for (i = 0; i < nrank; i++)
        mmo_get_utf16_nt(r, out->rank_label[i], sizeof out->rank_label[i]);
    return r->err ? -1 : 0;
}

int mmo_game_read_guild_profile(const u8 *body, size_t n, mmo_guild_profile *out)
{
    mmo_rbuf r;
    mmo_guild_profile sink;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    if (read_guild_profile(&r, out) != 0)
        return -1;
    if (mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_guild_membership(const u8 *body, size_t n,
                                   int *in_guild, mmo_guild_profile *out)
{
    mmo_rbuf r;
    mmo_guild_profile sink;
    u8 flag;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    flag = mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (flag == 1) {
        if (read_guild_profile(&r, out) != 0)
            return -1;
    } else {
        memset(out, 0, sizeof *out);
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (in_guild)
        *in_guild = (flag == 1);
    return 0;
}

static int read_guild_member(mmo_rbuf *r, mmo_guild_member *out)
{
    memset(out, 0, sizeof *out);
    out->rank = mmo_get_u8(r);
    out->entity_id = mmo_get_s64le(r);
    out->joined_at = mmo_get_s32le(r);
    if (read_ql1(r, out->appearance.name, sizeof out->appearance.name,
                 &out->appearance.unk0, &out->appearance.last_seen,
                 &out->appearance.kind, &out->appearance.packed_slots,
                 out->appearance.sprite) != 0)
        return -1;
    out->online = mmo_get_u8(r) == 1;
    return r->err ? -1 : 0;
}

int mmo_game_read_guild_member(const u8 *body, size_t n, mmo_guild_member *out)
{
    mmo_rbuf r;
    mmo_guild_member sink;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    if (read_guild_member(&r, out) != 0)
        return -1;
    if (mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_guild_members(const u8 *body, size_t n,
                                mmo_guild_member *out, int cap,
                                int *count, u8 *replace)
{
    mmo_rbuf r;
    int i, nent;
    u8 repl;

    mmo_rbuf_init(&r, body, n);
    repl = mmo_get_u8(&r);
    nent = mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (nent > cap)
        return -1;
    for (i = 0; i < nent; i++) {
        mmo_guild_member one;

        if (read_guild_member(&r, &one) != 0)
            return -1;
        if (out)
            out[i] = one;
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (replace)
        *replace = (repl == 1);
    if (count)
        *count = nent;
    return 0;
}

int mmo_game_read_guild_rank_change(const u8 *body, size_t n,
                                    s64 *member, u8 *rank)
{
    mmo_rbuf r;
    s64 id;
    u8 rk;

    mmo_rbuf_init(&r, body, n);
    (void)mmo_get_s64le(&r);
    id = mmo_get_s64le(&r);
    rk = mmo_get_u8(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (member)
        *member = id;
    if (rank)
        *rank = rk;
    return 0;
}

int mmo_game_read_guild_member_drop(const u8 *body, size_t n, s64 *member)
{
    mmo_rbuf r;
    s64 id;

    mmo_rbuf_init(&r, body, n);
    (void)mmo_get_s64le(&r);
    id = mmo_get_s64le(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (member)
        *member = id;
    return 0;
}

int mmo_game_read_guild_presence(const u8 *body, size_t n,
                                 s64 *member, u8 *online)
{
    mmo_rbuf r;
    s64 id;
    u8 bit;

    mmo_rbuf_init(&r, body, n);
    id = mmo_get_s64le(&r);
    bit = mmo_get_u8(&r) == 1;
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (member)
        *member = id;
    if (online)
        *online = bit;
    return 0;
}

int mmo_game_read_guild_log(const u8 *body, size_t n,
                            mmo_guild_log_entry *out, int cap,
                            int *count, s16 *total)
{
    mmo_rbuf r;
    int i, nent;
    s16 tot;

    mmo_rbuf_init(&r, body, n);
    tot = mmo_get_s16le(&r);
    nent = mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (nent > cap)
        return -1;
    for (i = 0; i < nent; i++) {
        mmo_guild_log_entry one;

        memset(&one, 0, sizeof one);
        one.type = mmo_get_u8(&r);
        mmo_get_utf16_nt(&r, one.actor, sizeof one.actor);
        mmo_get_utf16_nt(&r, one.target, sizeof one.target);
        one.timestamp = mmo_get_s32le(&r);
        if (r.err)
            return -1;
        if (out)
            out[i] = one;
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (total)
        *total = tot;
    if (count)
        *count = nent;
    return 0;
}

void mmo_game_write_guild_create(mmo_wbuf *w, const char *name, const char *tag)
{
    mmo_put_utf16_nt(w, name ? name : "");
    mmo_put_utf16_nt(w, tag ? tag : "");
}

void mmo_game_write_guild_invite(mmo_wbuf *w, const char *name)
{
    mmo_put_utf16_nt(w, name ? name : "");
}

void mmo_game_write_guild_leave(mmo_wbuf *w)
{
    (void)w;
}

void mmo_game_write_guild_disband(mmo_wbuf *w, int initiate, s64 guild_id)
{
    mmo_put_u8(w, initiate ? 1 : 0);
    mmo_put_s64le(w, guild_id);
}

void mmo_game_write_guild_kick(mmo_wbuf *w, s64 member)
{
    mmo_put_s64le(w, member);
}

void mmo_game_write_guild_rank(mmo_wbuf *w, s64 member, u8 rank)
{
    mmo_put_s64le(w, member);
    mmo_put_u8(w, rank);
}

void mmo_game_write_guild_motd(mmo_wbuf *w, const char *text)
{
    mmo_put_utf16_nt(w, text ? text : "");
}

void mmo_game_write_guild_rank_label(mmo_wbuf *w, u8 rank, const char *label)
{
    mmo_put_u8(w, rank);
    mmo_put_utf16_nt(w, label ? label : "");
}

void mmo_game_write_guild_perms(mmo_wbuf *w, const s16 *masks)
{
    int i;

    for (i = 0; i < MMO_GUILD_PERM_COUNT; i++)
        mmo_put_s16le(w, masks ? masks[i] : 0);
}

void mmo_game_write_guild_log_req(mmo_wbuf *w, s16 page)
{
    mmo_put_s16le(w, page);
}

static int read_mail_row(mmo_rbuf *r, int sent, int with_body, mmo_mail *out)
{
    int n;

    memset(out, 0, sizeof *out);
    out->mail_id = mmo_get_s64le(r);
    out->recipient_id = mmo_get_s64le(r);
    out->sender_id = mmo_get_s64le(r);
    out->staff_kind = mmo_get_u8(r);
    if (!sent)
        mmo_get_utf16_nt(r, out->sender, sizeof out->sender);
    else
        mmo_get_utf16_nt(r, out->recipient, sizeof out->recipient);
    out->sent_at = mmo_get_s32le(r);
    mmo_get_utf16_nt(r, out->subject, sizeof out->subject);
    if (with_body)
        mmo_get_utf16_nt(r, out->body, sizeof out->body);
    out->unread = mmo_get_u8(r);
    out->has_attachments = mmo_get_u8(r) == 1;
    if (with_body) {
        n = mmo_get_u8(r);
        if (r->err)
            return -1;
        if (n != 0)
            return -1;
    }
    return r->err ? -1 : 0;
}

int mmo_game_read_mail(const u8 *body, size_t n, int sent, int with_body,
                       mmo_mail *out)
{
    mmo_rbuf r;
    mmo_mail sink;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    if (read_mail_row(&r, sent, with_body, out) != 0)
        return -1;
    if (mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_mail_page(const u8 *body, size_t n,
                            mmo_mail *out, int cap, int *count,
                            s16 *page, u8 *sent)
{
    mmo_rbuf r;
    int i, nent;
    s16 pg;
    u8 box;

    mmo_rbuf_init(&r, body, n);
    pg = mmo_get_s16le(&r);
    box = mmo_get_u8(&r) == 1;
    nent = mmo_get_s16le(&r);
    if (r.err)
        return -1;
    if (nent < 0 || nent > cap)
        return -1;
    for (i = 0; i < nent; i++) {
        mmo_mail one;

        if (read_mail_row(&r, box, 0, &one) != 0)
            return -1;
        if (out)
            out[i] = one;
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (page)
        *page = pg;
    if (sent)
        *sent = box;
    if (count)
        *count = nent;
    return 0;
}

int mmo_game_read_mail_detail(const u8 *body, size_t n, mmo_mail *out,
                              int *present, u8 *sent)
{
    mmo_rbuf r;
    mmo_mail sink;
    u8 here, box;

    mmo_rbuf_init(&r, body, n);
    here = mmo_get_u8(&r) == 1;
    if (r.err)
        return -1;
    if (!here) {
        if (mmo_rbuf_remaining(&r) != 0)
            return -1;
        if (present)
            *present = 0;
        if (sent)
            *sent = 0;
        return 0;
    }
    box = mmo_get_u8(&r) == 1;
    if (!out)
        out = &sink;
    if (read_mail_row(&r, box, 1, out) != 0)
        return -1;
    if (mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (present)
        *present = 1;
    if (sent)
        *sent = box;
    return 0;
}

int mmo_game_read_mail_counts(const u8 *body, size_t n,
                              s16 *inbox, s16 *inbox_seen, s16 *sent)
{
    mmo_rbuf r;
    s16 a, b, c;

    mmo_rbuf_init(&r, body, n);
    a = mmo_get_s16le(&r);
    b = mmo_get_s16le(&r);
    c = mmo_get_s16le(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (inbox)
        *inbox = a;
    if (inbox_seen)
        *inbox_seen = b;
    if (sent)
        *sent = c;
    return 0;
}

int mmo_game_read_mail_result(const u8 *body, size_t n, u8 *code)
{
    mmo_rbuf r;
    u8 v;

    mmo_rbuf_init(&r, body, n);
    v = mmo_get_u8(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (code)
        *code = v;
    return 0;
}

void mmo_game_write_mail_compose(mmo_wbuf *w, const char *recipient,
                                 const char *subject, const char *body)
{
    mmo_put_utf16_nt(w, recipient ? recipient : "");
    mmo_put_utf16_nt(w, subject ? subject : "");
    mmo_put_utf16_nt(w, body ? body : "");
    mmo_put_u8(w, 0);
}

void mmo_game_write_mail_page_req(mmo_wbuf *w, s16 page, int sent)
{
    mmo_put_s16le(w, page);
    mmo_put_u8(w, sent ? 1 : 0);
}

void mmo_game_write_mail_detail_req(mmo_wbuf *w, s64 mail_id)
{
    mmo_put_s64le(w, mail_id);
}

void mmo_game_write_mail_delete(mmo_wbuf *w, s64 mail_id, s16 page)
{
    mmo_put_s64le(w, mail_id);
    mmo_put_s16le(w, page);
}

static int read_nr1(mmo_rbuf *r)
{
    int i, n;

    n = mmo_get_u8(r);
    if (r->err)
        return -1;
    if (n < 1)
        return 0;
    (void)mmo_get_u8(r);
    for (i = 0; i < n; i++) {
        (void)mmo_get_s64le(r);
        (void)mmo_get_s16le(r);
        (void)mmo_get_u8(r);
        (void)mmo_get_u8(r);
        (void)mmo_get_s16le(r);
    }
    return r->err ? -1 : 0;
}

static int read_link_member(mmo_rbuf *r, mmo_link_member *out)
{
    memset(out, 0, sizeof *out);
    out->entity_id = mmo_get_s64le(r);
    if (read_ql1(r, out->name, sizeof out->name, &out->unk0,
                 &out->last_seen, &out->kind, &out->packed_slots,
                 out->sprite) != 0)
        return -1;
    return read_nr1(r);
}

int mmo_game_read_link_member(const u8 *body, size_t n, mmo_link_member *out)
{
    mmo_rbuf r;
    mmo_link_member sink;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    if (read_link_member(&r, out) != 0)
        return -1;
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_link_snapshot(const u8 *body, size_t n,
                                mmo_link_member *out, int cap, int *count,
                                int *present, s64 *leader)
{
    mmo_rbuf r;
    int i, nent, here;
    s64 lead = 0;
    u8 flag;

    mmo_rbuf_init(&r, body, n);
    flag = mmo_get_u8(&r);
    here = flag == 1;
    if (r.err)
        return -1;
    if (!here) {
        if (mmo_rbuf_remaining(&r) != 0)
            return -1;
        if (present)
            *present = 0;
        if (count)
            *count = 0;
        if (leader)
            *leader = 0;
        return 0;
    }
    lead = mmo_get_s64le(&r);
    nent = mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (nent > cap)
        return -1;
    for (i = 0; i < nent; i++) {
        mmo_link_member one;

        if (read_link_member(&r, &one) != 0)
            return -1;
        if (out)
            out[i] = one;
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (present)
        *present = 1;
    if (count)
        *count = nent;
    if (leader)
        *leader = lead;
    return 0;
}

int mmo_game_read_link_remove(const u8 *body, size_t n,
                              s64 *removed, s64 *leader)
{
    mmo_rbuf r;
    s64 gone, lead;

    mmo_rbuf_init(&r, body, n);
    gone = mmo_get_s64le(&r);
    lead = mmo_get_s64le(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (removed)
        *removed = gone;
    if (leader)
        *leader = lead;
    return 0;
}

int mmo_game_read_link_leader(const u8 *body, size_t n, s64 *leader)
{
    mmo_rbuf r;
    s64 id;

    mmo_rbuf_init(&r, body, n);
    id = mmo_get_s64le(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (leader)
        *leader = id;
    return 0;
}

void mmo_game_write_link_invite(mmo_wbuf *w, const char *name)
{
    mmo_put_utf16_nt(w, name ? name : "");
}

void mmo_game_write_link_id(mmo_wbuf *w, s64 entity_id)
{
    mmo_put_s64le(w, entity_id);
}

static int ui_menu_type_ok(u8 type)
{
    return type < MMO_UI_MENU_TYPES;
}

int mmo_game_read_menu_visibility(const u8 *body, size_t n,
                                  mmo_menu_visibility *out)
{
    mmo_rbuf r;
    mmo_menu_visibility sink;
    u8 enabled;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    enabled = mmo_get_u8(&r);
    out->enabled = (enabled == 1);
    if (out->enabled) {
        u8 type = mmo_get_u8(&r);

        if (!ui_menu_type_ok(type))
            return -1;
        out->menu_type = (s8)type;
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_menu_page(const u8 *body, size_t n, mmo_menu_page *out)
{
    mmo_rbuf r;
    mmo_menu_page sink;
    u8 type, present;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    type = mmo_get_u8(&r);
    if (!ui_menu_type_ok(type))
        return -1;
    out->menu_type = (s8)type;
    present = mmo_get_u8(&r);
    out->present = (present == 1);
    if (out->present) {
        u8 inner_type;
        int len;

        (void)mmo_get_s64le(&r);
        inner_type = mmo_get_u8(&r);
        (void)mmo_get_u8(&r);
        if (!ui_menu_type_ok(inner_type))
            return -1;
        len = (int)mmo_get_u16le(&r);
        if (r.err || len < 0 || len > MMO_UI_PAGE_MAX)
            return -1;
        out->len = len;
        if (len > 0)
            mmo_get_bytes(&r, out->bytes, (size_t)len);
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_name_choices(const u8 *body, size_t n, mmo_name_choices *out)
{
    mmo_rbuf r;
    mmo_name_choices sink;
    int i, count;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->kind = (s8)mmo_get_u8(&r);
    out->flag = (mmo_get_u8(&r) == 1);
    count = (int)mmo_get_u8(&r);
    if (r.err || count > MMO_UI_NAME_MAX)
        return -1;
    out->count = count;
    for (i = 0; i < count; i++) {
        out->entry[i].entity_id = mmo_get_s64le(&r);
        mmo_get_utf16_nt(&r, out->entry[i].name, sizeof out->entry[i].name);
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_option_list(const u8 *body, size_t n, mmo_option_list *out)
{
    mmo_rbuf r;
    mmo_option_list sink;
    int i, count;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->kind = (s8)mmo_get_u8(&r);
    count = (int)mmo_get_u8(&r);
    if (r.err || count > MMO_UI_OPTION_MAX)
        return -1;
    out->count = count;
    for (i = 0; i < count; i++) {
        int v;

        (void)mmo_get_s64le(&r);
        (void)mmo_get_u8(&r);
        out->entry[i].type_id = (s8)mmo_get_u8(&r);
        out->entry[i].sub_type = (s8)mmo_get_u8(&r);
        for (v = 0; v < 5; v++)
            out->entry[i].value[v] = mmo_get_s16le(&r);
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_list_window(const u8 *body, size_t n, mmo_list_window *out)
{
    mmo_rbuf r;
    mmo_list_window sink;
    int i, count;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->window_id = mmo_get_s32le(&r);
    out->first_page = (mmo_get_u8(&r) == 1);
    out->last_page = (mmo_get_u8(&r) == 1);
    out->header[0] = (s8)mmo_get_u8(&r);
    out->header[1] = (s8)mmo_get_u8(&r);
    out->header[2] = (s8)mmo_get_u8(&r);
    count = (int)mmo_get_u16le(&r);
    if (r.err || count > MMO_UI_ROW_MAX)
        return -1;
    out->count = count;
    for (i = 0; i < count; i++) {
        int v;

        out->row[i].row_type = (s8)mmo_get_u8(&r);
        for (v = 0; v < 5; v++)
            out->row[i].value[v] = mmo_get_s16le(&r);
        mmo_get_utf16_nt(&r, out->row[i].label, sizeof out->row[i].label);
        out->row[i].extra = mmo_get_s16le(&r);
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_confirm_prompt(const u8 *body, size_t n,
                                 mmo_confirm_prompt *out)
{
    mmo_rbuf r;
    mmo_confirm_prompt sink;
    u8 visible;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    visible = mmo_get_u8(&r);
    out->visible = (visible == 1);
    if (out->visible) {
        out->entity_id = mmo_get_s64le(&r);
        out->request_s = mmo_get_u16le(&r);
        out->response_s = mmo_get_u16le(&r);
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_menu_prompt_open(const u8 *body, size_t n,
                                   mmo_menu_prompt *out)
{
    mmo_rbuf r;
    mmo_menu_prompt sink;
    int kind;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    kind = (int)(s8)mmo_get_u8(&r);
    out->kind = (s8)kind;
    if (kind == 6) {
        int nrel = (int)mmo_get_u8(&r);
        int i;

        if (r.err || nrel < 0)
            return -1;
        out->relation_count = nrel;
        for (i = 0; i < nrel; i++) {
            int nstat, s;

            (void)mmo_get_s64le(&r);
            nstat = (int)mmo_get_u8(&r);
            for (s = 0; s < nstat; s++) {
                int nids, k;

                (void)mmo_get_u8(&r);
                (void)mmo_get_u8(&r);
                nids = (int)mmo_get_u8(&r);
                for (k = 0; k < nids; k++)
                    (void)mmo_get_s64le(&r);
            }
        }
    } else if (kind >= 3 && kind <= 5) {
        out->prompt_type = (s8)mmo_get_u8(&r);
        out->value = mmo_get_s16le(&r);
    } else if (kind >= 1 && kind <= 2) {
        out->prompt_type = (s8)mmo_get_u8(&r);
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_menu_prompt_close(const u8 *body, size_t n, s8 *kind)
{
    mmo_rbuf r;
    s8 value;

    mmo_rbuf_init(&r, body, n);
    value = (s8)mmo_get_u8(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (kind)
        *kind = value;
    return 0;
}

int mmo_game_read_view_scale(const u8 *body, size_t n, s8 *scale)
{
    mmo_rbuf r;
    s8 value;

    mmo_rbuf_init(&r, body, n);
    value = (s8)mmo_get_u8(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    if (scale)
        *scale = value;
    return 0;
}

int mmo_game_read_digest(const u8 *body, size_t n, mmo_digest *out)
{
    mmo_rbuf r;
    mmo_digest sink;
    u16 bits, len;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    bits = mmo_get_u16le(&r);
    len = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    if (mmo_rbuf_remaining(&r) < (size_t)len)
        return -1;
    out->bit_count = bits;
    out->len = len;
    out->bytes = len ? body + r.pos : NULL;
    r.pos += len;
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_transfer_begin(const u8 *body, size_t n,
                                 mmo_transfer_begin *out)
{
    mmo_rbuf r;
    mmo_transfer_begin sink;
    u16 slen;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->id = mmo_get_s64le(&r);
    out->size = mmo_get_s32le(&r);
    slen = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    if (mmo_rbuf_remaining(&r) < (size_t)slen)
        return -1;
    out->sig_len = slen;
    out->sig = slen ? body + r.pos : NULL;
    r.pos += slen;
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_transfer_append(const u8 *body, size_t n,
                                  mmo_transfer_append *out)
{
    mmo_rbuf r;
    mmo_transfer_append sink;
    u16 len;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    len = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    if (mmo_rbuf_remaining(&r) < (size_t)len)
        return -1;
    out->len = len;
    out->data = len ? body + r.pos : NULL;
    r.pos += len;
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_stream_chunk(const u8 *body, size_t n,
                               mmo_stream_chunk *out)
{
    mmo_rbuf r;
    mmo_stream_chunk sink;
    u16 len;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->id = mmo_get_s64le(&r);
    out->last = (mmo_get_u8(&r) == 1);
    len = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    if (mmo_rbuf_remaining(&r) < (size_t)len)
        return -1;
    out->len = len;
    out->data = len ? body + r.pos : NULL;
    r.pos += len;
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_image_chunk(const u8 *body, size_t n,
                              mmo_image_chunk *out)
{
    mmo_rbuf r;
    mmo_image_chunk sink;
    u16 len;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->ctrl = (s8)mmo_get_u8(&r);
    out->present = (out->ctrl != 2);
    if (!out->present) {
        if (r.err || mmo_rbuf_remaining(&r) != 0)
            return -1;
        return 0;
    }
    out->image_type = (s8)mmo_get_u8(&r);
    out->chunk_index = mmo_get_u8(&r);
    out->last = (mmo_get_u8(&r) == 1);
    len = mmo_get_u16le(&r);
    if (r.err)
        return -1;
    if (mmo_rbuf_remaining(&r) < (size_t)len)
        return -1;
    out->len = len;
    out->data = len ? body + r.pos : NULL;
    r.pos += len;
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

void mmo_game_sync_xor(u8 *p, size_t n)
{
    static const u8 key[8] = { 81, 147, 63, 224, 82, 99, 116, 206 };
    size_t i;

    if (!p)
        return;
    for (i = 0; i < n; i++)
        p[i] ^= key[i & 7];
}

int mmo_game_transfer_open(const u8 *wired, size_t n, u8 *out, size_t cap,
                           size_t *out_n)
{
    u8 *tmp;
    size_t got;

    if (out_n)
        *out_n = 0;
    if (!wired || !out)
        return -1;
    tmp = (u8 *)malloc(n ? n : 1);
    if (!tmp)
        return -1;
    if (n)
        memcpy(tmp, wired, n);
    mmo_game_sync_xor(tmp, n);
    got = mmo_inflate_gzip(tmp, n, out, cap);
    free(tmp);
    if (got == (size_t)-1)
        return -1;
    if (out_n)
        *out_n = got;
    return 0;
}

void mmo_game_write_digest_empty(mmo_wbuf *w)
{
    mmo_put_u8(w, 0);
}

int mmo_game_read_rentals(const u8 *body, size_t n, mmo_rentals *out)
{
    mmo_rbuf r;
    mmo_rentals sink;
    int i, count;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->mode = (s8)mmo_get_u8(&r);
    for (i = 0; i < 3; i++)
        out->param[i] = (s8)mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (out->mode == 2) {
        count = (int)mmo_get_u8(&r);
        if (r.err || count > MMO_RENTAL_MAX)
            return -1;
        out->preview_count = count;
        for (i = 0; i < count; i++) {
            out->preview[i].species = mmo_get_s16le(&r);
            out->preview[i].level = mmo_get_s16le(&r);
        }
    } else if (out->mode == 0) {
        count = (int)mmo_get_u8(&r);
        if (r.err || count > MMO_RENTAL_MAX)
            return -1;
        out->monster_count = count;
        for (i = 0; i < count; i++)
            if (read_monster(&r, &out->monster[i]) != 0)
                return -1;
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

static void read_tourney(mmo_rbuf *r, mmo_tourney *out)
{
    out->id = mmo_get_s64le(r);
    out->kind = (s8)mmo_get_u8(r);
    out->type = (s8)mmo_get_u8(r);
    (void)mmo_get_utf16_nt(r, out->name, sizeof out->name);
    out->capacity = mmo_get_s16le(r);
    out->format = mmo_get_s16le(r);
    (void)mmo_get_u8(r);
    out->start_time = mmo_get_s64le(r);
    out->mode = (s8)mmo_get_u8(r);
    out->s32_a = mmo_get_s32le(r);
    out->u8_a = (s8)mmo_get_u8(r);
    out->flag_a = (mmo_get_u8(r) == 1);
    out->flag_b = (mmo_get_u8(r) == 1);
    (void)mmo_get_u8(r);
}

int mmo_game_read_tourney_page(const u8 *body, size_t n, mmo_tourney_page *out)
{
    mmo_rbuf r;
    mmo_tourney_page sink;
    int i, count;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->tab = (s8)mmo_get_u8(&r);
    out->second_list = (mmo_get_u8(&r) == 1);
    out->page = mmo_get_s16le(&r);
    out->total = mmo_get_s32le(&r);
    count = (int)mmo_get_u8(&r);
    if (r.err || count > MMO_TOURNEY_MAX)
        return -1;
    out->count = count;
    for (i = 0; i < count; i++)
        read_tourney(&r, &out->entry[i]);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_tourney_count(const u8 *body, size_t n,
                                mmo_tourney_count *out)
{
    mmo_rbuf r;
    mmo_tourney_count sink;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->tourney_id = mmo_get_s64le(&r);
    out->entered = mmo_get_s16le(&r);
    out->checked_in = mmo_get_s16le(&r);
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_matchups(const u8 *body, size_t n, mmo_matchups *out)
{
    mmo_rbuf r;
    mmo_matchups sink;
    int i, count;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    count = (int)mmo_get_u16le(&r);
    if (r.err || count > MMO_MATCHUP_MAX)
        return -1;
    out->count = count;
    for (i = 0; i < count; i++) {
        mmo_matchup *m = &out->entry[i];

        m->winner = mmo_get_s16le(&r);
        m->type = (s8)mmo_get_u8(&r);
        m->id = mmo_get_s64le(&r);
        m->entrant[0] = -1;
        m->entrant[1] = -1;
        if (m->type != 0) {
            m->has_entrants = 1;
            m->entrant[0] = mmo_get_s16le(&r);
            m->entrant[1] = mmo_get_s16le(&r);
        }
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

static void read_board_mon(mmo_rbuf *r, mmo_board_mon *out)
{
    int i;
    u8 packed;

    (void)mmo_get_utf16_nt(r, out->name, sizeof out->name);
    out->u8_a = (s8)mmo_get_u8(r);
    out->s32_a = mmo_get_s32le(r);
    out->u8_b = (s8)mmo_get_u8(r);
    packed = mmo_get_u8(r);
    for (i = 0; i < MMO_BOARD_MON_SLOTS; i++) {
        u16 w = mmo_get_u16le(r);
        s16 value = (s16)(w & 0x3FF);
        s8 extra = (s8)((w >> 10) & 0x3F);

        out->value[i] = (value == 1023) ? -1 : value;
        out->extra[i] = (extra == 63) ? -1 : extra;
        out->packed[i] = (s8)((packed >> (i * 2)) & 3);
    }
}

int mmo_game_read_score_board(const u8 *body, size_t n, mmo_score_board *out)
{
    mmo_rbuf r;
    mmo_score_board sink;
    int i, j, total;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->category = (s8)mmo_get_u8(&r);
    (void)mmo_get_s32le(&r);
    total = mmo_get_s32le(&r);
    if (r.err || total < 0 || total > MMO_BOARD_ROW_MAX)
        return -1;
    out->total = total;
    out->count = total;
    for (i = 0; i < total; i++) {
        mmo_board_row *row = &out->row[i];
        int mons;

        row->entity_id = mmo_get_s64le(&r);
        row->rank = mmo_get_s32le(&r);
        row->score = mmo_get_s32le(&r);
        mons = (int)mmo_get_u8(&r);
        if (r.err || mons > MMO_BOARD_MON_MAX)
            return -1;
        row->monster_count = mons;
        for (j = 0; j < mons; j++)
            read_board_mon(&r, &row->monster[j]);
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

void mmo_game_write_empty_request(mmo_wbuf *w)
{
    (void)w;
}

void mmo_game_write_queue_action_tier(mmo_wbuf *w, s32 tier_id)
{
    mmo_put_u8(w, MMO_QUEUE_ACTION_TIER);
    mmo_put_s32le(w, tier_id);
}

void mmo_game_write_queue_action_target(mmo_wbuf *w, s64 target, s16 slot)
{
    mmo_put_u8(w, MMO_QUEUE_ACTION_TARGET);
    mmo_put_s64le(w, target);
    mmo_put_s16le(w, slot);
}

void mmo_game_write_queue_action_teleport(mmo_wbuf *w)
{
    mmo_put_u8(w, MMO_QUEUE_ACTION_TELEPORT);
}

void mmo_game_write_queue_langs(mmo_wbuf *w, const s8 *langs, int count)
{
    int i;

    if (count < 0)
        count = 0;
    if (count > 255)
        count = 255;
    mmo_put_u8(w, (u8)count);
    for (i = 0; i < count; i++)
        mmo_put_u8(w, (u8)langs[i]);
}

void mmo_game_write_tier_select(mmo_wbuf *w, s8 slot, s8 tier)
{
    mmo_put_u8(w, (u8)slot);
    mmo_put_u8(w, (u8)tier);
}

/*
 * A count of zero cannot be written. The leading byte is the discriminator as well as the
 * count, and a 0 there means "a tournament id follows", so an empty list and a tournament
 * signup are the same first byte, and a reader has no way to tell them apart.
 */
int mmo_game_write_queue_signup(mmo_wbuf *w, const s8 *queues,
                                const s8 *slots, int count)
{
    int i;

    if (count < 1 || count > 255 || !queues)
        return -1;
    mmo_put_u8(w, (u8)count);
    for (i = 0; i < count; i++) {
        mmo_put_u8(w, (u8)queues[i]);
        mmo_put_u8(w, slots ? (u8)slots[i] : 0);
    }
    return 0;
}

void mmo_game_write_tourney_signup(mmo_wbuf *w, s64 tourney_id, s8 slot)
{
    mmo_put_u8(w, 0);
    mmo_put_s64le(w, tourney_id);
    mmo_put_u8(w, (u8)slot);
}

void mmo_game_write_tourney_view(mmo_wbuf *w, s8 tourney_id, int active,
                                 s16 tab)
{
    mmo_put_u8(w, (u8)tourney_id);
    mmo_put_u8(w, active ? 1 : 0);
    mmo_put_s16le(w, tab);
}

void mmo_game_write_tourney_register(mmo_wbuf *w, int register_)
{
    mmo_put_u8(w, register_ ? 1 : 0);
}

void mmo_game_write_score_board_req(mmo_wbuf *w, s8 category)
{
    mmo_put_u8(w, (u8)category);
}

/* Walk the ZZ session record the official client's tK0.kq builds. Called with false from
 * the lookup, so the long is on the wire. The reads the official client discards are
 * walked and dropped here too: they are part of the length. */
static void read_gm_session(mmo_rbuf *r, mmo_gm_session *out)
{
    int i, count;

    out->entity_id = mmo_get_s64le(r);
    mmo_get_utf16_nt(r, out->name, sizeof out->name);
    mmo_get_utf16_nt(r, out->secondary, sizeof out->secondary);
    out->i32[0] = mmo_get_s32le(r);
    out->i8[0] = (s8)mmo_get_u8(r);
    out->i32[1] = mmo_get_s32le(r);
    out->s64_a = mmo_get_s64le(r);
    out->i32[2] = mmo_get_s32le(r);
    (void)mmo_get_s32le(r);
    (void)mmo_get_u8(r);
    out->i32[3] = mmo_get_s32le(r);
    out->i32[4] = mmo_get_s32le(r);
    out->i16[0] = mmo_get_s16le(r);
    out->i32[5] = mmo_get_s32le(r);
    out->i8[1] = (s8)mmo_get_u8(r);
    out->i8[2] = (s8)mmo_get_u8(r);
    (void)mmo_get_u8(r);
    out->i8[3] = (s8)mmo_get_u8(r);
    (void)mmo_get_s32le(r);
    for (i = 0; i < 8; i++)
        (void)mmo_get_u8(r);
    out->i16[1] = mmo_get_s16le(r);
    out->i8[4] = (s8)mmo_get_u8(r);
    out->i32[6] = mmo_get_s32le(r);
    out->i8[5] = (s8)mmo_get_u8(r);
    out->i8[6] = (s8)mmo_get_u8(r);
    out->i8[7] = (s8)mmo_get_u8(r);
    for (i = 0; i < 3; i++)
        (void)mmo_get_u8(r);
    out->i8[8] = (s8)mmo_get_u8(r);
    out->i8[9] = (s8)mmo_get_u8(r);
    out->i8[10] = (s8)mmo_get_u8(r);
    (void)mmo_get_u8(r);
    out->i16[2] = mmo_get_s16le(r);
    out->i16[3] = mmo_get_s16le(r);
    (void)mmo_get_u8(r);
    (void)mmo_get_u8(r);
    out->i16[4] = mmo_get_s16le(r);
    out->i16[5] = mmo_get_s16le(r);
    out->status = (s8)mmo_get_u8(r);
    out->i16[6] = mmo_get_s16le(r);
    out->i16[7] = mmo_get_s16le(r);
    count = (int)mmo_get_u8(r);
    if (r->err || count > MMO_GM_STATUS_MAX) {
        r->err = 1;
        return;
    }
    out->status_count = count;
    for (i = 0; i < count; i++)
        out->status_list[i] = (s8)mmo_get_u8(r);
}

int mmo_game_read_gm_lookup(const u8 *body, size_t n, mmo_gm_lookup *out)
{
    mmo_rbuf r;
    mmo_gm_lookup sink;
    int i, count;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->found = mmo_get_u8(&r) == 1;
    if (r.err)
        return -1;
    if (!out->found)
        return mmo_rbuf_remaining(&r) == 0 ? 0 : -1;

    read_gm_session(&r, &out->session);
    out->rank = (s8)mmo_get_u8(&r);
    if (r.err)
        return -1;
    if (out->rank > 0) {
        out->has_account = 1;
        for (i = 0; i < 3; i++)
            out->rank_extra[i] = (s8)mmo_get_u8(&r);
        mmo_get_utf16_nt(&r, out->address, sizeof out->address);
        out->playtime = mmo_get_s64le(&r);
        out->account_id = mmo_get_s32le(&r);
        mmo_get_utf16_nt(&r, out->account_a, sizeof out->account_a);
        mmo_get_utf16_nt(&r, out->account_b, sizeof out->account_b);
        mmo_get_utf16_nt(&r, out->account_c, sizeof out->account_c);
    }
    if (mmo_get_u8(&r) == 1) {
        out->has_detail = 1;
        out->detail_value = mmo_get_s32le(&r);
        mmo_get_utf16_nt(&r, out->detail_text, sizeof out->detail_text);
    }
    count = (int)mmo_get_u16le(&r);
    if (r.err || count > MMO_GM_CHAR_MAX)
        return -1;
    out->count = count;
    for (i = 0; i < count; i++) {
        mmo_gm_character *e = &out->entry[i];

        e->entity_id = mmo_get_s64le(&r);
        e->value = mmo_get_s32le(&r);
        mmo_get_utf16_nt(&r, e->name_a, sizeof e->name_a);
        mmo_get_utf16_nt(&r, e->name_b, sizeof e->name_b);
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

/* The tail cases 1 and 101 share: the official client's 101 reads a string and then falls
 * straight into 1 rather than jumping past it. */
static void read_gm_panel_note(mmo_rbuf *r, mmo_gm_panel *out)
{
    out->entity_id = mmo_get_s64le(r);
    mmo_get_utf16_nt(r, out->text_b, sizeof out->text_b);
    mmo_get_utf16_nt(r, out->text_c, sizeof out->text_c);
    out->value = mmo_get_s32le(r);
}

int mmo_game_read_gm_panel(const u8 *body, size_t n, mmo_gm_panel *out)
{
    mmo_rbuf r;
    mmo_gm_panel sink;
    int i, j, count;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->variant = (s8)mmo_get_u8(&r);
    if (r.err)
        return -1;
    out->known = 1;
    switch ((u8)out->variant) {
    case MMO_GM_PANEL_NOTE:
        read_gm_panel_note(&r, out);
        break;
    case MMO_GM_PANEL_TARGET:
        out->entity_id = mmo_get_s64le(&r);
        break;
    case MMO_GM_PANEL_TITLED_NOTE:
        mmo_get_utf16_nt(&r, out->text_a, sizeof out->text_a);
        read_gm_panel_note(&r, out);
        break;
    case MMO_GM_PANEL_DETAIL:
    case MMO_GM_PANEL_SUMMARY:
        if ((u8)out->variant == MMO_GM_PANEL_DETAIL)
            out->s8_a = (s8)mmo_get_u8(&r);
        out->s16_a = mmo_get_s16le(&r);
        out->s16_b = mmo_get_s16le(&r);
        out->s16_c = mmo_get_s16le(&r);
        out->s8_b = (s8)mmo_get_u8(&r);
        (void)mmo_get_u8(&r);
        out->s32_a = mmo_get_s32le(&r);
        if ((u8)out->variant == MMO_GM_PANEL_DETAIL) {
            out->flag_a = mmo_get_u8(&r) == 1;
            out->flag_b = mmo_get_u8(&r) == 1;
        }
        out->s8_c = (s8)mmo_get_u8(&r);
        out->s8_d = (s8)mmo_get_u8(&r);
        out->s8_e = (s8)mmo_get_u8(&r);
        if ((u8)out->variant == MMO_GM_PANEL_DETAIL) {
            count = (int)mmo_get_u8(&r);
            if (r.err || count > MMO_GM_PAIR_MAX)
                return -1;
            out->pair_count = count;
            for (i = 0; i < count; i++) {
                out->pair_a[i] = (s8)mmo_get_u8(&r);
                out->pair_b[i] = (s8)mmo_get_u8(&r);
            }
        }
        break;
    case MMO_GM_PANEL_MENU:
        count = (int)mmo_get_u8(&r);
        if (r.err || count > MMO_GM_PANEL_ROW_MAX)
            return -1;
        out->row_count = count;
        for (i = 0; i < count; i++) {
            mmo_gm_panel_row *row = &out->row[i];
            int opts;

            mmo_get_utf16_nt(&r, row->label, sizeof row->label);
            opts = (int)mmo_get_u8(&r);
            if (r.err || opts > MMO_GM_PANEL_OPT_MAX)
                return -1;
            row->option_count = opts;
            for (j = 0; j < opts; j++) {
                mmo_get_utf16_nt(&r, row->option[j], sizeof row->option[j]);
                row->option_kind[j] = (s8)mmo_get_u8(&r);
            }
        }
        break;
    default:
        /* 0, 31, 100 and every byte the enum does not name: no body. */
        out->known = 0;
        break;
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

int mmo_game_read_gm_panel_entry(const u8 *body, size_t n,
                                 mmo_gm_panel_entry *out)
{
    mmo_rbuf r;
    mmo_gm_panel_entry sink;

    mmo_rbuf_init(&r, body, n);
    if (!out)
        out = &sink;
    memset(out, 0, sizeof *out);

    out->clear = (s8)mmo_get_u8(&r);
    if (out->clear == 0) {
        mmo_get_utf16_nt(&r, out->label, sizeof out->label);
        mmo_get_utf16_nt(&r, out->value, sizeof out->value);
        out->count = mmo_get_s32le(&r);
    }
    if (r.err || mmo_rbuf_remaining(&r) != 0)
        return -1;
    return 0;
}

void mmo_game_write_admin_note_add(mmo_wbuf *w, s64 target, const char *text)
{
    mmo_put_u8(w, MMO_GM_NOTE_ADD);
    mmo_put_s64le(w, target);
    mmo_put_utf16_nt(w, text ? text : "");
}

void mmo_game_write_admin_note_delete(mmo_wbuf *w, s64 target, s64 note_id)
{
    mmo_put_u8(w, MMO_GM_NOTE_DELETE);
    mmo_put_s64le(w, target);
    mmo_put_s64le(w, note_id);
}

void mmo_game_write_moderation_confirm(mmo_wbuf *w, s64 target)
{
    mmo_put_s64le(w, target);
}
