/*
 * The engine's PC boxes as a display of the server's, and the box screen's
 * rearrangements as the wire moves the server already answers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/daycare.h"
#include "constants/heap.h"
#include "constants/pokemon.h"
#include "daycare_save.h"
#include "field/field_system.h"
#include "field_system.h"
#include "heap.h"
#include "overlay005/daycare.h"
#include "party.h"
#include "pc_boxes.h"
#include "pokemon.h"
#include "savedata.h"
#include "savedata/save_table.h"
#include "string_template.h"
#include "struct_defs/daycare.h"

#include "../../../include/charcode.h"
#include "../../../include/species_port.h"
#include "../../../include/endpoint.h"
#include "../../../include/client.h"

#define PC_ENGINE_SLOTS (MAX_PC_BOXES * MAX_MONS_PER_BOX) /* 18 * 30 = 540 */
#define TRACK_MAX (OPENMMO_PARTY_MAX + PC_ENGINE_SLOTS + NUM_DAYCARE_MONS)

/* The wire's day care holds what the building holds; say so once rather than
 * letting the two drift. */
typedef char openmmo_pc_daycare_fits[
    (OPENMMO_DAYCARE_MAX == NUM_DAYCARE_MONS) ? 1 : -1];

typedef struct {
    u32 pid;
    s64 id;
    u8  cont;   /* OPENMMO_CONTAINER_* */
    s16 slot;
} pc_track;

static pc_track s_track[TRACK_MAX];
static int s_track_n;
/* Where phase one found each tracked monster, parallel to s_track. */
#define CONT_LOST 0xFF /* the scan did not find it in either container */
static u8  s_now_cont[TRACK_MAX];
static s16 s_now_slot[TRACK_MAX];
/* Phase two's working copy of what the server holds. Kept apart from s_track
 * so a pass that cannot be finished leaves the known layout untouched. */
static u8  s_model_cont[TRACK_MAX];
static s16 s_model_slot[TRACK_MAX];
/* The last PC seat could not put every record the server sent into a box, so
 * the engine is not the whole layout and a move derived from it would name a
 * slot whose real occupant this side cannot see. */
static int s_layout_partial;
static int s_hold;       /* frames to sit still while emitted moves round-trip */
static int s_saw_child;  /* a menu (any child process) was up this settle cycle */
static int s_asked;      /* someone (a battle's end) asked for a reconcile pass */
/* A digest of the storage mirror the current PC seat came from. Not a struct
 * copy: openmmo_storage holds a pointer to its records, so copying the struct
 * compares the live array with itself and never reseats. */
static struct {
    int count, total;
    s64 sum_id;
    long sum_state;
} s_last;
static int s_last_valid;
/* The same idea for the day care: a digest over the server's boarders and the
 * personalities the engine's own block is holding, so the bind runs again
 * whenever either side moves and not once a frame. */
static long s_daycare_sum;
static int s_daycare_valid;
/* Boarders the server named that the engine's own day care block is not
 * holding. The two disagreeing is a monster whose whereabouts nobody here can
 * state, and the grant sweep must not mint one while that is true. */
static int s_daycare_unbound;

/*
 * Grants sent whose answering containers have not landed: their PIDs, so the next reconcile
 * does not grant the same capture twice, each tagged with the container the grant named.
 */
static u32 s_granted_pid[OPENMMO_PARTY_MAX + 8];
static u8 s_granted_cont[OPENMMO_PARTY_MAX + 8];
static int s_granted_n;

/*
 * Releases the box screen itself performed, by PID: the positive signal that a disappearance
 * was chosen.
 */
static u32 s_released_pid[OPENMMO_PARTY_MAX + 8];
static int s_released_n;

static int take_released(u32 pid)
{
    int i;

    for (i = 0; i < s_released_n; i++) {
        if (s_released_pid[i] == pid) {
            s_released_pid[i] = s_released_pid[--s_released_n];
            return 1;
        }
    }
    return 0;
}

