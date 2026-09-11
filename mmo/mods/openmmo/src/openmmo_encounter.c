/* Start the engine's own battle overlay from a server-opened fight. */

#include <stdio.h>
#include <string.h>

#include "constants/battle.h"
#include "constants/heap.h"
#include "constants/items.h"
#include "constants/species.h"
#include "encounter.h"
#include "field/field_system.h"
#include "field_battle_data_transfer.h"
#include "field_system.h"
#include "heap.h"
#include "map_header.h"
#include "message.h"
#include "overlay006/wild_encounters.h"
#include "party.h"
#include "pokemon.h"
#include "save_player.h"
#include "savedata.h"
#include "system_vars.h"
#include "trainer_info.h"
#include "unk_02054884.h"
#include "vars_flags.h"

#include "../../../include/charcode.h"
#include "../../../include/species_port.h"
#include "../../../include/client.h"
#include "../../../include/idmap.h"

/* A move past Shadow Force is one only a fill serves. */
extern int openmmo_move_served(unsigned move);
extern void openmmo_static_battle_started(FieldSystem *fs);      /* openmmo_static.c */
extern void openmmo_static_battle_ended(FieldSystem *fs, u32 result);
extern int openmmo_static_script_result(void);

extern void openmmo_script_gave_pokemon_named(int species, int level, int hp,
                                              const void *nick_charcodes); /* openmmo_boot.c */

/* Set when the engine consumed an item (openmmo_bag.c), the party may have
 * been healed by it; cleared by whichever report next carries the party. */
static int s_party_touched;

/* Settled frames an outcome this side cannot name yet is held for. Naming one
 * costs a grant and the containers that answer it, which is a round trip; a
 * party still unnameable after that is one no grant can name either, and
 * holding the errand for ever would hide that instead of saying it. */
#define OUTCOME_HELD_MAX 600
/* A reconcile pass reads all 540 PC slots, so the ask that resolves the hold
 * is paced rather than repeated every settled frame. */
#define OUTCOME_ASK_EVERY 60
static int s_outcome_held;

static int s_scene;
static int s_saw_child;
/* The transfer the running fight was started with, and the result it carries
 * once the battle has written one. The engine frees the transfer on the
 * encounter task's own step after the child returns, so it is read only while
 * the child is up. */
static FieldBattleDTO *s_dto;
static u32 s_result;

/* A server battle that arrived while a field task still ran, the rock's
 * shatter, the tree's shake, held for the frame the field is free. */
#define DEFER_MAX_FRAMES 900
static int s_deferred;
static int s_deferred_foe;
static int s_deferred_lv;
static int s_deferred_frames;

/* Which server monster each seated engine slot is, so the outcome of a fight
 * can be written back onto the record it belongs to. Sparse server slots (an
 * untranslatable species) are skipped at seat time, so engine slot i is not
 * server slot i and the id is the only honest key. */
static s64 s_slot_id[OPENMMO_PARTY_MAX];
static int s_slots;

/* The mirror the seated party was built from, and whether there has been a seat at all. */
static openmmo_party s_seated_from;
static int s_seated;
/* How many of that mirror the engine could actually hold. Not the same
 * number: a member this build has no species table for is dropped. */
static int s_seated_count;

int openmmo_encounter_scene_up(void)
{
    return s_scene;
}

/* The met date, out of the record's unix seconds. */
static void met_date_of(s32 unix_seconds, u8 *year, u8 *month, u8 *day)
{
    long z = (long)(unix_seconds / 86400) + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned long doe = (unsigned long)(z - era * 146097);
    unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned long mp = (5 * doy + 2) / 153;
    unsigned long d = doy - (153 * mp + 2) / 5 + 1;
    unsigned long mth = mp < 10 ? mp + 3 : mp - 9;

    if (mth <= 2)
        y++;
    y -= 2000;
    if (y < 0)
        y = 0;
    if (y > 255)
        y = 255;
    *year = (u8)y;
    *month = (u8)mth;
    *day = (u8)d;
}

/* Build one seated party monster from one server record. */
/*
 * A record whose species this build's tables cannot hold is not seated, and the refusal says
 * so once per species per run and names the cure.
 */
