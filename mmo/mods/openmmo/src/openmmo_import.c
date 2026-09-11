/* Reading an offline save, and offering it to the server. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/species.h"

#include "system.h"  /* gSystem: the pad, held at zero while the game leaves */

#include "bag.h"
#include "field_overworld_state.h"
#include "location.h"
#include "party.h"
#include "pc_boxes.h"
#include "play_time.h"
#include "pokedex.h"
#include "pokemon.h"
#include "save_player.h"
#include "savedata/save_table.h"
#include "savedata.h"
#include "trainer_info.h"
#include "vars_flags.h"

#include "../../../include/charcode.h"
#include "../../../include/client.h"
#include "../../../include/crypto.h"
#include "../../../include/idmap.h"
#include "../../../include/offline_chain.h"
#include "../../../include/offline_import.h"
#include "../../../include/platform.h"
#include "../../../include/species_port.h"

/* pc/src/pc_card_rom.c: put the backup image on disk now. Not in a header,
 * the port declares it in its own translation unit and openmmo_offline.c
 * reaches it the same way. */
extern void pc_card_backup_sync(void);

/* pc/src/pc_video.c's VBlank counter, reached the same way. This is the frame
 * a replay of the session's recording has to stop on: the port's own recorder
 * writes no trailer, so without it nobody downstream can tell a session that
 * ended after its last button from one that ran on for another hour. */
extern unsigned long long pc_irq_frames(void);

/* Whether this run has a server behind it. */
extern int openmmo_session_configured(void);

/* Bind the species overlay's live count now. */
extern void openmmo_species_bind(void);

/* Every block this client keeps, whole. */
extern int openmmo_save_blocks_all(SaveData *save, mmo_save_block *out, int cap);

/* Below, after the answers it prints; named here because the leave that
 * follows a landed save prints one first. An implicit declaration is a
 * warning the desktop's -w hides and an error the NDK's clang does not. */
void openmmo_import_print_answer(const openmmo_import_answer *a);

/* The eight badges a Sinnoh save can hold. The server reads the bitfield and
 * writes one story flag per bit; a ninth would be a badge this cartridge has
 * no BADGE_ID_ for. */
#define IMPORT_BADGES 8

/* How much of one report stands on the heap at a time. The party and the boxes
 * together are six plus eighteen boxes of thirty. */
#define IMPORT_MON_MAX (MAX_PARTY_SIZE + MAX_PC_BOXES * MAX_MONS_PER_BOX)

/*
 * The engine's IV and EV params run HP, ATK, DEF, SPEED, SPATK, SPDEF; the report's six run in
 * the server's own order, HP, ATK, DEF, SPATK, SPDEF, SPEED.
 */
static const int kStatOrder[MMO_IMPORT_STATS] = { 0, 1, 2, 4, 5, 3 };

/* A charcode name into UTF-8, always terminated. An unmappable name comes out
 * as far as it got rather than empty: the server stores what it is given and a
 * truncated nickname is better than a nameless monster. */
static void name_utf8(const u16 *name, char *dst, size_t cap)
{
    mmo_charcode buf[32];
    size_t i;

    dst[0] = '\0';
    if (name == NULL)
        return;
    for (i = 0; i + 1 < sizeof buf / sizeof buf[0]; i++) {
        buf[i] = (mmo_charcode)name[i];
        if (name[i] == MMO_CHAR_EOS)
            break;
    }
    buf[i] = MMO_CHAR_EOS;
    mmo_charcode_to_utf8(buf, dst, cap);
}

/*
 * One monster out of a BoxPokemon, which is what both a party member and a stored one are
 * underneath. Returns 0 when the slot holds nothing.
 */