/* Called from the engine's box screen, with the BoxPokemon it is about to
 * remove. A monster released before its answering containers ever landed has
 * no id here yet; the ledger still takes its PID so the reconcile can at
 * least say the loss was chosen rather than stand the whole pass down. */
void openmmo_pc_note_release(void *boxMon)
{
    u32 pid;

    if (boxMon == NULL)
        return;
    pid = BoxPokemon_GetValue((BoxPokemon *)boxMon, MON_DATA_PERSONALITY,
                              NULL);
    if (s_released_n < (int)(sizeof s_released_pid / sizeof s_released_pid[0]))
        s_released_pid[s_released_n++] = pid;
    printf("openmmo: box screen released pid %08x\n", (unsigned)pid);
}

/*
 * The individual the engine rolled for one monster, as the wire carries it: the personality
 * (the server reads a nature out of it, `personality % 25`), the six IVs packed five bits each
 * in stat order, and the shiny bit, gen 4 derives that from the personality and the ot id
 * rather than storing it, so it cannot survive the trip as anything but a flag.
 */
void openmmo_pc_individual(void *boxMon, u32 *seed, u32 *iv_bits, int *shiny)
{
    BoxPokemon *bm = (BoxPokemon *)boxMon;
    u32 pid, otid, bits = 0;
    int s;

    *seed = 0;
    *iv_bits = 0;
    *shiny = 0;
    if (bm == NULL)
        return;
    pid = BoxPokemon_GetValue(bm, MON_DATA_PERSONALITY, NULL);
    otid = BoxPokemon_GetValue(bm, MON_DATA_OT_ID, NULL);
    for (s = 0; s < 6; s++) {
        u32 iv = BoxPokemon_GetValue(bm, MON_DATA_HP_IV + s, NULL);

        bits |= (iv & 31) << (5 * s);
    }
    *seed = pid;
    *iv_bits = bits;
    *shiny = Pokemon_IsPersonalityShiny(otid, pid) ? 1 : 0;
}

static int already_granted(u32 pid)
{
    int i;

    for (i = 0; i < s_granted_n; i++)
        if (s_granted_pid[i] == pid)
            return 1;
    return 0;
}

static void note_granted(u32 pid, int cont)
{
    if (already_granted(pid))
        return;
    if (s_granted_n < (int)(sizeof s_granted_pid / sizeof s_granted_pid[0])) {
        s_granted_pid[s_granted_n] = pid;
        s_granted_cont[s_granted_n] = (u8)cont;
        s_granted_n++;
    }
}

/* A scripted gift is already spoken for. */
void openmmo_pc_note_script_grant(FieldSystem *fs, u32 *seed, u32 *iv_bits,
                                  int *shiny)
{
    Party *party;
    Pokemon *mon;
    int n;

    *seed = 0;
    *iv_bits = 0;
    *shiny = 0;
    if (fs == NULL || fs->saveData == NULL)
        return;
    party = SaveData_GetParty(fs->saveData);
    n = Party_GetCurrentCount(party);
    if (n <= 0)
        return;
    mon = Party_GetPokemonBySlotIndex(party, n - 1);
    if (mon == NULL)
        return;
    note_granted(Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL),
                 OPENMMO_CONTAINER_PARTY);
    /* The report that follows carries the monster the engine built, the same
     * as a capture does: one grant path, one individual. */
    openmmo_pc_individual(Pokemon_GetBoxPokemon(mon), seed, iv_bits, shiny);
}

void openmmo_pc_request_reconcile(void)
{
    s_asked = 1;
}

void openmmo_pc_track_reset_container(int cont)
{
    int i, n = 0;

    for (i = 0; i < s_track_n; i++)
        if (s_track[i].cont != (u8)cont)
            s_track[n++] = s_track[i];
    s_track_n = n;
    /* Only this container's grants are answered by this seat. A grant aimed
     * at the other container is still in flight, the party seat that opens
     * every wild fight must not forget a box catch whose PC push has not
     * landed, or the next battle's end grants the same capture again. */
    for (i = 0, n = 0; i < s_granted_n; i++) {
        if (s_granted_cont[i] != (u8)cont) {
            s_granted_pid[n] = s_granted_pid[i];
            s_granted_cont[n] = s_granted_cont[i];
            n++;
        }
    }
    s_granted_n = n;
    s_released_n = 0;
}