int openmmo_species_seat_live(int wire)
{
    static u8 said[(MMO_SPECIES_MAX + 8) / 8];

    if (mmo_species_port_live(mmo_species_port_engine_id(wire)))
        return 1;
    if (wire >= 0 && wire <= MMO_SPECIES_MAX
            && !(said[wire >> 3] & (u8)(1u << (wire & 7)))) {
        said[wire >> 3] |= (u8)(1u << (wire & 7));
        printf("openmmo: species %d is past this build's tables (species to"
               " %d); not seated -- fill mods/imports and list it in `mods`\n",
               wire, mmo_species_port_max());
    }
    return 0;
}

int openmmo_seat_build_mon(Pokemon *mon, SaveData *save,
                           const openmmo_party_mon *m)
{
    int level = m->level;
    int met_location = 0;
    u32 otid;
    int s;

    if (level < 1)
        level = 1;
    if (level > 100)
        level = 100;

    if (!openmmo_species_seat_live(m->species))
        return 0;

    /* Read before the monster exists, because the personality below is chosen
     * against it: this is the id Pokemon_SetCatchData is about to stamp on. */
    otid = TrainerInfo_ID(SaveData_GetTrainerInfo(save));

    Pokemon_Init(mon);
    /* Victini and Snivy are 650 and 651 inside the engine (species_port.h). */
    Pokemon_InitWith(mon, (u16)mmo_species_port_engine_id(m->species), level,
                     INIT_IVS_RANDOM, TRUE,
                     mmo_mon_shiny_personality(m->seed, otid, m->shiny,
                                               m->nature),
                     OTID_NOT_SET, 0);
    /*
     * The engine keeps a location label, not a map: several headers share one name, and the
     * summary screen prints the label's string. A record with no place keeps 0, which is the
     * label official itself shows as Mystery Zone.
     */
    if (m->caught_location_label > 0)
        met_location = m->caught_location_label;
    else if (m->caught_map_header >= 0)
        met_location = (int)MapHeader_GetMapLabelTextID(
            (enum MapHeaderID)m->caught_map_header);
    Pokemon_SetCatchData(mon, SaveData_GetTrainerInfo(save), ITEM_POKE_BALL,
                         met_location, TERRAIN_MAX, HEAP_ID_FIELD2);
    /* SetCatchData stamped the RTC's own date on the way through, which is
     * today for a monster caught in March. The record's timestamp is the one
     * that means something; a record without one keeps what it stamped. */
    if (m->caught_at > 0) {
        u8 y, mo, d;

        met_date_of(m->caught_at, &y, &mo, &d);
        Pokemon_SetValue(mon, MON_DATA_MET_YEAR, &y);
        Pokemon_SetValue(mon, MON_DATA_MET_MONTH, &mo);
        Pokemon_SetValue(mon, MON_DATA_MET_DAY, &d);
    }

    /*
     * The record's own progress, not a fresh roll: the fight this party is being built for
     * adds experience ON TOP of what the server holds, and a mon rebuilt at the bottom of its
     * level forgot every fight before this one.
     */
    if (m->xp > 0) {
        u32 exp = (u32)m->xp;
        Pokemon_SetValue(mon, MON_DATA_EXPERIENCE, &exp);
    }
    for (s = 0; s < 6; s++) {
        u32 iv = m->iv[s] > 31 ? 31 : m->iv[s];
        u32 ev = m->ev[s];

        Pokemon_SetValue(mon, MON_DATA_HP_IV + s, &iv);
        Pokemon_SetValue(mon, MON_DATA_HP_EV + s, &ev);
    }
    /* How much it likes its trainer. */
    {
        u8 friendship = (u8)(m->friendship < 0 ? 0
                             : (m->friendship > 255 ? 255 : m->friendship));

        Pokemon_SetValue(mon, MON_DATA_FRIENDSHIP, &friendship);
    }
    /*
     * What it is carrying. Half of this game's rules read it and every one of them is the
     * engine's own: the Everstone the evolution screen asks before it runs, the Razor Claw
     * that is the trigger for a Weavile, the Leftovers the battle heals from each turn, the
     * Exp.
     */
    {
        u16 held = m->held_item_engine;

        Pokemon_SetValue(mon, MON_DATA_HELD_ITEM, &held);
    }
    /*
     * The contest half of the record, and the ribbons are the load-bearing part of it: the
     * party menu counts the ribbons a monster holds of the chosen contest type and refuses any
     * rank above that count, so a monster seated without them can only ever enter Normal rank
     * however many contests it has won.
     */
    for (s = 0; s < MMO_MON_CONDITIONS; s++) {
        u8 cond = m->cond[s];

        Pokemon_SetValue(mon, MON_DATA_COOL + s, &cond);
    }
    {
        u8 sheen = m->sheen;

        Pokemon_SetValue(mon, MON_DATA_SHEEN, &sheen);
    }
    for (s = 0; s < MMO_MON_CONDITIONS * MMO_MON_RIBBON_RANKS; s++) {
        u8 on = (u8)((m->ribbons_super >> s) & 1);

        Pokemon_SetValue(mon, MON_DATA_SUPER_COOL_RIBBON + s, &on);
    }
    /* A move past Shadow Force is one only a fill serves; without one the
     * slot stays empty rather than indexing past six archives. */
    for (s = 0; s < 4; s++) {
        if (m->move[s] != 0 && openmmo_move_served(m->move[s])) {
            Pokemon_ResetMoveSlot(mon, m->move[s], (u8)s);
            if (m->move_pp[s] != 0) {
                u8 pp = m->move_pp[s];
                Pokemon_SetValue(mon, MON_DATA_MOVE1_PP + s, &pp);
            }
        }
    }
    /*
     * The name its trainer typed. The record carried it all along (the database showed DEDE);
     * this seat just never wrote it, so every screen fell back to the species name.
     */
    if (m->nickname[0] != '\0') {
        mmo_charcode nick[MON_NAME_LEN + 4];
        size_t k;

        for (k = 0; k < (size_t)(MON_NAME_LEN + 4); k++)
            nick[k] = MMO_CHAR_EOS;
        mmo_utf8_to_charcode(m->nickname, nick, MON_NAME_LEN + 1);
        Pokemon_SetValue(mon, MON_DATA_NICKNAME_AND_FLAG, nick);
    }

    /* An egg, and the engine is what hatches one. */
    if (m->egg) {
        charcode_t egg_name[MON_NAME_LEN + 1];
        u8 is_egg = TRUE;
        u8 has_nickname = FALSE;

        MessageLoader_GetSpeciesName(SPECIES_EGG, HEAP_ID_SYSTEM, egg_name);
        Pokemon_SetValue(mon, MON_DATA_NICKNAME, egg_name);
        Pokemon_SetValue(mon, MON_DATA_HAS_NICKNAME, &has_nickname);
        Pokemon_SetValue(mon, MON_DATA_IS_EGG, &is_egg);
    }

    /*
     * Last, over the experience and the IVs and EVs above, because every one of them moves a
     * stat. Then the HP: the stat calculation writes a full bar, and the record's own current
     * HP is what the overworld starts from.
     */
    Pokemon_CalcLevelAndStats(mon);
    if (m->hp >= 0) {
        u32 maxhp = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL);
        u16 hp = (u16)(m->hp > (int)maxhp ? (int)maxhp : m->hp);
        Pokemon_SetValue(mon, MON_DATA_HP, &hp);
    }
    /*
     * What it is suffering from. The record keeps the engine's own condition word, so this is
     * the number going straight back where it came from.
     */
    {
        u32 status = (u32)m->status & MMO_MON_STATUS_MASK;

        Pokemon_SetValue(mon, MON_DATA_STATUS, &status);
    }
    return 1;
}