static int read_mon(BoxPokemon *bm, Pokemon *mon, int box, int slot,
                    mmo_import_mon *out)
{
    u16 name[32];
    u32 species;
    int i;

    if (bm == NULL || !BoxPokemon_GetValue(bm, MON_DATA_SPECIES_EXISTS, NULL))
        return 0;
    species = BoxPokemon_GetValue(bm, MON_DATA_SPECIES, NULL);
    if (species == 0)
        return 0;
    /* Never hand a species this build has no tables for to the getters below:
     * MON_DATA_LEVEL alone reads pl_personal past its end and corrupts the
     * heap. A caller reaching this has skipped
     * openmmo_offline_save_unsupported, which is the door for this. */
    if (!mmo_species_port_live((int)species)) {
        printf("openmmo: species %d is not in this build; no report can name it\n",
               (int)species);
        return 0;
    }

    memset(out, 0, sizeof *out);
    out->pid = (s32)BoxPokemon_GetValue(bm, MON_DATA_PERSONALITY, NULL);
    /* The wire's species id, not the cartridge's: the two agree for the first
     * 493 and part company after them. A species with no wire id crosses as
     * the engine's own number and the server's door drops it by name, which is
     * the honest answer, better than a silent 0. */
    {
        int wire = mmo_species_port_wire_id((int)species);

        out->dex = (u16)(wire > 0 ? wire : (int)species);
    }
    out->form = (u8)BoxPokemon_GetValue(bm, MON_DATA_FORM, NULL);
    out->level = (u8)BoxPokemon_GetValue(bm, MON_DATA_LEVEL, NULL);
    out->xp = (s32)BoxPokemon_GetValue(bm, MON_DATA_EXPERIENCE, NULL);
    for (i = 0; i < MMO_IMPORT_STATS; i++) {
        out->ivs[i] = (u8)BoxPokemon_GetValue(
            bm, MON_DATA_HP_IV + kStatOrder[i], NULL);
        out->evs[i] = (u8)BoxPokemon_GetValue(
            bm, MON_DATA_HP_EV + kStatOrder[i], NULL);
    }
    out->nmoves = 0;
    for (i = 0; i < 4; i++) {
        u32 move = BoxPokemon_GetValue(bm, MON_DATA_MOVE1 + i, NULL);
        const char *why = NULL;
        u16 wire;

        if (move == 0)
            continue;
        wire = mmo_id_move_to_server((u16)move, &why);
        out->moves[out->nmoves].move = wire != 0 ? wire : (u16)move;
        out->moves[out->nmoves].pp =
            (u8)BoxPokemon_GetValue(bm, MON_DATA_MOVE1_PP + i, NULL);
        out->moves[out->nmoves].pp_ups =
            (u8)BoxPokemon_GetValue(bm, MON_DATA_MOVE1_PP_UPS + i, NULL);
        out->nmoves++;
    }

    BoxPokemon_GetValue(bm, MON_DATA_NICKNAME, name);
    name_utf8(name, out->nickname, sizeof out->nickname);
    BoxPokemon_GetValue(bm, MON_DATA_OT_NAME, name);
    name_utf8(name, out->ot_name, sizeof out->ot_name);

    out->ot_id = (s32)BoxPokemon_GetValue(bm, MON_DATA_OT_ID, NULL);
    out->ability = (u16)BoxPokemon_GetValue(bm, MON_DATA_ABILITY, NULL);
    /* This cartridge has no hidden abilities at all: the field the record
     * keeps for one is always false out of a Gen 4 save, and saying so here is
     * what stops a later reader inventing a rule for it. */
    out->hidden_ability = 0;
    /* On the unsigned value, the way the engine and the server both take it:
     * the field is signed on the wire, and a negative remainder here had the
     * door "bringing" half of all natures to the number they already were. */
    out->nature = (u8)((u32)out->pid % 25u);
    out->shiny = Pokemon_IsPersonalityShiny((u32)out->ot_id, (u32)out->pid) ? 1 : 0;
    {
        const char *why = NULL;
        u32 held = BoxPokemon_GetValue(bm, MON_DATA_HELD_ITEM, NULL);

        out->held_item = held != 0
                             ? mmo_id_item_to_server((u16)held, &why)
                             : 0;
    }
    out->egg = BoxPokemon_GetValue(bm, MON_DATA_IS_EGG, NULL) ? 1 : 0;
    out->friendship = (u8)BoxPokemon_GetValue(bm, MON_DATA_FRIENDSHIP, NULL);
    /* An egg keeps its remaining cycles in the friendship byte, which is why
     * the two are read from one place and separated here rather than being two
     * getters that would both be the same one. */
    out->egg_cycles = out->egg ? out->friendship : 0;
    if (out->egg)
        out->friendship = 0;
    out->box = (u8)box;
    out->slot = (u16)slot;
    /* What a poffin and a contest left behind. The engine keeps the ribbons
     * as one mask (`ribbonsDS2`) whose bit n is MON_DATA_SUPER_COOL_RIBBON + n,
     * which is the layout both the live wire and the server's own ribbon bit
     * already use, so the mask crosses unchanged. */
    for (i = 0; i < MMO_IMPORT_CONDITIONS; i++)
        out->cond[i] = (u8)BoxPokemon_GetValue(bm, MON_DATA_COOL + i, NULL);
    out->sheen = (u8)BoxPokemon_GetValue(bm, MON_DATA_SHEEN, NULL);
    out->ribbons_super = 0;
    for (i = 0; i < MMO_IMPORT_CONDITIONS * MMO_IMPORT_RIBBON_RANKS; i++) {
        if (BoxPokemon_GetValue(bm, MON_DATA_SUPER_COOL_RIBBON + i, NULL))
            out->ribbons_super |= (u64)1 << i;
    }
    /* The place, which the file holds as a location label rather than as a
     * map: what a caught monster is stamped with is the label of the map the
     * battle was on. Carried as the number it is. */
    out->met_location =
        (u16)BoxPokemon_GetValue(bm, MON_DATA_MET_LOCATION_PTHGSS, NULL);
    {
        const char *why = NULL;
        u32 ball = BoxPokemon_GetValue(bm, MON_DATA_POKEBALL, NULL);

        out->ball = ball != 0 ? mmo_id_item_to_server((u16)ball, &why) : 0;
    }
    out->pokerus = (u8)BoxPokemon_GetValue(bm, MON_DATA_POKERUS, NULL);
    out->markings = (u8)BoxPokemon_GetValue(bm, MON_DATA_MARKINGS, NULL);
    /*
     * What it is suffering from, read through the party record because that is the only place
     * a save keeps one.
     */
    out->status = (mon != NULL)
                      ? (u16)(Pokemon_GetValue(mon, MON_DATA_STATUS, NULL)
                              & MMO_MON_STATUS_MASK)
                      : 0;
    return 1;
}