void openmmo_pc_track(u32 pid, s64 id, int cont, int slot)
{
    if (s_track_n >= TRACK_MAX)
        return;
    s_track[s_track_n].pid = pid;
    s_track[s_track_n].id = id;
    s_track[s_track_n].cont = (u8)cont;
    s_track[s_track_n].slot = (s16)slot;
    s_track_n++;
}

int openmmo_pc_moves_in_flight(void)
{
    return s_hold > 0;
}

int openmmo_pc_id_for_pid(u32 pid, s64 *out)
{
    int i;

    for (i = 0; i < s_track_n; i++) {
        if (s_track[i].pid == pid) {
            *out = s_track[i].id;
            return 1;
        }
    }
    return 0;
}

/* The monster boarding day care slot `slot`, or NULL for an empty slot. The
 * engine holds a boarder as a BoxPokemon inside the day care save block, the
 * same shape a box holds one, so its personality reads the same way. */
static BoxPokemon *daycare_boarder(FieldSystem *fs, int slot)
{
    Daycare *dc;
    BoxPokemon *bm;

    if (slot < 0 || slot >= NUM_DAYCARE_MONS)
        return NULL;
    dc = SaveData_GetDaycare(fs->saveData);
    if (dc == NULL)
        return NULL;
    bm = DaycareMon_GetBoxMon(Daycare_GetDaycareMon(dc, slot));
    if (bm == NULL || !BoxPokemon_GetValue(bm, MON_DATA_SPECIES_EXISTS, NULL))
        return NULL;
    return bm;
}

static const char *cont_name(int cont)
{
    switch (cont) {
    case OPENMMO_CONTAINER_PARTY:   return "party";
    case OPENMMO_CONTAINER_PC:      return "pc";
    case OPENMMO_CONTAINER_DAYCARE: return "daycare";
    }
    return "?";
}

/*
 * Build one engine Pokemon from a mirror record: the party seat's own builder
 * (openmmo_encounter.c), because a boxed monster is the same monster.
 */
extern int openmmo_seat_build_mon(Pokemon *mon, SaveData *save,
                                  const openmmo_party_mon *m);

/* Seat the engine PC from the storage mirror. Absolute, like every seat. */
static void seat_pc(FieldSystem *fs, const openmmo_storage *st)
{
    PCBoxes *pc = SaveData_GetPCBoxes(fs->saveData);
    Pokemon *mon;
    int i, n = 0, beyond = 0, unseated = 0, undrawable = 0;

    PCBoxes_Init(pc);
    openmmo_pc_track_reset_container(OPENMMO_CONTAINER_PC);
    mon = Pokemon_New(HEAP_ID_FIELD1);
    if (mon == NULL) {
        s_layout_partial = 1;
        return;
    }
    for (i = 0; i < st->count; i++) {
        const openmmo_party_mon *m = &st->mon[i];

        /*
         * The id map found no engine species for this one (idmap.h: 0 means untranslatable,
         * never an empty record, the mirror holds only monsters the server sent).
         */
        if (m->species == 0) {
            undrawable++;
            continue;
        }
        /*
         * The server's PC and the engine's eighteen boxes are both 540 slots now
         * (PC_STORAGE_SIZE), so nothing should land here.
         */
        if (m->slot < 0 || m->slot >= PC_ENGINE_SLOTS) {
            beyond++;
            continue;
        }
        if (!openmmo_seat_build_mon(mon, fs->saveData, m))
            continue;
        if (!PCBoxes_TryStoreBoxMonAt(pc, (u32)(m->slot / MAX_MONS_PER_BOX),
                                      (u32)(m->slot % MAX_MONS_PER_BOX),
                                      Pokemon_GetBoxPokemon(mon))) {
            unseated++;
            continue;
        }
        openmmo_pc_track(Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL),
                         m->id, OPENMMO_CONTAINER_PC, m->slot);
        n++;
    }
    Heap_Free(mon);
    /*
     * A record that belonged in a box and did not get one leaves this side unable to say where
     * things are: the destination of a move could then be occupied on the server and empty
     * here, and the swap would displace a monster nobody can see.
     */
    s_layout_partial = unseated != 0 || undrawable != 0;
    printf("openmmo: pc seated %d mon(s)%s%s%s\n", n,
           beyond ? " (some slots past the engine's boxes)" : "",
           unseated ? " (some could not be seated)" : "",
           undrawable ? " (some are species this client cannot draw)" : "");
}