static int seat_engine_party(SaveData *save, const openmmo_party *p)
{
    Party *party;
    Pokemon *mon;
    int i, n;

    if (save == NULL || p == NULL || !p->valid)
        return 0;
    {
        extern void openmmo_pc_track_reset_container(int cont);
        openmmo_pc_track_reset_container(OPENMMO_CONTAINER_PARTY);
    }
    party = SaveData_GetParty(save);
    Party_Init(party);
    mon = Pokemon_New(HEAP_ID_FIELD2);
    if (mon == NULL)
        return 0;
    n = 0;
    for (i = 0; i < p->count; i++) {
        const openmmo_party_mon *m = &p->mon[i];

        if (m->species == 0)
            continue;
        if (!openmmo_seat_build_mon(mon, save, m))
            continue;
        if (!Party_AddPokemon(party, mon))
            continue;
        if (n < OPENMMO_PARTY_MAX) {
            s_slot_id[n] = m->id;
            {
                extern void openmmo_pc_track(u32 pid, s64 id, int cont, int slot);
                openmmo_pc_track(Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL),
                                 m->id, OPENMMO_CONTAINER_PARTY, n);
            }
        }
        n++;
    }
    Heap_Free(mon);
    s_slots = n > OPENMMO_PARTY_MAX ? OPENMMO_PARTY_MAX : n;
    /* What this seat was built from, so the window can tell which half of the
     * pair holds the newer party (openmmo_party_seat_is_current). */
    s_seated_from = *p;
    s_seated = 1;
    s_seated_count = n;
    return n;
}

