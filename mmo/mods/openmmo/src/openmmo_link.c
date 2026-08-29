/* PvP as the engine's own link battle, with this server as the wire. */

#include <stdio.h>
#include <string.h>

#include "constants/battle.h"
#include "constants/heap.h"
#include "encounter.h"
#include "field/field_system.h"
#include "field_battle_data_transfer.h"
#include "field_system.h"
#include "charcode_util.h"
#include "heap.h"
#include "party.h"
#include "pokemon.h"
#include "save_player.h"
#include "savedata.h"
#include "struct_defs/trainer.h"
#include "trainer_info.h"

#include "../../../include/charcode.h"
#include "../../../include/client.h"
#include "../../../include/idmap.h"

/* encounter.c, via mods/openmmo/patches: the link-battle door, minus the party
 * order menu and the FieldTask a script would have supplied. */
extern void openmmo_encounter_new_vs_link(FieldSystem *fs, FieldBattleDTO *dto);

/* unk_0207A6DC.c, via mods/openmmo/patches: the cmd-23 handler by another
 * name, so a delivered blob lands in the engine's own client ring. */
extern void openmmo_link_battle_deliver(void *battleSys, const void *data, int size);

/* unk_0207A6DC.c, via mods/openmmo/patches: what the DS does when the link
 * goes away mid-fight, stop both transport tasks and tell the battle to end
 * its wait. Without it a peer that walks out leaves the other player inside a
 * scene waiting for messages that are not coming. */
extern void openmmo_link_battle_abort(void *battleSys);

/* One queued blob. The width is the engine's own ceiling: a BattleMessageInfo
 * (4 bytes) plus a body whose size rides in a u8. */
#define LINK_MSG_MAX  (4 + 255)
#define LINK_QUEUE    32

static struct {
    int len;
    u8  data[LINK_MSG_MAX];
} s_in[LINK_QUEUE];
static int s_in_head;
static int s_in_count;
static int s_in_dropped;

static int s_active;        /* a link battle is seated */
static int s_net_id;        /* 0 computes the fight, 1 presents it */
static int s_scene;         /* the encounter task is up */
static int s_saw_child;
static int s_result;        /* the engine's result mask, latched */
static int s_reported;
static openmmo_client *s_client;
static FieldBattleDTO *s_dto;
static void *s_battle_sys;

/* CommTiming, as the two of us. The DS semantics are: my number is broadcast,
 * and the barrier is "state" once every system has reported the same one,
 * and it stays that number afterwards, which is why the same tag can be
 * stepped through twice (the vs intro and the result screen both use 62). */
static int s_sync_mine = -1;
static int s_sync_peer = -1;
static int s_sync_state = -1;
static int s_peer_gone;

/* ------------------------------------------------------------------ *
 * What the engine asks
 * ------------------------------------------------------------------ */

int openmmo_link_battle_active(void)
{
    return s_active;
}

/* < 0 when no link battle is up, so CommSys_CurNetId keeps its own answer. */
int openmmo_link_battle_net_id(void)
{
    return s_active ? s_net_id : -1;
}

/* 0 when no link battle is up. Two, otherwise: the engine counts *clients*,
 * and both of them are one. */
int openmmo_link_battle_connected(void)
{
    return s_active ? 2 : 0;
}

void openmmo_link_battle_attach(void *battleSys)
{
    s_battle_sys = battleSys;
}

static void link_push(const void *data, int size)
{
    int slot;

    if (size <= 0 || size > LINK_MSG_MAX) {
        printf("openmmo: link battle refused a %d-byte message\n", size);
        s_in_dropped++;
        return;
    }
    if (s_in_count >= LINK_QUEUE) {
        /* The presenter has stopped draining. Saying so is the whole of what
         * can be done: the fight the two halves are in is no longer the same
         * fight, and a silently dropped message would look like a fight that
         * simply went quiet. */
        s_in_dropped++;
        return;
    }
    slot = (s_in_head + s_in_count) % LINK_QUEUE;
    s_in[slot].len = size;
    memcpy(s_in[slot].data, data, (size_t)size);
    s_in_count++;
}