/* Bind the server's boarders to the slots the engine is holding them in. */
static void bind_daycare(FieldSystem *fs, const openmmo_storage *dc)
{
    int i, bound = 0, unmatched = 0;

    s_daycare_unbound = 0;

    openmmo_pc_track_reset_container(OPENMMO_CONTAINER_DAYCARE);
    for (i = 0; i < dc->count && dc->mon != NULL; i++) {
        const openmmo_party_mon *m = &dc->mon[i];
        BoxPokemon *bm;

        if (m->slot < 0 || m->slot >= NUM_DAYCARE_MONS) {
            unmatched++;
            continue;
        }
        bm = daycare_boarder(fs, m->slot);
        if (bm == NULL) {
            unmatched++;
            continue;
        }
        openmmo_pc_track(BoxPokemon_GetValue(bm, MON_DATA_PERSONALITY, NULL),
                         m->id, OPENMMO_CONTAINER_DAYCARE, m->slot);
        bound++;
    }
    s_daycare_unbound = unmatched;
    printf("openmmo: daycare bound %d boarder(s)%s\n", bound,
           unmatched ? " (the day care block does not hold them all yet)" : "");
}

/*
 * A measurement seam, off unless asked for: hand party slot N over the day care counter, or
 * take day care slot N back, once, the first time the field is settled.
 */
static void daycare_station(FieldSystem *fs)
{
    static int done;
    const char *board, *collect;
    Daycare *dc;
    Party *party;

    if (done)
        return;
    board = openmmo_dev_env("OPENMMO_DAYCARE_BOARD");
    collect = openmmo_dev_env("OPENMMO_DAYCARE_COLLECT");
    if ((board == NULL || board[0] == '\0')
        && (collect == NULL || collect[0] == '\0'))
        return;
    done = 1;
    dc = SaveData_GetDaycare(fs->saveData);
    party = SaveData_GetParty(fs->saveData);
    if (dc == NULL || party == NULL)
        return;
    if (board != NULL && board[0] != '\0') {
        int slot = atoi(board);

        if (slot < 0 || slot >= Party_GetCurrentCount(party)) {
            printf("openmmo: daycare station: party slot %d is empty\n", slot);
            return;
        }
        Daycare_MoveToEmptySlotFromParty(party, slot, dc, fs->saveData);
        printf("openmmo: daycare station: handed party slot %d over the"
               " counter\n", slot);
        /* The lady's own path opens the party menu to choose with, and a menu
         * closing is what arms the reconcile. This station opens none, so it
         * says so itself, otherwise the deposit sits unreported and the next
         * party seat quietly puts the boarder back in the party as well. */
        openmmo_pc_request_reconcile();
        return;
    }
    {
        int slot = atoi(collect);
        StringTemplate *tmpl;

        if (daycare_boarder(fs, slot) == NULL) {
            printf("openmmo: daycare station: day care slot %d is empty\n",
                   slot);
            return;
        }
        /*
         * A template of this station's own. The lady's path borrows the running script's
         * (FieldSystem_GetScriptMemberPtr), and there is no script here, asking for it
         * dereferences a null field task and takes the process down.
         */
        tmpl = StringTemplate_New(1, MON_NAME_LEN + 1, HEAP_ID_FIELD2);
        if (tmpl == NULL) {
            printf("openmmo: daycare station: no room for a string"
                   " template\n");
            return;
        }
        Daycare_MoveToPartyFromDaycareSlot(party, tmpl, dc, (u8)slot);
        StringTemplate_Free(tmpl);
        printf("openmmo: daycare station: took day care slot %d back\n", slot);
        openmmo_pc_request_reconcile();
    }
}