/*
 * Which engine party slot holds server monster `id`, or -1 for one the engine is not holding.
 */
int openmmo_party_engine_slot(s64 id)
{
    int i;

    if (id == 0)
        return -1;
    for (i = 0; i < s_slots; i++)
        if (s_slot_id[i] == id)
            return i;
    return -1;
}

/* Whether the seated party was built from the mirror as it now stands, that
 * is, whether the engine is the live copy of these monsters. See s_seated. */
int openmmo_party_seat_is_current(const openmmo_client *c)
{
    const openmmo_party *p;

    if (!s_seated || c == NULL)
        return 0;
    p = openmmo_client_party(c);
    if (p == NULL || !p->valid)
        return 0;
    return memcmp(&s_seated_from, p, sizeof s_seated_from) == 0;
}

/* After the box screen shuffled the containers, re-derive which server
 * monster each party slot holds, so the outcome report keeps writing onto
 * the right records. The PID table in openmmo_pc.c is the identity. */
void openmmo_party_rebind(FieldSystem *fs)
{
    extern int openmmo_pc_id_for_pid(u32 pid, s64 *out);
    Party *party;
    int i, n;

    if (fs == NULL || fs->saveData == NULL)
        return;
    party = SaveData_GetParty(fs->saveData);
    n = Party_GetCurrentCount(party);
    if (n > OPENMMO_PARTY_MAX)
        n = OPENMMO_PARTY_MAX;
    for (i = 0; i < n; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);
        s64 id;

        if (mon != NULL
            && openmmo_pc_id_for_pid(
                   Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL), &id))
            s_slot_id[i] = id;
        else
            s_slot_id[i] = 0;
    }
    s_slots = n;
}

/* Read what the fight left each seated monster as, and tell the server. */
/* Whether the record for server monster `id` names an item this build has no
 * engine id for. The seat leaves such a monster's hand empty (openmmo_link.c's
 * seat and this file's both take `held_item_engine`, which is 0 for one), so an
 * empty hand on the way back does not mean the player put the item down. */
static int unseatable_item(const openmmo_client *c, s64 id)
{
    const openmmo_party *p;
    int k;

    if (c == NULL || id == 0)
        return 0;
    p = openmmo_client_party(c);
    if (p == NULL || !p->valid)
        return 0;
    for (k = 0; k < p->count; k++) {
        if (p->mon[k].id != id)
            continue;
        return p->mon[k].held_item != 0 && p->mon[k].held_item_engine == 0;
    }
    return 0;
}

