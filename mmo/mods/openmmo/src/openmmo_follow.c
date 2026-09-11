/* The Pokemon that walks behind you: HeartGold's own. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "generated/movement_types.h"

#include "field/field_system.h"
#include "field_system.h"

#include "constants/map_object.h"
#include "constants/player_avatar.h"
#include "map_header.h"
#include "map_object.h"
#include "map_object_move.h"
#include "party.h"
#include "player_avatar.h"
#include "pokemon.h"
#include "save_player.h"

#include "pc_modfs.h"

#include "../../../include/follower.h"
#include "../../../include/species_port.h"
#include "openmmo_follow_internal.h"

/* openmmo_follow_talk.c. The mood belongs to the lead this file seats and
 * decays one step a field frame (FieldSystem_UnkSub108_MoveMoodTowardsNeutral). */
void openmmo_follow_talk_step(void);
void openmmo_follow_talk_reset(void);
int openmmo_follow_talk_map_mode(enum MapHeaderID header);
int openmmo_follow_talk_tp_param(int gfx, unsigned char out[4]);

static openmmo_follow_mon g_follow;

openmmo_follow_mon *openmmo_follow_state(void)
{
    return &g_follow;
}

/* Why there is no follower, said once per reason. Declining quietly is
 * indistinguishable from not running at all, which cost an afternoon once. */
static const char *g_said;

static void no_follower(const char *why)
{
    if (g_said == why) {
        return;
    }
    g_said = why;
    printf("openmmo: no follower (%s)\n", why);
    fflush(stdout);
}

/* FollowMon_GetMapObject. HeartGold keeps the pointer; this game's object
 * table is torn down by the map change, so the object is found by the id
 * FollowMon_CreateMapObject gave it rather than held. */
MapObject *openmmo_follow_object(FieldSystem *fs)
{
    if (fs == NULL || fs->mapObjMan == NULL) {
        return NULL;
    }
    return MapObjMan_LocalMapObjByIndex(fs->mapObjMan, OPENMMO_FOLLOW_ID);
}

/* ------------------------------------------------------------------ *
 *  The param-2 bit field: sub_02069DC8 .. sub_02069ED4
 * ------------------------------------------------------------------ */

static int bits_get(MapObject *obj)
{
    return MapObject_GetDataAt(obj, FOLLOW_PARAM_BITS);
}

static void bits_set(MapObject *obj, int v)
{
    MapObject_SetDataAt(obj, v, FOLLOW_PARAM_BITS);
}

/* FollowMon_SetObjectShiny: bit 0. */
static void shiny_set(MapObject *obj, int on)
{
    int v = (u32)(bits_get(obj) >> 1) << 1;

    if (on) {
        v |= 1;
    }
    bits_set(obj, v);
}

/* sub_02069DEC: bit 1, "hidden by the follow logic and waiting to be shown". */
void openmmo_follow_bit1_set(MapObject *obj, int on)
{
    int v = bits_get(obj);
    int b0 = v & 1;
    u32 rest = (u32)v >> 2;

    v = (int)(rest << 2);
    v |= (on ? 1 : 0) << 1;
    v |= b0;
    bits_set(obj, v);
}

int openmmo_follow_bit1_get(MapObject *obj)
{
    return (bits_get(obj) >> 1) & 1;
}

/* sub_02069E28: bits 8-9, the facing remembered across a ledge jump. */
void openmmo_follow_pending_set(MapObject *obj, int dir)
{
    int v = bits_get(obj);
    u16 high = (u16)((u16)(v >> 10) << 10);

    v = (u8)v;
    v |= high | ((dir & 3) << 8);
    bits_set(obj, v);
}

int openmmo_follow_pending_get(MapObject *obj)
{
    return (bits_get(obj) >> 8) & 3;
}

/* sub_02069E50: bits 10-15, the action a catch-up step repeats. */
void openmmo_follow_stored_anim_set(MapObject *obj, int a)
{
    int v = bits_get(obj);
    u32 low = (u32)v & 0x3FF;

    if (a > 23) {
        printf("openmmo: follower stored action %d is past the 23 the field holds\n", a);
        fflush(stdout);
    }
    v = (int)((a << 10) | (u8)low);
    bits_set(obj, v);
}

int openmmo_follow_stored_anim_get(MapObject *obj)
{
    return (bits_get(obj) >> 10) & 0x3F;
}