/* --- a save this build cannot draw --------------------------------------- */

/* Which package a species past this cartridge's own would have arrived in. */
static const char *cartridge_for_species(int species)
{
    (void)species;
    return "BLACK";
}

/* The species in one slot this build has no tables for, or 0. */
static int slot_unsupported(BoxPokemon *bm)
{
    u32 species;

    if (bm == NULL || !BoxPokemon_GetValue(bm, MON_DATA_SPECIES_EXISTS, NULL))
        return 0;
    species = BoxPokemon_GetValue(bm, MON_DATA_SPECIES, NULL);
    if (species == 0 || mmo_species_port_live((int)species))
        return 0;
    return (int)species;
}

/*
 * The first monster in this save this build cannot answer for, party before boxes; 0 when
 * every one of them can be drawn. `why` takes a sentence naming the species and the cartridge
 * it is read out of, short enough for the lobby's hint.
 */
int openmmo_offline_save_unsupported(SaveData *save, char *why, size_t cap)
{
    Party *party;
    PCBoxes *boxes;
    int bad = 0;
    int i, count;
    u32 b, s;

    if (why != NULL && cap > 0)
        why[0] = '\0';
    if (save == NULL)
        return 0;
    /* This is the earliest anything asks what the packages carry, and the
     * count is bound on first use, unbound, every ported species reads as
     * absent and a filled install would refuse its own save. */
    openmmo_species_bind();

    party = SaveData_GetParty(save);
    if (party != NULL) {
        count = Party_GetCurrentCount(party);
        for (i = 0; i < count && bad == 0; i++) {
            Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

            if (mon != NULL)
                bad = slot_unsupported(Pokemon_GetBoxPokemon(mon));
        }
    }
    boxes = SaveData_GetPCBoxes(save);
    if (bad == 0 && boxes != NULL) {
        for (b = 0; b < MAX_PC_BOXES && bad == 0; b++) {
            for (s = 0; s < MAX_MONS_PER_BOX && bad == 0; s++)
                bad = slot_unsupported(PCBoxes_GetBoxMonAt(boxes, b, s));
        }
    }
    if (bad != 0 && why != NULL && cap > 0)
        snprintf(why, cap, "SPECIES %d NEEDS YOUR %s CARTRIDGE",
                 bad, cartridge_for_species(bad));
    return bad;
}