void openmmo_pc_field_sync(FieldSystem *fs, const openmmo_client *c)
{
    const openmmo_storage *st;

    if (fs == NULL || fs->saveData == NULL || c == NULL)
        return;
    if (s_hold > 0)
        return;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return;
    {
        const openmmo_storage *dc = openmmo_client_daycare(c);
        long dsum = 0;
        int i;

        if (dc != NULL && dc->valid) {
            for (i = 0; i < dc->count && dc->mon != NULL; i++)
                dsum += (long)(dc->mon[i].id * (i + 1) + dc->mon[i].slot * 31);
            for (i = 0; i < NUM_DAYCARE_MONS; i++) {
                BoxPokemon *bm = daycare_boarder(fs, i);

                if (bm != NULL)
                    dsum += (long)BoxPokemon_GetValue(bm, MON_DATA_PERSONALITY,
                                                      NULL) * (i + 3);
            }
            if (!s_daycare_valid || s_daycare_sum != dsum) {
                s_daycare_sum = dsum;
                s_daycare_valid = 1;
                bind_daycare(fs, dc);
            }
            /*
             * After the bind and never before it: the station is standing in for a walk to
             * Solaceon and a word with the lady, and by the time a player has done that the
             * boarders have long been placed.
             */
            daycare_station(fs);
        }
    }
    st = openmmo_client_storage(c);
    if (st == NULL || !st->valid)
        return;
    {
        s64 sum_id = 0;
        long sum_state = 0;
        int i;

        for (i = 0; i < st->count && st->mon != NULL; i++) {
            sum_id += st->mon[i].id * (i + 1);
            sum_state += st->mon[i].slot * 7 + st->mon[i].xp + st->mon[i].hp;
        }
        if (s_last_valid && s_last.count == st->count
            && s_last.total == st->total && s_last.sum_id == sum_id
            && s_last.sum_state == sum_state)
            return;
        s_last.count = st->count;
        s_last.total = st->total;
        s_last.sum_id = sum_id;
        s_last.sum_state = sum_state;
        s_last_valid = 1;
    }
    seat_pc(fs, st);
}

/* Where a tracked PID sits now, or -1. */
static int find_now(FieldSystem *fs, u32 pid, int *out_cont, int *out_slot)
{
    Party *party = SaveData_GetParty(fs->saveData);
    PCBoxes *pc = SaveData_GetPCBoxes(fs->saveData);
    int i, n = Party_GetCurrentCount(party);

    for (i = 0; i < NUM_DAYCARE_MONS; i++) {
        BoxPokemon *bm = daycare_boarder(fs, i);

        if (bm != NULL
            && BoxPokemon_GetValue(bm, MON_DATA_PERSONALITY, NULL) == pid) {
            *out_cont = OPENMMO_CONTAINER_DAYCARE;
            *out_slot = i;
            return 1;
        }
    }
    for (i = 0; i < n; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

        if (mon != NULL
            && Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL) == pid) {
            *out_cont = OPENMMO_CONTAINER_PARTY;
            *out_slot = i;
            return 1;
        }
    }
    for (i = 0; i < PC_ENGINE_SLOTS; i++) {
        BoxPokemon *bm = PCBoxes_GetBoxMonAt(pc, (u32)(i / MAX_MONS_PER_BOX),
                                             (u32)(i % MAX_MONS_PER_BOX));
        u32 exists;

        if (bm == NULL)
            continue;
        exists = BoxPokemon_GetValue(bm, MON_DATA_SPECIES_EXISTS, NULL);
        if (!exists)
            continue;
        if (BoxPokemon_GetValue(bm, MON_DATA_PERSONALITY, NULL) == pid) {
            *out_cont = OPENMMO_CONTAINER_PC;
            *out_slot = i;
            return 1;
        }
    }
    return 0;
}