/* The pipe. Returns 1 when the engine may advance its read cursor, which is
 * the contract CommSys_SendData had. */
int openmmo_link_battle_send(const void *data, int size)
{
    if (!s_active || data == NULL || size <= 0)
        return 0;
    if (size > LINK_MSG_MAX) {
        printf("openmmo: link battle will not send a %d-byte message\n", size);
        return 0;
    }
    /* Our own copy first, so this engine's stream keeps its order regardless
     * of what the socket does with the peer's. */
    link_push(data, size);
    if (s_client != NULL)
        openmmo_client_link_send(s_client, MMO_LINK_KIND_MESSAGE, data, size);
    return 1;
}

/* Called from the engine's own client-ring drain, and only when that ring is
 * empty: one message at a time is the rate the DS transport worked at and the
 * rate the reader consumes at, so the ring never has to hold a backlog. */
void openmmo_link_battle_pump(void *battleSys)
{
    int slot;

    if (!s_active || s_in_count <= 0 || battleSys == NULL)
        return;
    slot = s_in_head;
    s_in_head = (s_in_head + 1) % LINK_QUEUE;
    s_in_count--;
    openmmo_link_battle_deliver(battleSys, s_in[slot].data, s_in[slot].len);
}

/*
 * The fight is over, from the side that computed it. On a DS this is comm command 22,
 * broadcast to every system including the sender (loopback), and each system's handler ends
 * its main loop.
 */
int openmmo_link_battle_end_fight(void *battleSys)
{
    if (!s_active)
        return 0;
    if (!s_peer_gone && s_client != NULL)
        openmmo_client_link_send(s_client, MMO_LINK_KIND_ENDWAIT, NULL, 0);
    openmmo_link_battle_abort(battleSys);
    /*
     * The fight is over, so the transport is: nothing sends or delivers past this point, and
     * the BattleSystem dies with the application while the result screen is still up.
     */
    s_battle_sys = NULL;
    return 1;
}

/* Returns 1 when this barrier is ours, so the engine's own CommTiming stays
 * out of it (its state block is not allocated in this port and touching it
 * would be a null dereference, not a fallback). */
int openmmo_link_sync_start(int syncNo)
{
    u8 tag;

    /* The trade scene's barriers are the pipe's (openmmo_trade.c). */
    {
        extern int openmmo_trade_sync_start(int syncNo);

        if (openmmo_trade_sync_start(syncNo))
            return 1;
    }
    if (!s_active)
        return 0;
    s_sync_mine = syncNo;
    /* A barrier with one participant left is a barrier already passed. */
    if (s_peer_gone || s_sync_mine == s_sync_peer)
        s_sync_state = syncNo;
    tag = (u8)syncNo;
    if (s_client != NULL)
        openmmo_client_link_send(s_client, MMO_LINK_KIND_SYNC, &tag, 1);
    return 1;
}

/* 1 / 0 when this barrier is ours, -1 when it is not. */
int openmmo_link_sync_state(int syncState)
{
    {
        extern int openmmo_trade_sync_state(int syncState);
        int tradeSync = openmmo_trade_sync_state(syncState);

        if (tradeSync >= 0)
            return tradeSync;
    }
    if (!s_active)
        return -1;
    if (s_peer_gone)
        return 1;
    return s_sync_state == syncState ? 1 : 0;
}

/* ------------------------------------------------------------------ *
 * Seating a monster the other client will seat identically
 * ------------------------------------------------------------------ */

/* A 64-bit id folded to 32 bits. Any stable mixing does; this one is
 * splitmix64's finaliser, which spreads consecutive ids (the server hands them
 * out in sequence) across the whole range so the derived nature bend and the
 * derived shiny bend do not correlate with the order monsters were caught in. */