static int read_party(SaveData *save, mmo_import_mon *out, int cap)
{
    Party *party = SaveData_GetParty(save);
    int n = 0;
    int i;
    int count;

    if (party == NULL)
        return 0;
    count = Party_GetCurrentCount(party);
    for (i = 0; i < count && n < cap; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

        if (mon == NULL)
            continue;
        if (read_mon(Pokemon_GetBoxPokemon(mon), mon, 0, i, &out[n]))
            n++;
    }
    return n;
}

static int read_boxes(SaveData *save, mmo_import_mon *out, int cap)
{
    PCBoxes *boxes = SaveData_GetPCBoxes(save);
    int n = 0;
    u32 b, s;

    if (boxes == NULL)
        return 0;
    for (b = 0; b < MAX_PC_BOXES && n < cap; b++) {
        for (s = 0; s < MAX_MONS_PER_BOX && n < cap; s++) {
            BoxPokemon *bm = PCBoxes_GetBoxMonAt(boxes, b, s);

            /* The boxes are numbered straight through, so box 2 slot 0 is slot
             * 30: the record has one storage container and this is the index
             * into it. */
            if (read_mon(bm, NULL, 1, (int)(b * MAX_MONS_PER_BOX + s), &out[n]))
                n++;
        }
    }
    return n;
}

/* One pocket of the bag. Item ids cross as the server's; an item with no wire
 * id of its own is left out rather than sent as a number that means something
 * else on the far side. */
static int read_pocket(const BagItem *pocket, int size,
                       mmo_import_item *out, int at, int cap)
{
    int i;

    for (i = 0; i < size && at < cap; i++) {
        const char *why = NULL;
        u16 wire;

        if (pocket[i].item == 0 || pocket[i].quantity == 0)
            continue;
        wire = mmo_id_item_to_server(pocket[i].item, &why);
        if (wire == 0)
            continue;
        out[at].item = wire;
        out[at].quantity = pocket[i].quantity;
        at++;
    }
    return at;
}

static int read_bag(SaveData *save, mmo_import_item *out, int cap)
{
    Bag *bag = SaveData_GetBag(save);
    int n = 0;

    if (bag == NULL)
        return 0;
    n = read_pocket(bag->items, ITEM_POCKET_SIZE, out, n, cap);
    n = read_pocket(bag->keyItems, KEY_ITEM_POCKET_SIZE, out, n, cap);
    n = read_pocket(bag->tmHms, TMHM_POCKET_SIZE, out, n, cap);
    n = read_pocket(bag->mail, MAIL_POCKET_SIZE, out, n, cap);
    n = read_pocket(bag->medicine, MEDICINE_POCKET_SIZE, out, n, cap);
    n = read_pocket(bag->berries, BERRY_POCKET_SIZE, out, n, cap);
    n = read_pocket(bag->pokeballs, POKEBALL_POCKET_SIZE, out, n, cap);
    n = read_pocket(bag->battleItems, BATTLE_ITEM_POCKET_SIZE, out, n, cap);
    return n;
}