/* Everything the engine is actually holding, printed when a pass stands down.
 * A belief that a monster left the containers is only ever worth a message,
 * so the message has to carry enough to say which of the two sides is wrong. */
static void print_layout(FieldSystem *fs)
{
    Party *party = SaveData_GetParty(fs->saveData);
    PCBoxes *pc = SaveData_GetPCBoxes(fs->saveData);
    int i, n = Party_GetCurrentCount(party);

    for (i = 0; i < n; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

        if (mon == NULL)
            continue;
        printf("openmmo:   engine party:%d pid %08x species %d\n", i,
               (unsigned)Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL),
               (int)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL));
    }
    for (i = 0; i < PC_ENGINE_SLOTS; i++) {
        BoxPokemon *bm = PCBoxes_GetBoxMonAt(pc, (u32)(i / MAX_MONS_PER_BOX),
                                             (u32)(i % MAX_MONS_PER_BOX));

        if (bm == NULL || !BoxPokemon_GetValue(bm, MON_DATA_SPECIES_EXISTS, NULL))
            continue;
        printf("openmmo:   engine pc:%d pid %08x species %d\n", i,
               (unsigned)BoxPokemon_GetValue(bm, MON_DATA_PERSONALITY, NULL),
               (int)BoxPokemon_GetValue(bm, MON_DATA_SPECIES, NULL));
    }
    for (i = 0; i < NUM_DAYCARE_MONS; i++) {
        BoxPokemon *bm = daycare_boarder(fs, i);

        if (bm == NULL)
            continue;
        printf("openmmo:   engine daycare:%d pid %08x species %d\n", i,
               (unsigned)BoxPokemon_GetValue(bm, MON_DATA_PERSONALITY, NULL),
               (int)BoxPokemon_GetValue(bm, MON_DATA_SPECIES, NULL));
    }
    for (i = 0; i < s_track_n; i++)
        printf("openmmo:   tracked id %lld pid %08x at %s:%d\n",
               (long long)s_track[i].id, (unsigned)s_track[i].pid,
               cont_name(s_track[i].cont), s_track[i].slot);
}

extern void openmmo_party_rebind(FieldSystem *fs); /* openmmo_encounter.c */