static int report_battle_outcome(FieldSystem *fs, openmmo_client *c)
{
    mmo_battle_mon_outcome out[OPENMMO_PARTY_MAX];
    Party *party;
    Pokemon *mon;
    const char *why;
    int i, s, n = 0;

    if (fs == NULL || fs->saveData == NULL || c == NULL)
        return 1; /* nobody to tell: nothing is owed */
    party = SaveData_GetParty(fs->saveData);
    if (Party_GetCurrentCount(party) == 0)
        return 1; /* an empty party has no outcome to carry */
    /* No slot carries a server id, so nothing has seated this party yet, a
     * starter granted mid-scene and fought in the same scene is the case, and
     * the fight was thrown away here. The identity table knows a monster the
     * seat never covered, so ask it before giving up on the level-ups. */
    if (s_slots <= 0)
        openmmo_party_rebind(fs);
    /* A slot the identity table still cannot name, named from the record instead. */
    {
        const openmmo_party *p = openmmo_client_party(c);
        int live = 0, k, j;

        if (p != NULL && p->valid) {
            for (k = 0; k < p->count; k++)
                if (p->mon[k].species != 0)
                    live++;
            for (k = 0, j = 0; k < p->count && j < s_slots; k++) {
                if (p->mon[k].species == 0)
                    continue;
                if (live == s_slots && s_slot_id[j] == 0) {
                    Pokemon *held = Party_GetPokemonBySlotIndex(party, j);

                    if (held != NULL
                        && (u16)Pokemon_GetValue(held, MON_DATA_SPECIES, NULL)
                               == (u16)mmo_species_port_engine_id(p->mon[k].species))
                        s_slot_id[j] = p->mon[k].id;
                }
                j++;
            }
        }
    }
    for (i = 0; i < s_slots; i++) {
        if (s_slot_id[i] == 0)
            continue; /* a slot whose record the rebind could not name */
        mon = Party_GetPokemonBySlotIndex(party, i);
        if (mon == NULL)
            continue;
        out[n].id = s_slot_id[i];
        out[n].level = (u8)Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL);
        out[n].exp = (s32)Pokemon_GetValue(mon, MON_DATA_EXPERIENCE, NULL);
        out[n].hp = (s16)Pokemon_GetValue(mon, MON_DATA_HP, NULL);
        for (s = 0; s < 4; s++) {
            u16 ds_move = (u16)Pokemon_GetValue(mon, MON_DATA_MOVE1 + s, NULL);

            why = NULL;
            out[n].move[s] = ds_move ? mmo_id_move_to_server(ds_move, &why) : 0;
            out[n].pp[s] = (u8)Pokemon_GetValue(mon, MON_DATA_MOVE1_PP + s, NULL);
        }
        /* What a contest or a poffin left behind. Read on every report, not
         * only after a contest: this side cannot tell which scene ran, and
         * sending the values the engine currently holds is right for both,
         * a fight leaves them exactly as they were seated. */
        for (s = 0; s < MMO_MON_CONDITIONS; s++)
            out[n].cond[s] = (u8)Pokemon_GetValue(mon, MON_DATA_COOL + s, NULL);
        out[n].sheen = (u8)Pokemon_GetValue(mon, MON_DATA_SHEEN, NULL);
        out[n].ribbons_super = 0;
        for (s = 0; s < MMO_MON_CONDITIONS * MMO_MON_RIBBON_RANKS; s++) {
            if (Pokemon_GetValue(mon, MON_DATA_SUPER_COOL_RIBBON + s, NULL))
                out[n].ribbons_super |= (u64)1 << s;
        }
        /* What the engine holds it as now, in the server's numbering (species_port.h). */
        out[n].species = (u16)mmo_species_port_wire_id(
            (int)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL));
        /* How much it likes its trainer. Every rule that moves this is the
         * engine's own: 128 steps of walking, a level up, a contest win, a
         * faint. Read on every report for the same reason the contest
         * conditions are, that this side cannot tell which of them just ran. */
        out[n].friendship =
            (s16)Pokemon_GetValue(mon, MON_DATA_FRIENDSHIP, NULL);
        /* Whether the engine still holds it as an egg. */
        out[n].egg = (u8)(Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL) != 0);
        /* What the fight, or the Center, or the Full Heal left it as. Read on
         * every report like the rest of this row: a status is set and cleared in
         * a dozen places that are all the engine's, and this side cannot tell
         * which of them just ran. */
        out[n].status = (u16)(Pokemon_GetValue(mon, MON_DATA_STATUS, NULL)
                              & MMO_MON_STATUS_MASK);
        /*
         * What it is carrying now. The party menu's GIVE and TAKE are what move this, and each
         * of them moves the bag as well.
         */
        {
            u16 held = (u16)Pokemon_GetValue(mon, MON_DATA_HELD_ITEM, NULL);
            u16 wire = 0;

            why = NULL;
            if (held != 0)
                wire = mmo_id_item_to_server(held, &why);
            if (held != 0 && wire == MMO_ID_NONE) {
                printf("openmmo: held engine item %u has no wire id (%s)\n",
                       (unsigned)held, why != NULL ? why : "untranslatable");
                out[n].held_item = -1;
            } else if (held == 0 && unseatable_item(c, s_slot_id[i])) {
                out[n].held_item = -1;
            } else {
                out[n].held_item = (s16)wire;
            }
        }
        n++;
    }
    if (n == 0)
        return 0; /* held, not sent, the caller keeps the errand */
    /* An in-battle item marks the party touched too; this report covers it,
     * so the touched pass afterwards must not send the same rows again. */
    s_party_touched = 0;
    if (openmmo_client_send_battle_outcome(c, out, n) == 0) {
        for (i = 0; i < n; i++)
            printf("openmmo: battle outcome mon %d: species %d lv %d exp %d"
                   " hp %d\n",
                   i, out[i].species, out[i].level, out[i].exp, out[i].hp);
    }
    /* Captures, party or forced to a box, are found by the container
     * reconcile in openmmo_pc.c: any monster the identity table does not
     * know is granted where it sits. One path for both, so a full-party
     * catch is not silently engine-only. */
    {
        extern void openmmo_pc_request_reconcile(void);
        openmmo_pc_request_reconcile();
    }
    return 1;
}