static u32 link_mix(s64 id, u32 salt)
{
    u64 z = (u64)id + 0x9E3779B97F4A7C15ULL + (u64)salt * 0xBF58476D1CE4E5B9ULL;

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (u32)(z ^ (z >> 32));
}

/* Build one battle monster from one server record, with nothing rolled.
 *
 * `species` has already crossed the id map; a record the map cannot name is
 * refused by the caller rather than drawn as some other monster. */
/*
 * The personality one server record gets: derived from the record's own id, then bent to the
 * nature the server holds, because Gen 4 reads the nature out of the personality (`personality
 * % 25`) and the server's nature is the one the summary screen and the stat calculation have
 * to agree on.
 */
u32 openmmo_mon_personality(s64 id, int nature)
{
    u32 pid = link_mix(id, 1);

    if (nature < 0 || nature >= 25)
        nature = 0;
    return pid - (pid % 25) + (u32)nature;
}

static int link_build_mon(Pokemon *mon, const openmmo_party_mon *m)
{
    u32 pid, otid, otlo, x;
    int i;

    if (m->species == 0 || m->egg)
        return 0;

    pid = openmmo_mon_personality(m->id, m->nature);

    /*
     * Ot id: derived, then bent to the shiny bit. Gen 4 has no shiny flag, a monster is
     * shiny when the two halves of the ot id and the two halves of the personality xor below 8,
     * so the record's flag is expressed by choosing the high half.
     */
    otlo = link_mix(m->id, 2) & 0xFFFF;
    x = otlo ^ (pid >> 16) ^ (pid & 0xFFFF);
    otid = ((m->shiny ? x : (x ^ 0x8000u)) << 16) | otlo;

    Pokemon_Init(mon);
    Pokemon_InitWith(mon, m->species,
                     m->level < 1 ? 1 : (m->level > 100 ? 100 : m->level),
                     INIT_IVS_RANDOM, TRUE, pid, OTID_SET, otid);

    /* Experience over the level, so a monster sits where its record sits
     * rather than at the bottom of its level. */
    if (m->xp > 0) {
        u32 exp = (u32)m->xp;
        Pokemon_SetValue(mon, MON_DATA_EXPERIENCE, &exp);
    }
    for (i = 0; i < 6; i++) {
        u32 iv = m->iv[i] > 31 ? 31 : m->iv[i];
        u32 ev = m->ev[i];

        Pokemon_SetValue(mon, MON_DATA_HP_IV + i, &iv);
        Pokemon_SetValue(mon, MON_DATA_HP_EV + i, &ev);
    }
    {
        u32 friendship = (u32)(m->friendship < 0 ? 0
                               : (m->friendship > 255 ? 255 : m->friendship));
        Pokemon_SetValue(mon, MON_DATA_FRIENDSHIP, &friendship);
    }
    if (m->form > 0) {
        u32 form = (u32)m->form;
        Pokemon_SetValue(mon, MON_DATA_FORM, &form);
    }
    /* Ability. Slot 2 is the hidden ability, which Gen 4 has no concept of;
     * the record already says the client cannot draw it, so the monster fights
     * on its first ordinary one and the mismatch is named out loud rather than
     * silently becoming the second. */
    {
        u32 first = SpeciesData_GetSpeciesValue(m->species, SPECIES_DATA_ABILITY_1);
        u32 second = SpeciesData_GetSpeciesValue(m->species, SPECIES_DATA_ABILITY_2);
        u32 want = first;

        if (m->ability_slot == 1 && second != 0)
            want = second;
        else if (m->ability_unrenderable)
            printf("openmmo: link battle: %s has a hidden ability the engine"
                   " cannot draw; fighting on its first\n",
                   m->nickname[0] ? m->nickname : "a monster");
        Pokemon_SetValue(mon, MON_DATA_ABILITY, &want);
    }
    for (i = 0; i < 4; i++) {
        if (m->move[i] == 0)
            continue;
        Pokemon_ResetMoveSlot(mon, m->move[i], (u8)i);
        if (m->move_pp[i] != 0) {
            u8 pp = m->move_pp[i];
            Pokemon_SetValue(mon, MON_DATA_MOVE1_PP + i, &pp);
        }
    }
    /* The name its trainer typed, and the trainer who typed it. Both are on
     * the wire in both directions, so both clients write the same two. */
    if (m->nickname[0] != '\0') {
        mmo_charcode nick[MON_NAME_LEN + 4];
        size_t k;

        for (k = 0; k < (size_t)(MON_NAME_LEN + 4); k++)
            nick[k] = MMO_CHAR_EOS;
        mmo_utf8_to_charcode(m->nickname, nick, MON_NAME_LEN + 1);
        Pokemon_SetValue(mon, MON_DATA_NICKNAME_AND_FLAG, nick);
    }
    if (m->ot[0] != '\0') {
        mmo_charcode ot[TRAINER_NAME_LEN + 4];
        size_t k;

        for (k = 0; k < (size_t)(TRAINER_NAME_LEN + 4); k++)
            ot[k] = MMO_CHAR_EOS;
        mmo_utf8_to_charcode(m->ot, ot, TRAINER_NAME_LEN + 1);
        Pokemon_SetValue(mon, MON_DATA_OT_NAME, ot);
    }

    Pokemon_CalcLevelAndStats(mon);

    /* HP last: the stat calculation writes a full bar, and the record's own
     * current HP is what the fight starts from. */
    {
        u32 maxhp = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL);
        int want = m->hp;
        u16 hp;

        if (want < 0)
            want = (int)maxhp;
        if (want > (int)maxhp)
            want = (int)maxhp;
        hp = (u16)want;
        Pokemon_SetValue(mon, MON_DATA_HP, &hp);
    }
    return 1;
}