/* After any menu closes, re-derive the layout and report what moved. */
void openmmo_pc_reconcile_tick(FieldSystem *fs, openmmo_client *c)
{
    int i, j, sent = 0, lost = 0, over = 0, released = 0;
    int cont, slot;

    if (fs == NULL || fs->saveData == NULL || c == NULL)
        return;
    if (s_hold > 0)
        s_hold--;
    if (FieldSystem_HasChildProcess(fs)) {
        s_saw_child = 1;
        return;
    }
    if (!s_saw_child && !s_asked)
        return;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs))
        return;
    s_saw_child = 0;
    s_asked = 0;

    /* Phase one: read the whole layout before a word of it goes out. */
    for (i = 0; i < s_track_n; i++) {
        if (find_now(fs, s_track[i].pid, &cont, &slot)) {
            s_now_cont[i] = (u8)cont;
            s_now_slot[i] = (s16)slot;
            continue;
        }
        s_now_cont[i] = CONT_LOST;
        lost++;
    }

    /* A loss the box screen owns up to is a release: said on the wire, then
     * dropped from the table so the rest of the pass reasons over who is
     * left. A send that fails leaves the entry lost, and the stand-down
     * below puts the server's copy back on screen. */
    for (i = 0; i < s_track_n;) {
        if (s_now_cont[i] == CONT_LOST && take_released(s_track[i].pid)) {
            if (openmmo_client_send_pokemon_release(c, s_track[i].id) == 0) {
                printf("openmmo: released monster id %lld\n",
                       (long long)s_track[i].id);
                released++;
                lost--;
                s_track_n--;
                s_track[i] = s_track[s_track_n];
                s_now_cont[i] = s_now_cont[s_track_n];
                s_now_slot[i] = s_now_slot[s_track_n];
                continue;
            }
            printf("openmmo: could not send release of monster id %lld\n",
                   (long long)s_track[i].id);
        }
        i++;
    }

    if (lost || s_layout_partial) {
        /*
         * Not a release: the screen never owned up to this one. A monster the scan cannot see
         * is a belief with nothing behind it, and acting on that guess is what let go of a
         * Shinx its player still had.
         */
        for (i = 0; i < s_track_n; i++)
            if (s_now_cont[i] == CONT_LOST)
                printf("openmmo: box sync lost monster id %lld, last seen at"
                       " %s:%d\n", (long long)s_track[i].id,
                       cont_name(s_track[i].cont), s_track[i].slot);
        printf("openmmo: box sync stands down (%d lost%s), reseating from the"
               " server\n", lost,
               s_layout_partial ? ", last seat incomplete" : "");
        print_layout(fs);
        s_last_valid = 0;
    } else {
        /*
         * Phase two: the difference as the server's own gesture. Each pair puts one monster on
         * its place and sends whoever was there back to the slot the mover came from, which is
         * the swap the screen drew rather than two moves that undo each other.
         */
        mmo_pokemon_move batch[MMO_POKEMON_MOVE_MAX];
        s64 batch_id[MMO_POKEMON_MOVE_MAX];
        int n = 0;

        for (i = 0; i < s_track_n; i++) {
            s_model_cont[i] = s_track[i].cont;
            s_model_slot[i] = s_track[i].slot;
        }
        for (i = 0; i < s_track_n; i++) {
            int fc, fslot;

            if (s_model_cont[i] == s_now_cont[i]
                && s_model_slot[i] == s_now_slot[i])
                continue;
            if (n == MMO_POKEMON_MOVE_MAX) {
                over = 1;
                break;
            }
            fc = s_model_cont[i];
            fslot = s_model_slot[i];
            for (j = i + 1; j < s_track_n; j++) {
                if (s_model_cont[j] == s_now_cont[i]
                    && s_model_slot[j] == s_now_slot[i]) {
                    s_model_cont[j] = (u8)fc;
                    s_model_slot[j] = (s16)fslot;
                    break;
                }
            }
            s_model_cont[i] = s_now_cont[i];
            s_model_slot[i] = s_now_slot[i];
            batch[n].from_container = (u8)fc;
            batch[n].from_slot = (s16)fslot;
            batch[n].to_container = s_now_cont[i];
            batch[n].to_slot = s_now_slot[i];
            batch_id[n] = s_track[i].id;
            n++;
        }
        if (over) {
            /* One packet is the whole gesture or none of it: a half-sent
             * shuffle leaves the screen showing an arrangement the server
             * never agreed to. */
            printf("openmmo: box sync stands down, more than %d pairs to"
                   " send; reseating from the server\n",
                   MMO_POKEMON_MOVE_MAX);
            s_last_valid = 0;
        } else if (n > 0) {
            if (openmmo_client_move_pokemon_batch(c, batch, n) != 0) {
                printf("openmmo: box sync could not send %d pair(s),"
                       " reseating from the server\n", n);
                s_last_valid = 0;
            } else {
                /* The model is the layout only now that the server has been
                 * told it. */
                for (i = 0; i < s_track_n; i++) {
                    s_track[i].cont = s_model_cont[i];
                    s_track[i].slot = s_model_slot[i];
                }
                for (i = 0; i < n; i++)
                    printf("openmmo: moved monster id %lld %s:%d -> %s:%d\n",
                           (long long)batch_id[i],
                           cont_name(batch[i].from_container),
                           batch[i].from_slot,
                           cont_name(batch[i].to_container),
                           batch[i].to_slot);
                sent = n;
            }
        }
    }
    /*
     * Anything standing in a container this table does not know is a monster the server has no
     * record of, a capture, party or forced to a box.
     */
    {
        extern int openmmo_trade_scene_up(void);
        const openmmo_party *mirror = openmmo_client_party(c);
        int held = 0;

        if (openmmo_trade_scene_up() || openmmo_client_trade(c)->open)
            held = 1;
        /* And while the day care and its block disagree. */
        if (!held && s_daycare_unbound)
            held = 1;
        if (!held && mirror != NULL && mirror->valid) {
            for (i = 0; i < mirror->count && !held; i++) {
                int t, known = 0;

                if (mirror->mon[i].species == 0 || mirror->mon[i].egg)
                    continue;
                for (t = 0; t < s_track_n; t++)
                    if (s_track[t].id == mirror->mon[i].id) {
                        known = 1;
                        break;
                    }
                if (!known)
                    held = 1;
            }
        }
        if (held) {
            printf("openmmo: box sync holds its grants, %s\n",
                   s_daycare_unbound
                       ? "the day care and its block do not agree"
                       : "an incoming monster has not been seated yet");
            s_asked = 1;
            goto tail;
        }
    }
    {
        extern void openmmo_script_grant_report(int species, int level, int hp,
                                                int container, int slot,
                                                u32 seed, u32 iv_bits,
                                                int shiny,
                                                const void *nick_charcodes);
        Party *party = SaveData_GetParty(fs->saveData);
        PCBoxes *pc = SaveData_GetPCBoxes(fs->saveData);
        int n = Party_GetCurrentCount(party);
        int granted = 0;

        for (i = 0; i < n + PC_ENGINE_SLOTS; i++) {
            int in_party = i < n;
            int slot_now = in_party ? i : i - n;
            charcode_t nick[MON_NAME_LEN + 4];
            u32 pid, species, level, hp, seed, iv_bits;
            int shiny;
            s64 dummy;
            BoxPokemon *bm;
            Pokemon *pmon = NULL;

            if (in_party) {
                pmon = Party_GetPokemonBySlotIndex(party, slot_now);
                if (pmon == NULL)
                    continue;
                bm = Pokemon_GetBoxPokemon(pmon);
            } else {
                bm = PCBoxes_GetBoxMonAt(pc, (u32)(slot_now / MAX_MONS_PER_BOX),
                                         (u32)(slot_now % MAX_MONS_PER_BOX));
                if (bm == NULL || !BoxPokemon_GetValue(bm, MON_DATA_SPECIES_EXISTS, NULL))
                    continue;
            }
            pid = BoxPokemon_GetValue(bm, MON_DATA_PERSONALITY, NULL);
            if (openmmo_pc_id_for_pid(pid, &dummy))
                continue;
            if (already_granted(pid))
                continue;
            /* The server's number for it, not the engine's (species_port.h). */
            species = mmo_species_port_wire_id((int)BoxPokemon_GetValue(bm, MON_DATA_SPECIES, NULL));
            level = BoxPokemon_GetLevel(bm);
            hp = pmon != NULL ? Pokemon_GetValue(pmon, MON_DATA_HP, NULL) : 0;
            BoxPokemon_GetValue(bm, MON_DATA_NICKNAME, nick);
            /* The monster the ball caught, not a species and a level for the
             * server to roll a second one from: its nature, its IVs and its
             * shiny bit travel with the report. */
            openmmo_pc_individual(bm, &seed, &iv_bits, &shiny);
            openmmo_script_grant_report((int)species, (int)level,
                                        pmon != NULL ? (int)hp : -1,
                                        in_party ? OPENMMO_CONTAINER_PARTY
                                                 : OPENMMO_CONTAINER_PC,
                                        slot_now, seed, iv_bits, shiny, nick);
            note_granted(pid, in_party ? OPENMMO_CONTAINER_PARTY
                                       : OPENMMO_CONTAINER_PC);
            granted++;
        }
        if (granted)
            sent++;
    }

tail:
    /* Only a send earns the hold: a pass that stood down wants the reseat it
     * just asked for to happen on the next tick, not three seconds later. */
    if (sent || released)
        s_hold = 180; /* let the answering containers land before any reseat */
    if (sent || released || lost || over)
        openmmo_party_rebind(fs);
}