/* sub_02069E84: bit 2, "come out of the ball when next shown". */
void openmmo_follow_wants_ball_set(MapObject *obj, int on)
{
    int v = bits_get(obj);
    int a = v & 3;
    u32 b = (u32)v >> 3;

    v = (int)((b << 3) | ((on ? 1 : 0) << 2));
    v |= a;
    bits_set(obj, v);
}

int openmmo_follow_wants_ball_get(MapObject *obj)
{
    return (bits_get(obj) >> 2) & 1;
}

/* sub_0206A040: this game's hide bit is HeartGold's "visible" flag (bit 9 in
 * both), and flag 19 rides with it. */
void openmmo_follow_set_hidden_flags(MapObject *obj, int on)
{
    MapObject_SetHidden(obj, on ? 1 : 0);
    sub_02062DB4(obj, on ? 1 : 0);
}

/* sub_02069DC8. */
void openmmo_follow_hide(MapObject *obj, int on)
{
    openmmo_follow_set_hidden_flags(obj, on);
    openmmo_follow_bit1_set(obj, on ? 1 : 0);
}

/* sub_0206A054: hidden, and not waiting to be shown by the follow logic. */
void openmmo_follow_hide_quiet(FieldSystem *fs)
{
    MapObject *obj = openmmo_follow_object(fs);

    if (obj == NULL) {
        return;
    }
    openmmo_follow_hide(obj, 1);
    openmmo_follow_bit1_set(obj, 0);
}

/* ov01_022055DC / ov01_022055B0 / ov01_02205584: the three fields of
 * tp_param that ride in data slot 1, read only off the follower itself. */
int openmmo_follow_is_large(MapObject *obj)
{
    if (MapObject_GetLocalID(obj) != OPENMMO_FOLLOW_ID) {
        return 0;
    }
    return ((u16)MapObject_GetDataAt(obj, FOLLOW_PARAM_TP) >> 8) & 0xF;
}

int openmmo_follow_dip_class(MapObject *obj)
{
    if (MapObject_GetLocalID(obj) != OPENMMO_FOLLOW_ID) {
        return 0;
    }
    return (u8)MapObject_GetDataAt(obj, FOLLOW_PARAM_TP) & 0xF;
}

int openmmo_follow_no_dip(MapObject *obj)
{
    if (MapObject_GetLocalID(obj) != OPENMMO_FOLLOW_ID) {
        return 0;
    }
    return ((u8)MapObject_GetDataAt(obj, FOLLOW_PARAM_TP) >> 4) & 0xF;
}

int openmmo_follow_species_of(MapObject *obj)
{
    return MapObject_GetDataAt(obj, FOLLOW_PARAM_SPECIES);
}

/* ------------------------------------------------------------------ *
 *  Who follows, and what it looks like
 * ------------------------------------------------------------------ */

/* CountAlivePokemon, GetFirstAliveMonInParty, GetFirstNonEggInParty. */
static int party_alive(Party *party)
{
    int n = Party_GetCurrentCount(party);
    int i, alive = 0;

    for (i = 0; i < n; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

        if (mon != NULL && !Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)
            && Pokemon_GetValue(mon, MON_DATA_HP, NULL) > 0) {
            alive++;
        }
    }
    return alive;
}

static Pokemon *party_first_alive(Party *party)
{
    int n = Party_GetCurrentCount(party);
    int i;

    for (i = 0; i < n; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

        if (mon != NULL && !Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)
            && Pokemon_GetValue(mon, MON_DATA_HP, NULL) > 0) {
            return mon;
        }
    }
    return NULL;
}

static Pokemon *party_first_non_egg(Party *party)
{
    int n = Party_GetCurrentCount(party);
    int i;

    for (i = 0; i < n; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

        if (mon != NULL && !Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
            return mon;
        }
    }
    return NULL;
}

static Party *party_of(FieldSystem *fs)
{
    if (fs->saveData == NULL) {
        return NULL;
    }
    return SaveData_GetParty(fs->saveData);
}

/* Which graphics id draws this Pokemon, in this game's own band. -1 says the
 * table or the package has nothing for it, and says why. */