/* The wild DTO for a server-opened fight: the seated party against the foe
 * the server named, drawn as the engine draws a grass encounter's. The
 * caller starts it, from the field (below) or from inside the task that
 * asked for it (the fishing task, openmmo_fishing.c). */
int openmmo_encounter_build_wild(FieldSystem *fs, const openmmo_client *c,
                                 int foe_species, int foe_level,
                                 FieldBattleDTO **out)
{
    const openmmo_party *party;
    FieldBattleDTO *dto;
    const char *why = NULL;
    u16 species;
    int level;

    if (out != NULL)
        *out = NULL;
    if (fs == NULL || fs->saveData == NULL || c == NULL || out == NULL)
        return -1;
    party = openmmo_client_party(c);
    if (seat_engine_party(fs->saveData, party) < 1) {
        printf("openmmo: platinum battle needs a party the engine can draw\n");
        return -1;
    }

    species = mmo_id_species_from_server((u16)foe_species, &why);
    if (species != 0 && !mmo_species_port_live((int)species)) {
        species = 0;
        why = "species past this build's tables; fill mods/imports";
    }
    if (species == 0) {
        printf("openmmo: platinum battle cannot draw foe species %d%s%s\n",
               foe_species, why ? ": " : "", why ? why : "");
        return -1;
    }
    level = foe_level;
    if (level < 1)
        level = 1;
    if (level > 100)
        level = 100;

    dto = FieldBattleDTO_New(HEAP_ID_FIELD2, BATTLE_TYPE_WILD_MON);
    if (dto == NULL)
        return -1;
    FieldBattleDTO_Init(dto, fs);
    CreateWildMon_Scripted(fs, species, (u8)level, dto);
    *out = dto;
    printf("openmmo: platinum battle vs species %u level %d\n", species, level);
    return 0;
}

/* A fight this file did not start is running: the fishing task started the
 * DTO above itself. The poll then reports its outcome like any other. */
void openmmo_encounter_mark_started(void)
{
    s_scene = 1;
    s_saw_child = 0;
    /* The last fight's mask is not this one's. Without this a scene that ends
     * in a run reports the previous fight's win. */
    s_result = BATTLE_IN_PROGRESS;
}

/* Whether the field could not open a fight right now for a reason that
 * passes: a task on it, a child process over it, or no field at all. */
int openmmo_encounter_field_busy(const FieldSystem *fs)
{
    if (fs == NULL)
        return 0;
    if (s_scene)
        return 0;
    return fs->task != NULL || !FieldSystem_IsRunningFieldMap((FieldSystem *)fs)
        || FieldSystem_HasChildProcess((FieldSystem *)fs);
}

int openmmo_encounter_deferred(void)
{
    return s_deferred;
}

void openmmo_encounter_defer(int foe_species, int foe_level)
{
    s_deferred = 1;
    s_deferred_foe = foe_species;
    s_deferred_lv = foe_level;
    s_deferred_frames = 0;
    printf("openmmo: platinum battle held for the field to settle\n");
}