/* Fill one battler's party. Returns how many monsters it holds. */
static int link_seat_party(Party *party, const openmmo_party *p)
{
    Pokemon *mon;
    int i, n = 0;

    Party_Init(party);
    if (p == NULL || !p->valid)
        return 0;
    mon = Pokemon_New(HEAP_ID_FIELD2);
    if (mon == NULL)
        return 0;
    for (i = 0; i < p->count && n < MAX_PARTY_SIZE; i++) {
        if (!link_build_mon(mon, &p->mon[i]))
            continue;
        if (!Party_AddPokemon(party, mon))
            break;
        n++;
    }
    Heap_Free(mon);
    return n;
}

/* The opponent's TrainerInfo. A link battle carries one over the wire; here
 * the server named the peer and the rest is a copy of ours, so the screens
 * that ask for a badge count or a language get answers of the right shape. */
static void link_seat_trainer(TrainerInfo *dst, const TrainerInfo *mine,
                              const char *name, int gender, s64 seed)
{
    mmo_charcode buf[TRAINER_NAME_LEN + 4];
    size_t k;

    /* Ours as the template, because everything a battle screen asks a
     * TrainerInfo that is not the name, the gender or the id, language,
     * game code, badge mask, has to be an answer of the right shape and the
     * wire carries none of it. */
    TrainerInfo_Copy(mine, dst);
    for (k = 0; k < (size_t)(TRAINER_NAME_LEN + 4); k++)
        buf[k] = MMO_CHAR_EOS;
    mmo_utf8_to_charcode(name != NULL ? name : "", buf, TRAINER_NAME_LEN + 1);
    TrainerInfo_SetName(dst, buf);
    TrainerInfo_SetID(dst, link_mix(seed, 3));
    TrainerInfo_SetGender(dst, gender ? 1 : 0);
}

/* ------------------------------------------------------------------ *
 * The scene
 * ------------------------------------------------------------------ */

static void link_reset(void)
{
    s_active = 0;
    s_net_id = 0;
    s_scene = 0;
    s_saw_child = 0;
    s_result = BATTLE_IN_PROGRESS;
    s_reported = 0;
    s_dto = NULL;
    s_battle_sys = NULL;
    s_in_head = 0;
    s_in_count = 0;
    s_in_dropped = 0;
    s_sync_mine = -1;
    s_sync_peer = -1;
    s_sync_state = -1;
    s_peer_gone = 0;
}