static int gfx_for(Pokemon *mon, int *species_out, int *form_out,
                   int *gender_out, int *shiny_out)
{
    int species = mmo_species_port_wire_id((int)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL));
    int form = (int)Pokemon_GetValue(mon, MON_DATA_FORM, NULL);
    int gender = (int)Pokemon_GetValue(mon, MON_DATA_GENDER, NULL);
    int shiny = Pokemon_IsShiny(mon) ? 1 : 0;
    int gfx = mmo_follower_gfx(species, form, gender, shiny);
    struct pc_modfs_billboard row;

    *species_out = species;
    *form_out = form;
    *gender_out = gender;
    *shiny_out = shiny;
    if (gfx < 0) {
        no_follower("this species has no follower in the table");
        return -1;
    }
    if (!pc_modfs_billboard(gfx, &row)) {
        no_follower("no package carries this one; fill one with"
                    " tools/portfollow.py");
        return -1;
    }
    return gfx;
}

/* FollowMon_GetSizeParamBySpecies: tp_param byte 1 of the species' first
 * sprite (the source indexes by SpeciesToOverworldModelIndexOffset, not by
 * the form's own sprite), read out of the carried table. */
static int size_param_by_species(int species)
{
    int gfx = mmo_follower_gfx(species, 0, 0, 0);
    unsigned char tp[4];

    if (gfx < 0 || !openmmo_follow_talk_tp_param(gfx, tp)) {
        return 0;
    }
    return tp[1];
}

/* FollowMon_DiglettPermissionCheck. The eleven Bell Tower maps ride as bit 2
 * of the carried follow-mode byte (tools/portfollow.py), so the switch on map
 * ids is one bit test here. */
#define FOLLOW_MODE_PREVENT         0
#define FOLLOW_MODE_HEIGHT_RESTRICT 1
#define FOLLOW_MODE_ALLOW           2
#define FOLLOW_MODE_MASK            3
#define FOLLOW_NO_DIGLETT           4

/* A map of this game has no followMode byte. */
static int platinum_follow_mode(enum MapHeaderID header)
{
    if (MapHeader_IsUnionRoom(header)) {
        return FOLLOW_MODE_PREVENT;
    }
    switch ((int)header) {
    case MAP_HEADER_COMMUNICATION_CLUB_COLOSSEUM_2P:
    case MAP_HEADER_COMMUNICATION_CLUB_COLOSSEUM_4P:
    case MAP_HEADER_BATTLE_TOWER:
    case MAP_HEADER_BATTLE_TOWER_ELEVATOR:
    case MAP_HEADER_BATTLE_TOWER_CORRIDOR:
    case MAP_HEADER_BATTLE_TOWER_CORRIDOR_MULTI:
    case MAP_HEADER_BATTLE_TOWER_BATTLE_ROOM:
    case MAP_HEADER_BATTLE_TOWER_MULTI_BATTLE_ROOM:
    case MAP_HEADER_POKEMON_LEAGUE_HALL_OF_FAME:
    case MAP_HEADER_GLOBAL_TERMINAL_1F:
        return FOLLOW_MODE_PREVENT;
    default:
        break;
    }
    if (MapHeader_IsOutdoors(header) || MapHeader_IsCave(header)) {
        return FOLLOW_MODE_ALLOW;
    }
    return FOLLOW_MODE_HEIGHT_RESTRICT;
}

static int follow_mode_of(enum MapHeaderID header)
{
    int mode = openmmo_follow_talk_map_mode(header);

    return mode >= 0 ? mode : platinum_follow_mode(header);
}

/* FollowMon_GetPermissionBySpeciesAndMap. */
int openmmo_follow_permission_for(int species, int header)
{
    int mode = follow_mode_of((enum MapHeaderID)header);

    if ((species == 50 || species == 51) && (mode & FOLLOW_NO_DIGLETT)) {
        return 0;
    }
    switch (mode & FOLLOW_MODE_MASK) {
    case FOLLOW_MODE_PREVENT:
        return 0;
    case FOLLOW_MODE_HEIGHT_RESTRICT:
        return size_param_by_species(species) ? 0 : 1;
    case FOLLOW_MODE_ALLOW:
    default:
        return 1;
    }
}

/* FollowMon_GetPermission: on the map the save block remembers, which is the
 * map being left while a warp is in flight. */
int openmmo_follow_permission(FieldSystem *fs)
{
    MapObject *obj = openmmo_follow_object(fs);

    if (obj == NULL) {
        return 0;
    }
    return openmmo_follow_permission_for(openmmo_follow_species_of(obj),
                                         g_follow.saved_map);
}

/* FollowMon_IsActive. */
int openmmo_follow_is_active(FieldSystem *fs)
{
    Party *party;

    if (!g_follow.active || openmmo_follow_object(fs) == NULL) {
        return 0;
    }
    party = party_of(fs);
    return party != NULL && party_alive(party) != 0;
}