int openmmo_encounter_start(FieldSystem *fs, const openmmo_client *c,
                            int foe_species, int foe_level)
{
    FieldBattleDTO *dto;

    if (fs == NULL || fs->saveData == NULL || c == NULL)
        return -1;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return -1;
    if (s_scene)
        return -1;
    if (openmmo_encounter_build_wild(fs, c, foe_species, foe_level, &dto) != 0)
        return -1;
    Encounter_NewVsWild(fs, dto);
    if (fs->task == NULL) {
        printf("openmmo: platinum battle would not start\n");
        return -1;
    }
    s_scene = 1;
    s_saw_child = 0;
    s_dto = dto;
    s_result = BATTLE_IN_PROGRESS;
    openmmo_static_battle_started(fs);
    return 0;
}

/*
 * An item used outside battle healed the engine's field party, state the server has no other
 * way to hear, because the outcome report only runs after battles and the next battle rebuilds
 * its party from the server's mirror: the playtest drank a potion, fought, and the fight
 * started at 2 HP as if the potion never was.
 */
void openmmo_party_mark_touched(void)
{
    s_party_touched = 1;
}

/*
 * The engine raises the party's friendship every 128 steps in the overworld
 * (Field_UpdateFriendship), and unlike every other thing that writes a party member it runs
 * under no menu and no battle, so the "a child process is over the field" rule that marks a
 * potion or a fight misses it entirely.
 */
static void watch_friendship_steps(FieldSystem *fs)
{
    static int have_last;
    static u16 last;
    u16 steps;

    if (fs == NULL || fs->saveData == NULL)
        return;
    steps = SystemVars_GetFriendshipStepCount(SaveData_GetVarsFlags(fs->saveData));
    if (have_last && steps < last)
        openmmo_party_mark_touched();
    last = steps;
    have_last = 1;
}

/* An egg the engine has hatched. */
static void watch_hatched_eggs(FieldSystem *fs, const openmmo_client *c)
{
    const openmmo_party *p;
    Party *party;
    int k, held;

    if (s_party_touched || s_scene || fs == NULL || fs->saveData == NULL
        || c == NULL)
        return;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return;
    p = openmmo_client_party(c);
    if (p == NULL || !p->valid)
        return;
    party = SaveData_GetParty(fs->saveData);
    held = Party_GetCurrentCount(party);
    for (k = 0; k < p->count; k++) {
        Pokemon *mon;
        int slot;

        if (!p->mon[k].egg)
            continue;
        slot = openmmo_party_engine_slot(p->mon[k].id);
        if (slot < 0 || slot >= held)
            continue;
        mon = Party_GetPokemonBySlotIndex(party, slot);
        if (mon == NULL || Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL))
            continue;
        printf("openmmo: the egg in party slot %d hatched\n", slot);
        openmmo_party_mark_touched();
        return;
    }
}