/* The hex sha256 of a file, or an empty string when it cannot be read. The
 * hash is how the server recognises the same save twice; it decides nothing,
 * so a file that cannot be read still makes a report. */
static void file_sha256(const char *path, char out[65])
{
    static const char HEX[] = "0123456789abcdef";
    mmo_sha256_ctx ctx;
    u8 digest[MMO_SHA256_DIGEST];
    u8 chunk[4096];
    FILE *f;
    size_t got;
    int i;

    out[0] = '\0';
    f = fopen(path, "rb");
    if (f == NULL)
        return;
    mmo_sha256_init(&ctx);
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0)
        mmo_sha256_update(&ctx, chunk, got);
    fclose(f);
    mmo_sha256_final(&ctx, digest);
    for (i = 0; i < MMO_SHA256_DIGEST; i++) {
        out[i * 2] = HEX[digest[i] >> 4];
        out[i * 2 + 1] = HEX[digest[i] & 0xF];
    }
    out[64] = '\0';
}

/* Read the whole of `save` into a report and write it beside `save_path`.
 * 0 on success. */
static int write_report(SaveData *save, const char *save_path)
{
    mmo_import_report r;
    mmo_import_mon *mons = NULL;
    mmo_import_item *bag = NULL;
    u16 *seen = NULL;
    u16 *caught = NULL;
    u16 *flags = NULL;
    mmo_import_var *vars = NULL;
    mmo_save_block blocks[MMO_SAVE_BLOCK_MAX];
    char path[1024];
    int rc = -1;
    int i;

    memset(&r, 0, sizeof r);
    mons = calloc(IMPORT_MON_MAX, sizeof *mons);
    bag = calloc(MMO_IMPORT_MAX_BAG, sizeof *bag);
    seen = calloc(NATIONAL_DEX_COUNT + 1u, sizeof *seen);
    caught = calloc(NATIONAL_DEX_COUNT + 1u, sizeof *caught);
    flags = calloc(NUM_FLAGS, sizeof *flags);
    vars = calloc(NUM_VARS, sizeof *vars);
    if (mons == NULL || bag == NULL || seen == NULL || caught == NULL
        || flags == NULL || vars == NULL) {
        fprintf(stderr, "openmmo: not enough memory to read this save\n");
        goto out;
    }

    {
        TrainerInfo *info = SaveData_GetTrainerInfo(save);

        if (info != NULL) {
            r.trainer_id = (s32)TrainerInfo_ID(info);
            r.money = (s32)TrainerInfo_Money(info);
            for (i = 0; i < IMPORT_BADGES; i++) {
                if (TrainerInfo_HasBadge(info, i))
                    r.badges |= 1 << i;
            }
        }
    }
    {
        PlayTime *pt = SaveData_GetPlayTime(save);

        if (pt != NULL)
            r.play_seconds = (s32)PlayTime_GetHours(pt) * 3600
                             + (s32)PlayTime_GetMinutes(pt) * 60
                             + (s32)PlayTime_GetSeconds(pt);
    }
    {
        FieldOverworldState *field = SaveData_GetFieldOverworldState(save);
        Location *loc = FieldOverworldState_GetPlayerLocation(field);

        if (loc != NULL) {
            /* The same split the local warp report lands on: the engine's map
             * header id is a bank in its high byte and a map in its low one,
             * and the region is the character's, which only the server knows. */
            r.position.bank = (u8)(((int)loc->mapHeaderID >> 8) & 0xFF);
            r.position.map = (u16)((int)loc->mapHeaderID & 0xFF);
            r.position.x = (s16)loc->x;
            r.position.y = (s16)loc->z;
        }
        /*
         * Where a white out lands, which is a row of the engine's spawn table and not a tile.
         */
        if (field != NULL)
            r.black_out_warp = FieldOverworldState_GetBlackOutWarpId(field);
    }

    r.nmonsters = read_party(save, mons, IMPORT_MON_MAX);
    r.nmonsters += read_boxes(save, mons + r.nmonsters,
                              IMPORT_MON_MAX - r.nmonsters);
    r.monsters = mons;
    r.nbag = read_bag(save, bag, MMO_IMPORT_MAX_BAG);
    r.bag = bag;

    {
        Pokedex *dex = SaveData_GetPokedex(save);

        if (dex != NULL) {
            for (i = 1; i <= NATIONAL_DEX_COUNT; i++) {
                int wire = mmo_species_port_wire_id(i);

                if (wire <= 0)
                    continue;
                if (Pokedex_HasSeenSpecies(dex, (u16)i))
                    seen[r.ndex_seen++] = (u16)wire;
                if (Pokedex_HasCaughtSpecies(dex, (u16)i))
                    caught[r.ndex_caught++] = (u16)wire;
            }
        }
    }
    r.dex_seen = seen;
    r.dex_caught = caught;

    {
        VarsFlags *vf = SaveData_GetVarsFlags(save);

        if (vf != NULL) {
            /*
             * Flag 0 is the engine's own "never set" slot; every other set bit crosses as the
             * number it is.
             */
            for (i = 1; i < NUM_FLAGS; i++) {
                if (vf->flags[i / 8] & (1u << (i % 8)))
                    flags[r.nflags++] = (u16)i;
            }
            for (i = 0; i < NUM_VARS; i++) {
                if (vf->vars[i] == 0)
                    continue;
                vars[r.nvars].id = (u16)(VARS_START + i);
                vars[r.nvars].value = (s16)vf->vars[i];
                r.nvars++;
            }
        }
    }
    r.flags = flags;
    r.vars = vars;

    {
        int n = openmmo_save_blocks_all(save, blocks,
                                        (int)(sizeof blocks / sizeof blocks[0]));
        static mmo_import_block out[MMO_SAVE_BLOCK_MAX];

        for (i = 0; i < n; i++) {
            out[i].id = (u8)blocks[i].id;
            out[i].data = blocks[i].data;
            out[i].len = (size_t)blocks[i].len;
        }
        r.blocks = out;
        r.nblocks = n;
    }

    {
        const char *rev = getenv("OPENMMO_REVISION");

        r.client_revision = (rev != NULL && rev[0] != '\0') ? atoi(rev) : -1;
    }
    file_sha256(save_path, r.sha256);

    if ((size_t)snprintf(path, sizeof path, "%s.report", save_path) >= sizeof path) {
        fprintf(stderr, "openmmo: the save path is too long to write a report"
                " beside: %s\n", save_path);
        goto out;
    }
    if (mmo_import_report_write(path, &r) != 0)
        goto out;
    printf("openmmo: this save is ready to take online: %s\n", path);
    printf("openmmo:   %d in the party and the boxes, %d bag lines, %d money,"
           " badges 0x%02x, %d caught, %d seconds played, spawn row %d\n",
           r.nmonsters, r.nbag, r.money, (unsigned)r.badges, r.ndex_caught,
           r.play_seconds, (int)r.black_out_warp);
    rc = 0;

out:
    free(mons);
    free(bag);
    free(seen);
    free(caught);
    free(flags);
    free(vars);
    return rc;
}