/* FollowMon_IsVisible: active and the hidden flag clear. */
int openmmo_follow_is_visible(FieldSystem *fs)
{
    MapObject *obj;

    if (!openmmo_follow_is_active(fs)) {
        return 0;
    }
    obj = openmmo_follow_object(fs);
    return obj != NULL && !MapObject_IsHidden(obj);
}

/* FollowMon_SetObjectParams. tp_param's four bytes cross with the art, so
 * data slot 1 carries exactly what FollowMon_SetObjectForm wrote. */
static void set_object_params(MapObject *obj, int gfx, int species, int shiny)
{
    unsigned char tp[4] = { 0, 0, 0, 0 };

    shiny_set(obj, shiny);
    if (!openmmo_follow_talk_tp_param(gfx, tp)) {
        static int said;

        if (!said) {
            said = 1;
            printf("openmmo: no tp_param for follower gfx %d (an older package);"
                   " every follower is treated as small\n", gfx);
            fflush(stdout);
        }
    }
    MapObject_SetDataAt(obj, (tp[1] << 8) | tp[2], FOLLOW_PARAM_TP);
    MapObject_SetDataAt(obj, species, FOLLOW_PARAM_SPECIES);
}

/* FieldSystem_SetFollowerPokeParam. */
static void remember(int species, int form, int shiny, int gender, u32 personality)
{
    g_follow.species = species;
    g_follow.shiny = shiny;
    g_follow.form = form;
    g_follow.gender = gender;
    g_follow.personality = personality;
}

/* FollowMon_Clear. */
static void clear_state(void)
{
    g_follow.active = 0;
    g_follow.refuse_pending = 0;
    g_follow.arm_anim = 0;
    g_follow.arm_x = 0;
    g_follow.arm_z = 0;
    g_follow.arm_state = 0;
}

/* FollowMon_CreateMapObject: on the player's own tile, hidden, with the
 * driven movement type and the id every script finds it by. */
static MapObject *create_map_object(FieldSystem *fs, int gfx, int species,
                                    int shiny, int dir, int x, int z)
{
    enum MapHeaderID header = fs->location != NULL ? fs->location->mapHeaderID
                                                   : (enum MapHeaderID)0;
    MapObject *obj = MapObjectMan_AddMapObject(fs->mapObjMan, x, z, dir, gfx,
                                               MOVEMENT_TYPE_FOLLOW_PLAYER, header);

    if (obj == NULL) {
        /* The source asserts; a full table here is a missing sprite and the
         * crowd cull in openmmo_boot.c is what keeps us away from it. */
        printf("openmmo: the object table has no seat for the follower\n");
        fflush(stdout);
        return NULL;
    }
    MapObject_SetLocalID(obj, OPENMMO_FOLLOW_ID);
    MapObject_SetTrainerType(obj, 0);
    MapObject_SetFlag(obj, 0);
    /* std_following_mon: the A press is taken in C before any script
     * (openmmo_follow_talk_try), so the object carries no script id here. */
    MapObject_SetScript(obj, 0);
    MapObject_SetDataAt(obj, 0, FOLLOW_PARAM_BITS);
    set_object_params(obj, gfx, species, shiny);
    MapObject_SetMovementRangeX(obj, -1);
    MapObject_SetMovementRangeZ(obj, -1);
    MapObject_SetStatusFlagOn(obj, MAP_OBJ_STATUS_13);
    MapObject_SetFlagIsPersistent(obj, TRUE);
    MapObject_SetStatusFlagOff(obj, MAP_OBJ_STATUS_PAUSE_ANIMATION | MAP_OBJ_STATUS_LOCK_DIR);
    MapObject_SetDynamicHeightCalculationEnabled(obj, 1);
    openmmo_follow_hide(obj, 1);
    openmmo_follow_move_bind(obj, FOLLOW_TYPE_DRIVEN);
    return obj;
}

/* FollowMon_InitMapObject. Called where the source calls it: as the map's
 * objects are created, before the zone's own people, with the player's
 * arrival tile and facing. */
void openmmo_follow_init_map_object_at(FieldSystem *fs, int x, int z, int dir,
                                       int header);

void openmmo_follow_init_map_object(FieldSystem *fs)
{
    if (fs == NULL || fs->location == NULL) {
        return;
    }
    openmmo_follow_init_map_object_at(fs, fs->location->x, fs->location->z,
                                      fs->location->faceDirection,
                                      fs->location->mapHeaderID);
}