void openmmo_party_report_if_touched(FieldSystem *fs, openmmo_client *c)
{
    extern void openmmo_pc_request_reconcile(void); /* openmmo_pc.c */

    watch_friendship_steps(fs);
    watch_hatched_eggs(fs, c);
    if (!s_party_touched || s_scene)
        return;
    if (fs == NULL || fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return;
    if (report_battle_outcome(fs, c)) {
        /* Discharged: sent, or there was nothing owed, an empty party and a
         * session that is not up both answer that way, and both must clear
         * the flag or every settled frame asks the same question again. */
        s_party_touched = 0;
        s_outcome_held = 0;
        return;
    }
    /*
     * Held, not dropped. The errand is the fight, the exp and the level-ups, and clearing
     * the flag on a pass that said nothing threw a rival battle's level away with no line
     * anywhere to say so.
     */
    if (s_outcome_held % OUTCOME_ASK_EVERY == 0)
        openmmo_pc_request_reconcile();
    if (++s_outcome_held >= OUTCOME_HELD_MAX) {
        printf("openmmo: no party record this side can name after %d settled"
               " frames, the last fight's outcome is dropped\n",
               OUTCOME_HELD_MAX);
        s_party_touched = 0;
        s_outcome_held = 0;
    }
}

/* Keep the field save party a copy of the server's. */
void openmmo_party_field_sync(FieldSystem *fs, const openmmo_client *c)
{
    extern int openmmo_pc_moves_in_flight(void);
    const openmmo_party *p;

    if (fs == NULL || fs->saveData == NULL || c == NULL || s_scene)
        return;
    /* Emitted container moves are still round-tripping; a reseat now would
     * pull the box screen's arrangement out from under the report of it. */
    if (openmmo_pc_moves_in_flight())
        return;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs)) {
        /*
         * A child process over a running field is a battle or a menu, and either can move the
         * party: the exp and the level-ups a fight just produced, a potion drunk in the bag, a
         * slot reordered.
         */
        if (FieldSystem_IsRunningFieldMap(fs) && FieldSystem_HasChildProcess(fs))
            openmmo_party_mark_touched();
        return;
    }
    /*
     * The seat rebuilds every member from the mirror, so running it over a fight the server
     * has not been told about throws that fight away and the report that follows echoes the
     * record back at itself: a level-up shown in the battle and gone by the overworld, and
     * nothing of the party ever durable.
     */
    if (s_party_touched)
        return;
    p = openmmo_client_party(c);
    if (p == NULL || !p->valid)
        return;
    /*
     * The count check catches a save rebuilt under an unchanged mirror: GameStartNewSave re-
     * inits the party on every join, and a rejoin whose mirror bytes matched the last
     * session's would otherwise never reseat.
     */
    if (s_seated && memcmp(&s_seated_from, p, sizeof s_seated_from) == 0
        && Party_GetCurrentCount(SaveData_GetParty(fs->saveData)) == s_seated_count)
        return;
    /* The seat records what it was built from, so there is nothing to note
     * here: a seat that fails leaves the pair as it was and this asks again
     * on the next settled frame. */
    printf("openmmo: field party seated (%d mon(s))\n",
           seat_engine_party(fs->saveData, p));
}

void openmmo_encounter_poll(FieldSystem *fs, openmmo_client *c)
{
    if (s_deferred && !s_scene) {
        if (!openmmo_encounter_field_busy(fs)) {
            s_deferred = 0;
            if (openmmo_encounter_start(fs, c, s_deferred_foe, s_deferred_lv) != 0) {
                printf("openmmo: the held battle would not start; running\n");
                if (c != NULL)
                    openmmo_client_battle_run(c);
            }
        } else if (++s_deferred_frames > DEFER_MAX_FRAMES) {
            s_deferred = 0;
            printf("openmmo: the held battle waited %d frames; running\n",
                   DEFER_MAX_FRAMES);
            if (c != NULL)
                openmmo_client_battle_run(c);
        }
    }
    if (!s_scene)
        return;
    /*
     * A fight this file did not build the transfer for, one of this game's own scripted
     * sites, started on the script's own task, keeps its outcome in the engine's result mask
     * instead, and the encounter writes that mask on the task step after the child returns
     * (CheckPlayerWonEncounter), so reading it only while the child is up reads a zero every
     * time.
     */
    if (s_dto == NULL && fs != NULL && fs->task != NULL) {
        u32 scripted = (u32)openmmo_static_script_result();

        if (scripted != BATTLE_IN_PROGRESS)
            s_result = scripted;
    }
    if (fs != NULL && FieldSystem_HasChildProcess(fs)) {
        s_saw_child = 1;
        if (s_dto != NULL && s_dto->resultMask != BATTLE_IN_PROGRESS)
            s_result = s_dto->resultMask;
        return;
    }
    if (!s_saw_child)
        return;
    /*
     * The child closing is not the results landing: the encounter task copies the battle party
     * back into the save (FieldBattleDTO_UpdateFieldSystem) on its step after the child
     * returns, and reading at child-gone reported the pre-battle party every time, three
     * fights, three identical outcomes, no exp anywhere.
     */
    if (fs != NULL && fs->task != NULL)
        return;
    s_scene = 0;
    s_saw_child = 0;
    s_dto = NULL;
    printf("openmmo: platinum battle ended\n");
    openmmo_static_battle_ended(fs, s_result);
    /* A pass that could not name the party is not a fight that did not
     * happen: mark it and let the settled frames carry it, the same way a
     * scene's end does. */
    if (!report_battle_outcome(fs, c))
        openmmo_party_mark_touched();
    if (c != NULL
        && openmmo_client_battle_state(c) == OPENMMO_BATTLE_ACTIVE)
        openmmo_client_battle_run(c);
}