/* --- the offline half: write the report as the game ends ------------------ */

static char s_save_path[1024];

/* The frame the session stopped on, beside the save under `.frames`. */
static void write_end_frame(const char *save_path)
{
    char path[1200];
    FILE *f;

    if ((size_t)snprintf(path, sizeof path, "%s.frames", save_path) >= sizeof path)
        return;
    f = fopen(path, "wb");
    if (f == NULL)
        return;
    fprintf(f, "%llu\n", pc_irq_frames());
    fclose(f);
}

static void write_report_at_exit(void)
{
    SaveData *save = SaveData_Ptr();

    if (save == NULL || s_save_path[0] == '\0')
        return;
    /* The report's header carries the FILE's hash, and the port defers the
     * file write until the burst settles, so ask for it before reading. */
    pc_card_backup_sync();
    {
        char why[64];

        /*
         * A save holding a species this build cannot answer for gets no report and no end
         * frame: a report missing a monster would offer the server a character with a hole in
         * it, and this run could not have read the monster to put in it anyway.
         */
        if (openmmo_offline_save_unsupported(save, why, sizeof why) != 0) {
            printf("openmmo: no report for this save: %s\n", why);
            return;
        }
    }
    (void)write_report(save, s_save_path);
    write_end_frame(s_save_path);
}