void openmmo_follow_init_map_object_at(FieldSystem *fs, int x, int z, int dir,
                                       int header)
{
    Party *party;
    Pokemon *mon;
    int species, form, gender, shiny, gfx, state;
    MapObject *obj;

    clear_state();
    g_follow.unused2 = 0;
    if (fs == NULL || fs->mapObjMan == NULL) {
        return;
    }
    party = party_of(fs);
    if (party == NULL || Party_GetCurrentCount(party) == 0) {
        no_follower("party holds 0");
        return;
    }
    mon = party_alive(party) == 0 ? party_first_non_egg(party)
                                  : party_first_alive(party);
    if (mon == NULL) {
        no_follower("the party holds nothing that can walk");
        return;
    }
    gfx = gfx_for(mon, &species, &form, &gender, &shiny);
    if (gfx < 0) {
        return;
    }
    if (!openmmo_follow_permission_for(species, header)) {
        no_follower("this map takes none (its game's followMode)");
        return;
    }
    obj = create_map_object(fs, gfx, species, shiny, dir, x, z);
    if (obj == NULL) {
        return;
    }
    g_follow.active = 1;
    remember(species, form, shiny, gender, Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL));
    g_said = NULL;
    printf("openmmo: follower gfx %d seated at (%d,%d), hidden, on the player\n",
           gfx, x, z);
    fflush(stdout);

    state = fs->playerAvatar != NULL ? PlayerAvatar_GetPlayerState(fs->playerAvatar)
                                     : PLAYER_AVATAR_WALKING;
    if (state == PLAYER_AVATAR_CYCLING || state == PLAYER_AVATAR_SURFING) {
        g_follow.unused2 = 2;
        openmmo_follow_hide_quiet(fs);
        openmmo_follow_switch_type(fs, FOLLOW_TYPE_LOCKSTEP);
    } else {
        g_follow.unused2 = 1;
    }
    if (g_follow.inhibit) {
        openmmo_follow_hide_quiet(fs);
    }
}

/* FollowMon_ChangeMon: a field put back with its objects (after a battle,
 * a menu, an application) keeps the follower object and re-homes the lead
 * onto it. The source reads the first alive member here, with no egg
 * fallback; so does this. */
void openmmo_follow_change_mon(FieldSystem *fs)
{
    Party *party;
    Pokemon *mon;
    MapObject *obj;
    int species, form, gender, shiny, gfx, header, state;

    clear_state();
    if (fs == NULL || fs->mapObjMan == NULL || fs->location == NULL) {
        return;
    }
    party = party_of(fs);
    if (party == NULL || Party_GetCurrentCount(party) == 0) {
        no_follower("party holds 0");
        return;
    }
    mon = party_first_alive(party);
    if (mon == NULL) {
        no_follower("the party holds nothing that can walk");
        return;
    }
    header = fs->location->mapHeaderID;
    gfx = gfx_for(mon, &species, &form, &gender, &shiny);
    obj = openmmo_follow_object(fs);
    if (gfx >= 0 && openmmo_follow_permission_for(species, header)) {
        if (obj == NULL) {
            g_follow.refuse_pending = 1;
            return;
        }
        g_follow.active = 1;
        remember(species, form, shiny, gender, Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL));
        set_object_params(obj, gfx, species, shiny);
        if ((int)MapObject_GetGraphicsID(obj) != gfx) {
            MapObject_SetGraphicsID(obj, (u32)gfx);
        }
        /* The restore rebinds every object's movement from the engine's own
         * table; this puts the source's follow back on it. */
        openmmo_follow_move_bind(obj, openmmo_follow_type());
        state = fs->playerAvatar != NULL ? PlayerAvatar_GetPlayerState(fs->playerAvatar)
                                         : PLAYER_AVATAR_WALKING;
        if (state == PLAYER_AVATAR_CYCLING || state == PLAYER_AVATAR_SURFING) {
            g_follow.unused2 = 2;
            openmmo_follow_set_hidden_flags(obj, 1);
        } else {
            g_follow.unused2 = 1;
        }
        if (openmmo_follow_bit1_get(obj)) {
            openmmo_follow_set_hidden_flags(obj, 1);
        }
        if (g_follow.inhibit) {
            openmmo_follow_hide_quiet(fs);
        }
        sub_02062D80(obj, 0);
    } else if (obj != NULL) {
        if (gfx >= 0) {
            remember(species, form, shiny, gender, Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL));
            set_object_params(obj, gfx, species, shiny);
            if ((int)MapObject_GetGraphicsID(obj) != gfx) {
                MapObject_SetGraphicsID(obj, (u32)gfx);
            }
        }
        openmmo_follow_move_bind(obj, openmmo_follow_type());
        g_follow.active = 1;
        g_follow.refuse_pending = 1;
    }
}