/* The encounter task is up: the guest owns both screens (the battle draws
 * on both), and the window's panels stand aside while it does. */
int openmmo_link_scene_up(void)
{
    return s_scene;
}

/* The server seated a link battle. Build both parties and open the engine's
 * own link encounter. Returns 0 when the scene started. */
int openmmo_link_battle_open(FieldSystem *fs, openmmo_client *c)
{
    const openmmo_link_battle *lb;
    const openmmo_party *mine;
    FieldBattleDTO *dto;
    TrainerInfo *save_trainer;
    int me, them, n_mine, n_theirs;

    if (fs == NULL || fs->saveData == NULL || c == NULL)
        return -1;
    lb = openmmo_client_link_battle(c);
    if (lb == NULL || !lb->valid)
        return -1;
    if (s_scene)
        return -1;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs)) {
        /* The field is mid-anything. The seat stays live and the poll tries
         * again next frame rather than opening a scene over a transition. */
        return -1;
    }
    mine = openmmo_client_party(c);
    if (mine == NULL || !mine->valid) {
        printf("openmmo: link battle needs a party this client holds\n");
        return -1;
    }

    link_reset();
    s_client = c;
    s_net_id = lb->net_id != 0 ? 1 : 0;
    /* Live from here on: FieldBattleDTO_New asks CommSys which net id we are
     * and how many of us there are, and both answers are now ours. */
    s_active = 1;

    dto = FieldBattleDTO_New(HEAP_ID_FIELD2, BATTLE_TYPE_LINK | BATTLE_TYPE_TRAINER);
    if (dto == NULL) {
        link_reset();
        return -1;
    }
    FieldBattleDTO_Init(dto, fs);
    dto->networkID = (u16)s_net_id;
    dto->linkPlayerPositions[0] = 0;
    dto->linkPlayerPositions[1] = 1;
    /* The link battle room, which is what the official client draws for one. */
    dto->background = BACKGROUND_INDOORS_1;
    dto->terrain = TERRAIN_BUILDING;

    /* The parties are indexed by NET ID, not by us-and-them: the engine's own
     * transport writes an arriving party into dto->parties[sender], and
     * BattleSys_New reads dto->parties[i] as net id i's. */
    me = s_net_id;
    them = s_net_id ^ 1;
    n_mine = link_seat_party(dto->parties[me], mine);
    n_theirs = link_seat_party(dto->parties[them], &lb->party);
    if (n_mine < 1 || n_theirs < 1) {
        printf("openmmo: link battle needs a party the engine can draw"
               " (ours %d, theirs %d)\n", n_mine, n_theirs);
        FieldBattleDTO_Free(dto);
        link_reset();
        return -1;
    }

    save_trainer = SaveData_GetTrainerInfo(fs->saveData);
    FieldBattleDTO_CopyTrainerInfoToBattler(dto, save_trainer, me);
    link_seat_trainer(dto->trainerInfo[them], save_trainer, lb->peer_name,
                      lb->peer_gender, (s64)lb->battle_id * 2 + 1);
    /* The Trainer record beside each TrainerInfo. */
    {
        int i;

        for (i = 0; i < 2; i++) {
            int side = i == 0 ? me : them;

            memset(&dto->trainer[side], 0, sizeof dto->trainer[side]);
            dto->trainer[side].header.trainerType =
                (u8)TrainerInfo_Gender(dto->trainerInfo[side]);
            CharCode_Copy(dto->trainer[side].name,
                          TrainerInfo_Name(dto->trainerInfo[side]));
        }
    }

    s_dto = dto;
    openmmo_encounter_new_vs_link(fs, dto);
    if (fs->task == NULL) {
        printf("openmmo: link battle would not start\n");
        FieldBattleDTO_Free(dto);
        link_reset();
        return -1;
    }
    s_scene = 1;
    printf("openmmo: link battle %d vs %s, this client is net id %d (%s),"
           " %d mon(s) against %d\n",
           lb->battle_id, lb->peer_name, s_net_id,
           s_net_id == 0 ? "computes" : "presents", n_mine, n_theirs);
    return 0;
}