/* --- the online half: offer a report the front door named ----------------- */

static int s_offered;
static int s_answered;
static int s_chain_offered;
static int s_chain_answered;

/* A LANDED SAVE ends the session. */
#define IMPORT_CHAIN_SECONDS 20
#define IMPORT_LEAVE_SECONDS 3

static int s_landed;
static long s_landed_at;
static int s_left;

/* The report is done with once the server has taken it. Renamed rather than
 * deleted: the front door reads `<report>.landed` to say what happened, and a
 * report still at its name would be offered again on the next Play, as a save
 * newer than the last export, which it still is. */
static void retire_report(const char *path)
{
    char landed[1024 + 16];

    if ((size_t)snprintf(landed, sizeof landed, "%s.landed", path) >=
            sizeof landed ||
        mmo_plat_rename_over(path, landed) != 0) {
        printf("openmmo: the offered save's report could not be set aside;"
               " the front door may offer it again\n");
        return;
    }
    printf("openmmo: the offered save is on the server; its report is kept"
           " as %s\n", landed);
}

/*
 * The frames after a landed answer: hold for the chain's answer when one was offered, then
 * leave, once.
 */
static int leave_when_done(openmmo_client *c)
{
    long held = mmo_plat_seconds() - s_landed_at;

    gSystem.pressedKeys = 0;
    gSystem.pressedKeysRepeatable = 0;
    gSystem.heldKeys = 0;
    if (s_chain_offered && !s_chain_answered) {
        const openmmo_import_answer *a =
            c != NULL ? openmmo_client_import_answer(c) : NULL;

        if (a != NULL && a->status >= MMO_IMPORT_STATUS_CHECK_QUEUED) {
            s_chain_answered = 1;
            openmmo_import_print_answer(a);
        } else if (held <= IMPORT_CHAIN_SECONDS) {
            return 0;
        } else {
            printf("openmmo: no word from the server about the session"
                   " records after %d s; leaving with the save landed\n",
                   IMPORT_CHAIN_SECONDS);
        }
    } else if (held <= IMPORT_LEAVE_SECONDS) {
        return 0;
    }
    s_left = 1;
    return 1;
}

static void offer_report(openmmo_client *c, const char *path)
{
    u8 *report = NULL;
    size_t len = 0;

    if (mmo_import_report_read(path, &report, &len) != 0) {
        printf("openmmo: the save at %s could not be offered\n", path);
        return;
    }
    if (openmmo_client_send_offline_report(c, report, len) == 0)
        printf("openmmo: offered %zu bytes of save to the server\n", len);
    else
        printf("openmmo: the save could not be sent\n");
    free(report);
}

/* The play behind the save, once the save itself has landed and the server has
 * said it would look at it. Sent second and separately: a chain is evidence for
 * an import that exists, and a server with no replay worker throws every byte
 * of it away, so it is worth asking before spending a player's uplink on it. */