/* ov01_022059AC: the re-evaluation the source runs when its party menu
 * closes (ov01_02205424 asks for it). No follower and a lead that may walk
 * -> FollowMon_InitMapObject; one that the map now refuses -> gone. */
static void reevaluate(FieldSystem *fs)
{
    MapObject *obj = openmmo_follow_object(fs);

    if (!g_follow.refuse_pending) {
        return;
    }
    if (!g_follow.active || obj == NULL) {
        PlayerAvatar *av = fs->playerAvatar;

        if (av != NULL && fs->location != NULL) {
            openmmo_follow_init_map_object_at(fs, PlayerAvatar_GetXPos(av),
                                              PlayerAvatar_GetZPos(av),
                                              PlayerAvatar_GetFacingDir(av),
                                              fs->location->mapHeaderID);
        }
    } else if (!openmmo_follow_permission_for(openmmo_follow_species_of(obj),
                                              fs->location->mapHeaderID)) {
        printf("openmmo: follower put away (this map turns it away)\n");
        fflush(stdout);
        MapObject_Delete(obj);
        g_follow.active = 0;
    }
    g_follow.refuse_pending = 0;
}

/*
 * The lead can change without a menu here. The party is the server's, and a trade, a box move
 * or a faint the server settled reaches this client as new party bytes and nothing else.
 */
static void notice_party(FieldSystem *fs)
{
    Party *party = party_of(fs);
    Pokemon *mon;
    MapObject *obj = openmmo_follow_object(fs);

    if (party == NULL) {
        return;
    }
    mon = party_alive(party) == 0 ? party_first_non_egg(party) : party_first_alive(party);
    if (mon == NULL) {
        if (obj != NULL) {
            printf("openmmo: follower put away (the party holds nothing that can walk)\n");
            fflush(stdout);
            MapObject_Delete(obj);
        }
        g_follow.active = 0;
        return;
    }
    if (obj == NULL) {
        if (!g_follow.active) {
            g_follow.refuse_pending = 1;
        }
        return;
    }
    if (mmo_species_port_wire_id((int)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL)) != g_follow.species) {
        openmmo_follow_change_mon(fs);
    } else if (Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL) != g_follow.personality
               || (int)Pokemon_GetValue(mon, MON_DATA_FORM, NULL) != g_follow.form
               || (Pokemon_IsShiny(mon) ? 1 : 0) != g_follow.shiny) {
        openmmo_follow_change_mon(fs);
    }
}

/* Save_FollowMon_SetInhibitFlagState, from ScrCmd_SetFollowMonInhibitState.
 * A warp clears it (field_warp_tasks.c:271). */
void openmmo_follow_set_inhibit(FieldSystem *fs, int on);
void openmmo_follow_set_inhibit(FieldSystem *fs, int on)
{
    (void)fs;
    g_follow.inhibit = on ? 1 : 0;
}

void openmmo_follow_warp_clears_inhibit(void);
void openmmo_follow_warp_clears_inhibit(void)
{
    g_follow.inhibit = 0;
}

/* Save_FollowMon_SetMapID. */
void openmmo_follow_save_map(int header);
void openmmo_follow_save_map(int header)
{
    g_follow.saved_map = header;
}

/* One settled frame. `field_ready` is the caller's own settled test. The
 * source runs the mood decay off its field control; this does the same. */
void openmmo_follow_tick(FieldSystem *fs, int field_ready);

void openmmo_follow_tick(FieldSystem *fs, int field_ready)
{
    if (fs == NULL || fs->mapObjMan == NULL) {
        return;
    }
    if (!field_ready) {
        return;
    }
    openmmo_follow_talk_step();
    notice_party(fs);
    reevaluate(fs);
}

/* Called when the world is left, so a new session does not believe in the
 * last one's follower. */
void openmmo_follow_reset(void);

void openmmo_follow_reset(void)
{
    memset(&g_follow, 0, sizeof g_follow);
    g_said = NULL;
    openmmo_follow_move_forget();
    openmmo_follow_fx_forget();
    openmmo_follow_talk_reset();
}