/* One frame. Drains the peer's half of the wire, opens a scene the field was
 * not settled enough for when the seat arrived, and reports the result the
 * moment the engine has one. */
void openmmo_link_battle_poll(FieldSystem *fs, openmmo_client *c)
{
    mmo_link_battle_data blob;
    const openmmo_link_battle *lb;

    if (c == NULL)
        return;
    s_client = c;

    while (openmmo_client_link_recv(c, &blob)) {
        if (blob.kind == MMO_LINK_KIND_SYNC) {
            if (blob.len >= 1) {
                s_sync_peer = blob.data[0];
                if (s_sync_peer == s_sync_mine)
                    s_sync_state = s_sync_peer;
            }
        } else if (blob.kind == MMO_LINK_KIND_MESSAGE) {
            link_push(blob.data, blob.len);
        } else if (blob.kind == MMO_LINK_KIND_ENDWAIT) {
            /* The other engine's fight ended; end this scene's wait the same
             * way its own cmd-22 handler would have. The pointer is dropped
             * for the same reason end_fight drops it: the transport is done,
             * and the BattleSystem does not outlive its application. */
            if (s_battle_sys != NULL)
                openmmo_link_battle_abort(s_battle_sys);
            s_battle_sys = NULL;
        }
    }

    /* A seat that has not opened a scene yet, because the field was mid-warp
     * or mid-dialog when it landed. */
    lb = openmmo_client_link_battle(c);
    if (!s_scene && lb != NULL && lb->valid)
        openmmo_link_battle_open(fs, c);

    if (!s_scene)
        return;
    if (fs != NULL && FieldSystem_HasChildProcess(fs)) {
        s_saw_child = 1;
        /* The battle application writes the result into the DTO when its main
         * loop ends and then stays up for the result screen, so it is read
         * here, while the DTO is certainly still alive, rather than after
         * the encounter task has freed it. */
        if (s_dto != NULL && s_dto->resultMask != BATTLE_IN_PROGRESS
            && s_result == BATTLE_IN_PROGRESS) {
            s_result = s_dto->resultMask;
            printf("openmmo: link battle result mask %d\n", s_result);
        }
        return;
    }
    if (!s_saw_child)
        return;
    /* The child is gone, and so is the BattleSystem the transport tasks
     * belonged to: nothing may call into it again. */
    s_battle_sys = NULL;
    /* The encounter task is still winding the map back up,
     * and the DTO is its to free, so nothing here touches it again. */
    if (s_in_dropped > 0)
        printf("openmmo: link battle dropped %d message(s), the two halves"
               " were not drawing the same fight\n", s_in_dropped);
    if (!s_reported) {
        u8 mask = (u8)(s_result & 0xFF);

        s_reported = 1;
        openmmo_client_link_send(c, MMO_LINK_KIND_RESULT, &mask, 1);
        printf("openmmo: link battle ended (result %d)\n", s_result);
    }
    if (fs != NULL && fs->task != NULL)
        return;
    link_reset();
}

/* The server (or the peer walking out) closed the seat under us. */
void openmmo_link_battle_closed(openmmo_client *c)
{
    (void)c;
    if (!s_active && !s_scene)
        return;
    if (s_peer_gone)
        return;
    printf("openmmo: link battle: the other side is gone\n");
    s_peer_gone = 1;
    s_sync_state = s_sync_mine;
    s_in_head = 0;
    s_in_count = 0;
    if (s_battle_sys != NULL)
        openmmo_link_battle_abort(s_battle_sys);
    if (!s_scene)
        link_reset();
}