static void offer_chain(openmmo_client *c, const char *path)
{
    u8 *chain = NULL;
    size_t len = 0;

    if (mmo_chain_read(path, &chain, &len) != 0) {
        printf("openmmo: the sessions behind that save could not be offered\n");
        return;
    }
    if (openmmo_client_send_offline_chain(c, chain, len) == 0)
        printf("openmmo: offered %zu bytes of session records\n", len);
    else
        printf("openmmo: the session records could not be sent\n");
    free(chain);
}

/* The server's one sentence about a blob this client sent, in the player's
 * words. Shared with the export (openmmo_offline.c), which sends the third
 * blob up the same channel and gets its answer the same way. */
void openmmo_import_print_answer(const openmmo_import_answer *a)
{
    /* One entry per MMO_IMPORT_STATUS_*, in order. Sized by the count so a
     * status added to the list without a word here fails to compile rather
     * than reading past the end. */
    static const char *const WORD[MMO_IMPORT_STATUS_COUNT] = {
        "landed", "try again", "refused",
        "is queued to be checked", "will not be checked",
        "was kept", "was not kept",
        "is on file",
    };
    int i;

    if (a->status < 0 || a->status >= MMO_IMPORT_STATUS_COUNT)
        return;
    if (a->status == MMO_IMPORT_STATUS_EXPORT_KEPT ||
        a->status == MMO_IMPORT_STATUS_EXPORT_DECLINED)
        printf("openmmo: your offline copy %s: %s\n", WORD[a->status],
               a->message);
    else if (a->status >= MMO_IMPORT_STATUS_CHECK_QUEUED)
        printf("openmmo: your offline play %s: %s\n", WORD[a->status],
               a->message);
    else
        printf("openmmo: your save %s: %s\n", WORD[a->status], a->message);
    for (i = 0; i < a->nnotes; i++)
        printf("openmmo:   %s\n", a->notes[i]);
    if (a->nnotes_sent > a->nnotes)
        printf("openmmo:   (and %d more)\n", a->nnotes_sent - a->nnotes);
}

/*
 * One frame's worth of both halves. Called from openmmo_mod_frame, and answers 1 on the one
 * frame the session should end on a landed save.
 */
int openmmo_import_tick(openmmo_client *c)
{
    static int armed;
    const char *want;

    if (!openmmo_session_configured()) {
        const char *save;

        if (armed)
            return 0;
        save = getenv("PC_SAVE");
        if (save == NULL || save[0] == '\0' || strcmp(save, "none") == 0)
            return 0;
        armed = 1;
        snprintf(s_save_path, sizeof s_save_path, "%s", save);
        if (atexit(write_report_at_exit) != 0) {
            fprintf(stderr, "openmmo: this save will not be written down for"
                    " taking online\n");
            s_save_path[0] = '\0';
        }
        return 0;
    }

    want = getenv("OPENMMO_IMPORT");
    if (want == NULL || want[0] == '\0')
        return 0;
    if (s_left)
        return 0;
    /* Before the status gate: a link that drops during the hold still ends
     * the session, on its deadline, rather than holding a dead world. */
    if (s_landed)
        return leave_when_done(c);
    if (c == NULL || openmmo_client_status(c) != OPENMMO_IN_GAME)
        return 0;
    if (!s_offered) {
        s_offered = 1;
        offer_report(c, want);
        return 0;
    }
    if (!s_answered) {
        const openmmo_import_answer *a = openmmo_client_import_answer(c);

        if (a != NULL && a->status != MMO_IMPORT_STATUS_NONE) {
            const char *chain = getenv("OPENMMO_IMPORT_CHAIN");

            s_answered = 1;
            openmmo_import_print_answer(a);
            if (a->status != MMO_IMPORT_STATUS_LANDED)
                return 0;
            s_landed = 1;
            s_landed_at = mmo_plat_seconds();
            retire_report(want);
            /* The chain only when the server asked. A server that is not
             * replaying anything would read megabytes and drop them. */
            if (a->wants_chain && chain != NULL && chain[0] != '\0') {
                s_chain_offered = 1;
                offer_chain(c, chain);
            }
        }
    }
    return 0;
}
