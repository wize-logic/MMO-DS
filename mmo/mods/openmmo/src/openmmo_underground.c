/* The Underground, with this session as the group. */

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/field/map.h"
#include "constants/field/map_load.h"
#include "constants/heap.h"
#include "constants/map_object.h"
#include "underground/manager.h"
#include "underground/mining.h"
#include "underground/player.h"
#include "underground/player_talk.h"
#include "underground/secret_bases.h"
#include "underground/top_screen.h"

#include "field/field_system.h"
#include "brightness_controller.h"
#include "comm_player_manager.h"
#include "communication_system.h"
#include "field_bgm.h"
#include "field_map_change.h"
#include "field_overworld_state.h"
#include "field_system.h"
#include "field_task.h"
#include "field_transition.h"
#include "game_overlay.h"
#include "generated/map_headers.h"
#include "heap.h"
#include "item_use_functions.h"
#include "location.h"
#include "underground.h"
#include "vars_flags.h"
#include "network_icon.h"
#include "player_avatar.h"
#include "save_player.h"
#include "savedata.h"
#include "sound.h"
#include "sound_playback.h"
#include "start_menu.h"
#include "system.h"   /* gSystem: the frame counter official seeds from */
#include "underground_map_transition.h"
#include "charcode_util.h"
#include "map_object_move.h"
#include "trainer_info.h"
#include "unk_02032798.h"
#include "unk_02099500.h"

#include "../../../include/charcode.h"
#include "../../../include/client.h"

/* The one overlay this file names. The engine's own declaration of it lives
 * beside its use in field_map_change.c:77; the symbol is the linker's. */
FS_EXTERN_OVERLAY(underground);

#define UNDERGROUND_HEAP HEAP_ID_FIELD2

enum {
    DESCEND_FADE_BGM = 0,
    DESCEND_HOLE_ANIM,
    DESCEND_FINISH_MAP,
    DESCEND_LOAD_MAP,
    DESCEND_START_MAP,
    DESCEND_ARRIVE_ANIM,
    DESCEND_END,
};

enum {
    CLIMB_DIM_TOP_SCREEN = 0,
    CLIMB_FADE_BGM,
    CLIMB_ASCEND_ANIM,
    CLIMB_FINISH_MAP,
    CLIMB_LOAD_MAP,
    CLIMB_START_MAP,
    CLIMB_HOLE_ANIM,
    CLIMB_END,
};

typedef struct {
    int state;
    int transitionState;
    BOOL animationDone;
    int header;
    int destX;
    int destZ;
} UndergroundTrip;

/* The field the port is running, so the frame tick can find the player without
 * being handed one. pc/src/pc_lab.c; openmmo_boot.c names it the same way. */
extern FieldSystem *pc_lab_field_system(void);

/* player_talk.c, via mods/openmmo/patches: whether either of the two talk menus
 * is on the screen. The header does not carry it, the engine never had to ask,
 * so it is named here beside its use, the way every other answer this file
 * adds to the engine is named in the patch that calls it. */
extern BOOL UndergroundTalk_IsActive(void);

/* The pipe and the resources it carries, defined at the foot of this file
 * beside the reasoning for them. Named here because the descent and the climb
 * are the two things that switch them on and off. */
static int resources_up(FieldSystem *fs);
static void resources_down(void);
static void mirror_local_player(FieldSystem *fs);
static void ug_pump(void);
static void talk_on_send(int cmd, const void *data, int size);
static void talk_pump(void);
static void talk_tick(void);
static void talk_close(int tell_server);
static void talk_close_common(void);
static int talk_availability(void);
static TrainerInfo *talk_peer_trainer_info(int netId);

/* Live between the moment the descent commits and the moment the climb
 * begins. Two engine answers hang off this, so it is set late (once the map
 * is actually the Underground) and cleared early (the first frame of the
 * climb), never around the transitions themselves. */
static int s_down;
static int s_running;

int openmmo_underground_active(void)
{
    return s_down;
}

int openmmo_underground_comm_up(void)
{
    return s_down;
}

int openmmo_underground_walks_as_overworld(void)
{
    /* Not gated on s_down: the redirect has to hold for the whole time the
     * load type is UNDERGROUND, including the frames either side of the
     * transitions where s_down is deliberately not set yet. A field whose
     * load type is not UNDERGROUND never reaches this question. */
    return 1;
}

/*
 * The engine's own surface-tile-to-cavern-tile mapping (field_map_change.c,
 * MapChangeUndergroundContext_New).
 */
static int cavern_tile_for(const FieldSystem *fs, int *outX, int *outZ)
{
    int x = PlayerAvatar_GetXPos(fs->playerAvatar);
    int z = PlayerAvatar_GetZPos(fs->playerAvatar);
    int matrixX = x / MAP_TILES_COUNT_X - 1;
    int matrixZ = z / MAP_TILES_COUNT_Z - 6;
    int cellX, cellZ;

    if (matrixX < 0 || matrixZ < 0)
        return 0;

    cellX = (matrixX % 2 == 0) ? 8 : 23;
    cellZ = (matrixZ % 2 == 0) ? 8 : 23;

    matrixX = matrixX / 2 + SECRET_BASE_WIDTH / MAP_TILES_COUNT_X;
    matrixZ = matrixZ / 2 + SECRET_BASE_DEPTH * 2 / MAP_TILES_COUNT_Z + 1;

    *outX = matrixX * MAP_TILES_COUNT_X + cellX;
    *outZ = matrixZ * MAP_TILES_COUNT_Z + cellZ;
    return 1;
}

/* One of the four hole/ladder animations, run to completion. The engine keeps
 * the same two-state shape in a static helper it does not export. */
static int transition_done(FieldTask *task, UndergroundTrip *trip,
                           enum UGMapTransition which)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);

    switch (trip->transitionState) {
    case 0:
        trip->animationDone = FALSE;
        UndergroundMapTransition_StartTransition(fs, which, &trip->animationDone);
        trip->transitionState++;
        break;
    case 1:
        if (trip->animationDone) {
            trip->transitionState = 0;
            return 1;
        }
        break;
    }
    return 0;
}

static BOOL descend_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    UndergroundTrip *trip = FieldTask_GetEnv(task);

    switch (trip->state) {
    case DESCEND_FADE_BGM:
        Sound_FadeOutBGM(0, 30);
        trip->state++;
        break;

    case DESCEND_HOLE_ANIM:
        if (transition_done(task, trip, UG_MAP_TRANSITION_ENTER_START))
            trip->state++;
        break;

    case DESCEND_FINISH_MAP:
        FieldTransition_FinishMap(task);
        trip->state++;
        break;

    case DESCEND_LOAD_MAP:
        fs->mapLoadType = MAP_LOAD_TYPE_UNDERGROUND;
        Overlay_LoadByID(FS_OVERLAY_ID(underground), OVERLAY_LOAD_ASYNC);
        FieldTask_ChangeMapToLocation(task, MAP_HEADER_UNDERGROUND,
                                      WARP_ID_NONE, trip->destX, trip->destZ,
                                      DIR_SOUTH);
        trip->state++;
        break;

    case DESCEND_START_MAP:
        if (Sound_IsFadeActive())
            break;
        Sound_SetScene(SOUND_SCENE_NONE);
        FieldBGM_ClearOverride(fs);
        FieldTransition_StartMap(task);
        trip->state++;
        break;

    case DESCEND_ARRIVE_ANIM:
        if (transition_done(task, trip, UG_MAP_TRANSITION_ENTER_ARRIVE)) {
            /* From here the map is the Underground, so the two engine answers
             * this file gives may start being given. The resources go up in the
             * same frame the engine's own descent brings them up
             * (field_map_change.c, CommManUnderground_EnterUnderground). */
            s_down = 1;
            resources_up(fs);
            fs->ugTopScreenCtx = UndergroundTopScreen_StartTask(fs);
            BrightnessController_StartTransition(30, 0, -16,
                                                 GX_BLEND_PLANEMASK_BG0
                                                 | GX_BLEND_PLANEMASK_BG3
                                                 | GX_BLEND_PLANEMASK_OBJ,
                                                 BRIGHTNESS_SUB_SCREEN);
            trip->state++;
        }
        break;

    case DESCEND_END:
        if (BrightnessController_IsTransitionComplete(BRIGHTNESS_SUB_SCREEN)) {
            SecretBases_SetEntranceGraphicsEnabled(TRUE);
            printf("openmmo: underground, arrived at (%d,%d)\n",
                   trip->destX, trip->destZ);
            s_running = 0;
            Heap_Free(trip);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static BOOL climb_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    UndergroundTrip *trip = FieldTask_GetEnv(task);

    switch (trip->state) {
    case CLIMB_DIM_TOP_SCREEN:
        /* Cleared before the top screen is told to go, because the wait below
         * is on this answer as well as on the task: the engine's own exit
         * waits for `ugTopScreenCtx == NULL && !CommSys_IsInitialized()`. */
        SecretBases_SetEntranceGraphicsEnabled(FALSE);
        s_down = 0;
        UndergroundTopScreen_EndTask(fs->ugTopScreenCtx);
        BrightnessController_StartTransition(30, -16, 0,
                                             GX_BLEND_PLANEMASK_BG0,
                                             BRIGHTNESS_SUB_SCREEN);
        trip->state++;
        break;

    case CLIMB_FADE_BGM:
        if (!BrightnessController_IsTransitionComplete(BRIGHTNESS_SUB_SCREEN))
            break;
        if (fs->ugTopScreenCtx != NULL)
            break;
        /* Only now: the top screen reads the underground manager every frame it
         * is up, and freeing it underneath that task is what the NULL guard in
         * patches/src/underground/manager.c was written for. The official client defers this
         * the same way, on a three-frame timer while its radio winds down. */
        resources_down();
        Sound_FadeOutBGM(0, 30);
        trip->state++;
        break;

    case CLIMB_ASCEND_ANIM:
        if (transition_done(task, trip, UG_MAP_TRANSITION_EXIT_START))
            trip->state++;
        break;

    case CLIMB_FINISH_MAP:
        FieldTransition_FinishMap(task);
        trip->state++;
        break;

    case CLIMB_LOAD_MAP:
        fs->mapLoadType = MAP_LOAD_TYPE_OVERWORLD;
        Overlay_UnloadByID(FS_OVERLAY_ID(underground));
        FieldTask_ChangeMapToLocation(task, (enum MapHeaderID)trip->header,
                                      WARP_ID_NONE, trip->destX, trip->destZ,
                                      DIR_SOUTH);
        trip->state++;
        break;

    case CLIMB_START_MAP:
        if (Sound_IsFadeActive())
            break;
        Sound_SetScene(SOUND_SCENE_NONE);
        FieldBGM_ClearOverride(fs);
        FieldTransition_StartMap(task);
        trip->state++;
        break;

    case CLIMB_HOLE_ANIM:
        if (transition_done(task, trip, UG_MAP_TRANSITION_EXIT_ARRIVE))
            trip->state++;
        break;

    case CLIMB_END:
        printf("openmmo: underground, back up on header %d at (%d,%d)\n",
               trip->header, trip->destX, trip->destZ);
        s_running = 0;
        Heap_Free(trip);
        return TRUE;
    }
    return FALSE;
}

/*
 * Where a trip is started from. It is the only thing the four doors differ in, what owns the
 * field task at the moment the trip begins:
 */
enum {
    TRIP_ALONE = 0,
    TRIP_CALLED,
    TRIP_JUMPED,
};

static int trip_may_start(FieldSystem *fs, int shape)
{
    if (fs == NULL || fs->playerAvatar == NULL || fs->location == NULL)
        return 0;
    if (s_running)
        return 0;
    /*
     * TRIP_JUMPED is asked while the bag is still the running application, the item's use
     * function is called out of the bag's exit, before the engine's own
     * FieldSystem_StartFieldMap puts the map back.
     */
    if (shape != TRIP_JUMPED
        && (!FieldSystem_IsRunningFieldMap(fs) || FieldSystem_HasChildProcess(fs)))
        return 0;
    if (shape == TRIP_ALONE && fs->task != NULL)
        return 0;
    return 1;
}

/* Everything a descent needs decided before anything is started, so a door
 * that cannot go says so without having moved the player, taken the heap or
 * set s_running. NULL means it cannot; the reason is printed once here rather
 * than guessed at three call sites. */
static UndergroundTrip *descend_prepare(FieldSystem *fs, int shape)
{
    UndergroundTrip *trip;
    Location *special;
    int destX = 0, destZ = 0;

    if (!trip_may_start(fs, shape) || s_down)
        return NULL;
    if (fs->mapLoadType != MAP_LOAD_TYPE_OVERWORLD)
        return NULL;
    if (!cavern_tile_for(fs, &destX, &destZ)) {
        printf("openmmo: underground, no cavern tile under header %d;"
               " the Explorer Kit only opens on the outdoor matrix\n",
               fs->location != NULL ? (int)fs->location->mapHeaderID : -1);
        return NULL;
    }

    trip = Heap_Alloc(UNDERGROUND_HEAP, sizeof(UndergroundTrip));
    if (trip == NULL) {
        printf("openmmo: underground would not allocate\n");
        return NULL;
    }
    memset(trip, 0, sizeof(*trip));
    trip->destX = destX;
    trip->destZ = destZ;

    /* Where to put us back. The engine keeps the surface tile in the save's
     * "special location" slot for exactly this, and the climb reads it from
     * the same place, so a session that ends underground still knows the way
     * out on the next boot. Written only once the trip is certain to run. */
    special = FieldOverworldState_GetSpecialLocation(
        SaveData_GetFieldOverworldState(fs->saveData));
    Location_Set(special, fs->location->mapHeaderID, WARP_ID_NONE,
                 PlayerAvatar_GetXPos(fs->playerAvatar),
                 PlayerAvatar_GetZPos(fs->playerAvatar), DIR_SOUTH);

    /* Roark is standing at the bottom of the hole otherwise. */
    {
        VarsFlags *vf = SaveData_GetVarsFlags(fs->saveData);
        u16 *seen = VarsFlags_GetVarAddress(vf, VAR_HAS_SEEN_UNDERGROUND_ROARK_INTRO);

        if (seen != NULL && *seen == 0) {
            *seen = 1;
            VarsFlags_SetFlag(vf, FLAG_HAS_SEEN_UNDERGROUND_ROARK_INTRO);
            printf("openmmo: underground, the Roark intro is marked seen;"
                   " its scene is one of the scripts this client refuses\n");
        }
    }

    s_running = 1;
    printf("openmmo: underground, descending to (%d,%d)\n", destX, destZ);
    return trip;
}

static UndergroundTrip *climb_prepare(FieldSystem *fs, int shape)
{
    UndergroundTrip *trip;
    const Location *special;

    if (!trip_may_start(fs, shape) || !s_down)
        return NULL;
    if (fs->mapLoadType != MAP_LOAD_TYPE_UNDERGROUND)
        return NULL;

    special = FieldOverworldState_GetSpecialLocation(
        SaveData_GetFieldOverworldState(fs->saveData));
    if (special == NULL || special->mapHeaderID == MAP_HEADER_UNDERGROUND) {
        printf("openmmo: underground, no surface tile to climb out to\n");
        return NULL;
    }

    trip = Heap_Alloc(UNDERGROUND_HEAP, sizeof(UndergroundTrip));
    if (trip == NULL) {
        printf("openmmo: underground would not allocate\n");
        return NULL;
    }
    memset(trip, 0, sizeof(*trip));
    trip->header = (int)special->mapHeaderID;
    trip->destX = special->x;
    trip->destZ = special->z;

    s_running = 1;
    printf("openmmo: underground, climbing out to header %d\n", trip->header);
    return trip;
}

int openmmo_underground_enter(FieldSystem *fs, FieldTask *caller)
{
    UndergroundTrip *trip = descend_prepare(fs, caller != NULL ? TRIP_CALLED
                                                               : TRIP_ALONE);

    if (trip == NULL)
        return 0;
    MapObjectMan_PauseAllMovement(fs->mapObjMan);
    if (caller != NULL)
        FieldTask_InitCall(caller, descend_task, trip);
    else
        FieldSystem_CreateTask(fs, descend_task, trip);
    return 1;
}

int openmmo_underground_leave(FieldSystem *fs, FieldTask *caller)
{
    UndergroundTrip *trip = climb_prepare(fs, caller != NULL ? TRIP_CALLED
                                                             : TRIP_ALONE);

    if (trip == NULL)
        return 0;
    if (caller != NULL)
        FieldTask_InitCall(caller, climb_task, trip);
    else
        FieldSystem_CreateTask(fs, climb_task, trip);
    return 1;
}

/* The Underground's own way out, which is the official one: its menu's GO UP, answered yes. */
int openmmo_underground_menu_exit(FieldSystem *fs)
{
    if (fs == NULL || !s_down)
        return 0;
    FieldSystem_ResumeProcessing();
    if (!openmmo_underground_leave(fs, NULL)) {
        printf("openmmo: underground, the menu asked to go up and the climb"
               " would not start\n");
        return 0;
    }
    return 1;
}

/* ---- The Explorer Kit ------------------------------------------------- */

/* CanUseExplorerKit. -1 = not ours, answer the engine's own way; otherwise the
 * enum value to return. Underground the engine would refuse on its own first
 * test (the cavern is not on the main matrix), which would make the way out
 * unreachable, so that is the one case answered here. */
int openmmo_underground_item_check(void)
{
    if (s_down)
        return ITEM_USE_CAN_USE;
    return -1;
}

/* UseExplorerKitInField: the kit registered to Y and pressed in the overworld.
 * No task is running, so the trip creates its own. */
int openmmo_underground_item_field(FieldSystem *fs)
{
    if (fs == NULL)
        return 0;
    if (s_down)
        return openmmo_underground_leave(fs, NULL);
    return openmmo_underground_enter(fs, NULL);
}

/*
 * UseExplorerKitFromMenu: the kit used out of the bag, which is an application inside the
 * start menu's own field task.
 */
int openmmo_underground_item_menu(FieldSystem *fs, void *menuVoid)
{
    StartMenu *menu = menuVoid;
    UndergroundTrip *trip;
    FieldTaskFunc func;

    if (fs == NULL || menu == NULL)
        return 0;

    if (s_down) {
        trip = climb_prepare(fs, TRIP_JUMPED);
        func = climb_task;
    } else {
        trip = descend_prepare(fs, TRIP_JUMPED);
        func = descend_task;
    }
    if (trip == NULL)
        return 0;

    FieldSystem_StartFieldMap(fs);
    menu->callback = func;
    menu->taskData = trip;
    menu->state = START_MENU_STATE_NEW_TASK;
    fs->menuCursorPos = 0;
    return 1;
}

/* ---- The pipe --------------------------------------------------------- */

#define UG_QUEUE      64
#define UG_MSG_MAX    2048

/* The two heaps the Underground lives in, and why they are not the official client's. */
#define UG_COMM_HEAP_DEFAULT 0x8000
#define UG_TEXT_HEAP_DEFAULT 0xE800
#define UG_COMM_HEAP_SIZE    ug_heap_size("OPENMMO_UG_COMM_HEAP", UG_COMM_HEAP_DEFAULT)
#define UG_TEXT_HEAP_SIZE    ug_heap_size("OPENMMO_UG_TEXT_HEAP", UG_TEXT_HEAP_DEFAULT)

static u32 ug_heap_size(const char *name, u32 fallback)
{
    const char *v = getenv(name);
    char *end;
    unsigned long n;

    if (v == NULL || v[0] == '\0')
        return fallback;
    n = strtoul(v, &end, 0);
    if (*end != '\0' || n < 0x1000 || n > 0x40000) {
        printf("openmmo: %s=%s is not a heap size in [0x1000,0x40000];"
               " using %#x\n", name, v, (unsigned)fallback);
        return fallback;
    }
    return (u32)n;
}

typedef struct {
    int cmd;
    int len;
    u8 *data;
} UgMessage;

static UgMessage s_q[UG_QUEUE];
static int s_q_head;      /* next to deliver */
static int s_q_count;
static int s_q_dropped;
static int s_pipe;        /* the queue is live and the command table is up */

/* One message, copied. The engine hands a pointer into its own stack or into a
 * static it is about to reuse, so nothing may be kept by reference. */
/* How wide one command's body is. The fixed-size senders pass no length; the
 * command table is what knows it. Asked before the queue rather than inside it
 * because the relay to the other player needs the same answer. */
static int ug_body_size(int cmd, const void *data, int size)
{
    if (size == 0 && data != NULL) {
        size = CommCmd_PacketSizeOf(cmd);
        if (size == PACKET_SIZE_VARIABLE)
            size = 0;
    }
    return size;
}

static int ug_queue(int cmd, const void *data, int size)
{
    UgMessage *m;
    int slot;

    if (!s_pipe)
        return 0;
    size = ug_body_size(cmd, data, size);
    if (size < 0 || size > UG_MSG_MAX) {
        printf("openmmo: underground, command %d is %d bytes, past this"
               " queue's %d\n", cmd, size, UG_MSG_MAX);
        return 0;
    }
    if (s_q_count >= UG_QUEUE) {
        if (s_q_dropped++ == 0)
            printf("openmmo: underground, the command queue is full;"
                   " command %d dropped\n", cmd);
        return 0;
    }

    slot = (s_q_head + s_q_count) % UG_QUEUE;
    m = &s_q[slot];
    m->cmd = cmd;
    m->len = size;
    m->data = NULL;
    if (size > 0) {
        m->data = Heap_Alloc(HEAP_ID_COMMUNICATION, (u32)size);
        if (m->data == NULL) {
            printf("openmmo: underground, no room to queue command %d\n", cmd);
            return 0;
        }
        if (data != NULL)
            memcpy(m->data, data, (size_t)size);
        else
            memset(m->data, 0, (size_t)size);
    }
    s_q_count++;
    return 1;
}

/* The six the patch on communication_system.c calls. Every one of them answers
 * TRUE the way the engine's own does when the ring took the bytes, because the
 * callers treat FALSE as "try again next frame" and would spin. */
/* The trade scene borrows this whole shim family: its pipe and the
 * cavern's are never up together, and the patches on the comm stack ask
 * these first, so routing here keeps the engine seam one seam.
 * mmo/mods/openmmo/src/openmmo_trade.c. */
extern int openmmo_trade_pipe_up(void);
extern int openmmo_trade_net_id(void);
extern int openmmo_trade_send(int cmd, const void *data, int size);
extern int openmmo_trade_queued(int cmd);
extern int openmmo_trade_player_connected(int netId);
extern TrainerInfo *openmmo_trade_trainer_info(int netId);

int openmmo_ug_comm_up(void)
{
    return s_pipe || openmmo_trade_pipe_up();
}

int openmmo_ug_net_id(void)
{
    if (openmmo_trade_pipe_up())
        return openmmo_trade_net_id();
    return s_pipe ? 0 : -1;
}

/* Who net id 0 is. */
TrainerInfo *openmmo_ug_trainer_info(int netId)
{
    FieldSystem *fs;

    if (openmmo_trade_pipe_up())
        return openmmo_trade_trainer_info(netId);
    if (!s_pipe)
        return NULL;
    /*
     * And who the other net id is, while a conversation is up: the person on the other end of
     * it, built from the name and gender their spawn packet carried.
     */
    {
        TrainerInfo *peer = talk_peer_trainer_info(netId);

        if (peer != NULL)
            return peer;
    }
    if (netId != openmmo_ug_net_id())
        return NULL;

    fs = pc_lab_field_system();
    if (fs == NULL || fs->saveData == NULL)
        return NULL;

    return SaveData_GetTrainerInfo(fs->saveData);
}

int openmmo_ug_send(int cmd, const void *data, int size)
{
    if (openmmo_trade_pipe_up())
        return openmmo_trade_send(cmd, data, size);
    if (!s_pipe)
        return 0;
    size = ug_body_size(cmd, data, size);
    /* A conversation's own commands are the one thing down here with a second
     * end. They still go round this client's queue, our own handlers filter
     * them out by net id, which is what the official client's do with the copy the sender
     * receives, and the same bytes go to the other player. */
    talk_on_send(cmd, data, size);
    return ug_queue(cmd, data, size);
}

/* ---- The two traps that move you ------------------------------------- */
#define UG_MOVE_NORMAL   0
#define UG_MOVE_REVERSE  1
#define UG_MOVE_RANDOM   2

static int s_scramble;
static u16 s_scramble_key;
static s8 s_scramble_timer;
static MATHRandContext32 s_scramble_rand;

void openmmo_underground_set_movement_state(int state)
{
    if (!s_pipe)
        return;
    if (s_scramble == UG_MOVE_NORMAL && state != UG_MOVE_NORMAL) {
        s_scramble_key = 0;
        s_scramble_timer = 0;
        CommSys_Seed(&s_scramble_rand);
    }
    s_scramble = state;
}

/* The keys the avatar is walked from this frame. Returns `keys` untouched when
 * no trap is on, which is every frame but the ones after stepping on one. */
u16 openmmo_underground_scramble(u16 keys)
{
    u16 replaced = 0;

    if (!s_pipe || s_scramble == UG_MOVE_NORMAL)
        return keys;
    if (!(keys & (PAD_KEY_LEFT | PAD_KEY_RIGHT | PAD_KEY_UP | PAD_KEY_DOWN)))
        return keys;

    if (s_scramble == UG_MOVE_REVERSE) {
        if (keys & PAD_KEY_LEFT)
            replaced |= PAD_KEY_RIGHT;
        if (keys & PAD_KEY_RIGHT)
            replaced |= PAD_KEY_LEFT;
        if (keys & PAD_KEY_UP)
            replaced |= PAD_KEY_DOWN;
        if (keys & PAD_KEY_DOWN)
            replaced |= PAD_KEY_UP;
    } else if (s_scramble_key != 0) {
        replaced = s_scramble_key;
        s_scramble_timer--;
        if (s_scramble_timer < 0)
            s_scramble_key = 0;
    } else {
        switch (MATH_Rand32(&s_scramble_rand, 4)) {
        case 0:
            replaced = PAD_KEY_LEFT;
            break;
        case 1:
            replaced = PAD_KEY_RIGHT;
            break;
        case 2:
            replaced = PAD_KEY_UP;
            break;
        default:
            replaced = PAD_KEY_DOWN;
            break;
        }
        s_scramble_timer = (s8)MATH_Rand32(&s_scramble_rand, 16);
        s_scramble_key = replaced;
    }

    keys &= (u16)~(PAD_KEY_LEFT | PAD_KEY_RIGHT | PAD_KEY_UP | PAD_KEY_DOWN);
    return (u16)(keys | replaced);
}

/*
 * Whether a command is already waiting to be delivered. The Underground asks this before it
 * re-sends a periodic answer, the trap and mining radar results go out every frame otherwise,
 * so answering "no" would fill the queue with copies of the same reply.
 */
int openmmo_ug_queued(int cmd)
{
    int i;

    if (openmmo_trade_pipe_up())
        return openmmo_trade_queued(cmd);
    if (!s_pipe)
        return 0;
    for (i = 0; i < s_q_count; i++) {
        if (s_q[(s_q_head + i) % UG_QUEUE].cmd == cmd)
            return 1;
    }
    return 0;
}

/* One frame's delivery. The count is taken first so that what a handler sends
 * waits for the next frame, which is where the DS would have put it. */
static void ug_pump(void)
{
    int n;

    if (!s_pipe)
        return;
    n = s_q_count;
    while (n-- > 0 && s_q_count > 0) {
        UgMessage m = s_q[s_q_head];

        s_q[s_q_head].data = NULL;
        s_q_head = (s_q_head + 1) % UG_QUEUE;
        s_q_count--;
        CommCmd_Callback(0, m.cmd, m.len, m.data);
        if (m.data != NULL)
            Heap_Free(m.data);
        /* A handler may have torn the Underground down under us (the exit
         * command does exactly that), so stop rather than deliver into a
         * command table that is no longer registered. */
        if (!s_pipe)
            return;
    }
}

static void ug_queue_clear(void)
{
    while (s_q_count > 0) {
        if (s_q[s_q_head].data != NULL)
            Heap_Free(s_q[s_q_head].data);
        s_q[s_q_head].data = NULL;
        s_q_head = (s_q_head + 1) % UG_QUEUE;
        s_q_count--;
    }
    s_q_head = 0;
    s_q_count = 0;
    s_q_dropped = 0;
}

/* ---- Talking to another player ---------------------------------------- */

/* The net id the person we are talking to holds. One conversation at a time is
 * the official client's own rule, `sCurrentTalkMenu` and `sCurrentResponseMenu` are single
 * globals, so one id is the whole model. */
#define UG_TALK_PEER 1

/* How long a paused field waits for something to open on it. Four seconds is
 * far past any round trip this client would still call playable, and short
 * enough that a server which stopped answering is a pause rather than a hang. */
#define UG_TALK_WAIT 240

/* How long the person we are talking to may be absent from this client's own
 * entity model before the conversation is treated as over. They are paused too,
 * so this is a disconnect or a warp the server has not told us about yet. */
#define UG_TALK_GONE 60

/*
 * People greeted since the descent. The official client dedupes the people-met record on the group's net
 * id, which is stable for a whole wireless session; ours is not, everyone we talk to is id 1,
 * so the peers themselves are remembered instead.
 */
#define UG_TALK_MET_MAX 16

static openmmo_client *s_talk_client;
static int s_talk_live;      /* a conversation is seated and the peer is here */
static int s_talk_menus;     /* the engine's own menus were up last frame */
static int s_talk_wait;      /* frames left before a paused field is given back */
static int s_talk_missing;   /* frames the peer has not been in the model */
static int s_talk_told;      /* the server is the one that said this ended */
static u32 s_talk_peer;      /* the peer's server entity id */
static TrainerInfo *s_talk_info;  /* their name and gender, as the cavern asks */
static int s_talk_reported;  /* the availability last sent; -1 is none */
static u32 s_talk_met[UG_TALK_MET_MAX];
static int s_talk_met_count;

/* The session this cavern is in. openmmo_boot.c hands it over beside the other
 * attaches, so nothing below has to be given a client per call. */
void openmmo_underground_attach(openmmo_client *c)
{
    s_talk_client = c;
    s_talk_reported = -1;
}

/* The five that cross, and where each carries the recipient's net id. Both
 * answers are read off the structs in src/underground/player_talk.c and
 * src/underground/records.c. */
static int talk_relayed(int cmd)
{
    return cmd == 75 || cmd == 76 || cmd == 78 || cmd == 80 || cmd == 82;
}

static int talk_recipient_at(int cmd)
{
    switch (cmd) {
    case 75:
    case 76:
        return 0; /* TalkStateChangeRequest.targetNetID */
    case 78:
        return 0; /* Gift.recipientNetID */
    case 80:
        return 1; /* TalkMessage.recipientNetID */
    case 82:
        return 0; /* RecordBuffer.netID */
    }
    return -1;
}

/* One of ours, on its way to them. The command id rides in front of the body so
 * the other end can hand it to the same table this one did. Called for every
 * send while a conversation is up; everything that is not the conversation's
 * own stays in this client, which is the whole of the rest of the Underground. */
static void talk_on_send(int cmd, const void *data, int size)
{
    u8 blob[MMO_UG_TALK_MAX];

    if (!s_talk_live || s_talk_client == NULL || !talk_relayed(cmd))
        return;
    if (size < 0 || size + 1 > (int)sizeof blob) {
        printf("openmmo: underground, command %d is %d bytes, past what a"
               " conversation carries\n", cmd, size);
        return;
    }
    blob[0] = (u8)cmd;
    if (size > 0) {
        if (data != NULL)
            memcpy(blob + 1, data, (size_t)size);
        else
            memset(blob + 1, 0, (size_t)size);
    }
    openmmo_client_ug_talk_send(s_talk_client, MMO_UG_TALK_DATA,
                                (s64)s_talk_peer, blob, size + 1);
}

/* And one of theirs, in our frame. See "the one byte that is rewritten". */
static void talk_deliver(const mmo_underground_talk *msg)
{
    u8 body[MMO_UG_TALK_MAX];
    int cmd, len, at;

    if (msg->len < 1)
        return;
    cmd = msg->data[0];
    len = msg->len - 1;
    if (!talk_relayed(cmd)) {
        printf("openmmo: underground, a conversation carried command %d,"
               " which is not one of its own\n", cmd);
        return;
    }
    if (len > 0)
        memcpy(body, msg->data + 1, (size_t)len);
    at = talk_recipient_at(cmd);
    if (at >= 0 && at < len)
        body[at] = (u8)openmmo_ug_net_id();
    ug_queue(cmd, len > 0 ? body : NULL, len);
}

/* The peer, as this client currently draws them. Everything a conversation
 * needs to know about a person, their tile, their name and which trainer they
 * are drawn as, is on the entity the spawn packet seated, so it is asked for
 * rather than carried in a seat packet of its own. */
static int talk_peer_now(openmmo_event *out)
{
    if (s_talk_client == NULL || s_talk_peer == 0)
        return 0;
    return openmmo_client_entity_by_id(s_talk_client, s_talk_peer, out);
}

/*
 * Their TrainerInfo. The cavern's own text puts a name in it, the greeting on the way in,
 * the goodbye on the way out, the trainer case's header, and picks the male or the female
 * wording off the gender, so both have to be answers of the right shape.
 */
static void talk_seat_info(FieldSystem *fs, const openmmo_event *peer)
{
    mmo_charcode buf[TRAINER_NAME_LEN + 4];
    size_t k;
    TrainerInfo *mine;

    if (s_talk_info != NULL || fs == NULL || fs->saveData == NULL)
        return;
    mine = SaveData_GetTrainerInfo(fs->saveData);
    if (mine == NULL)
        return;
    s_talk_info = TrainerInfo_New(HEAP_ID_UNDERGROUND);
    if (s_talk_info == NULL) {
        printf("openmmo: underground, no room for the other player's"
               " trainer info\n");
        return;
    }
    TrainerInfo_Copy(mine, s_talk_info);
    for (k = 0; k < (size_t)(TRAINER_NAME_LEN + 4); k++)
        buf[k] = MMO_CHAR_EOS;
    mmo_utf8_to_charcode(peer->entity.name, buf, TRAINER_NAME_LEN + 1);
    TrainerInfo_SetName(s_talk_info, buf);
    TrainerInfo_SetGender(s_talk_info, peer->entity.gender ? 1 : 0);
    TrainerInfo_SetID(s_talk_info, s_talk_peer);
}

/* Answered by CommInfo_TrainerInfo above, which sits in front of this block. */
static TrainerInfo *talk_peer_trainer_info(int netId)
{
    return netId == UG_TALK_PEER ? s_talk_info : NULL;
}

static void talk_forget_info(void)
{
    if (s_talk_info == NULL)
        return;
    Heap_Free(s_talk_info);
    s_talk_info = NULL;
}

/* Turn to face them. */
static void talk_face_peer(FieldSystem *fs, const openmmo_event *peer)
{
    int px, pz, dir;

    if (fs == NULL || fs->playerAvatar == NULL)
        return;
    px = PlayerAvatar_GetXPos(fs->playerAvatar);
    pz = PlayerAvatar_GetZPos(fs->playerAvatar);
    for (dir = 0; dir < 4; dir++) {
        if (px + MapObject_GetDxFromDir(dir) != peer->entity.x
            || pz + MapObject_GetDzFromDir(dir) != peer->entity.z)
            continue;
        if (PlayerAvatar_GetFacingDir(fs->playerAvatar) == dir)
            return;
        PlayerAvatar_TryFace(fs->playerAvatar, dir);
        if (s_talk_client != NULL)
            openmmo_client_send_face(s_talk_client, dir);
        return;
    }
}

/* Whether this peer has been greeted since the descent, remembering them if
 * not. See UG_TALK_MET_MAX. */
static int talk_already_met(u32 id)
{
    int i;

    for (i = 0; i < s_talk_met_count; i++) {
        if (s_talk_met[i] == id)
            return 1;
    }
    if (s_talk_met_count < UG_TALK_MET_MAX)
        s_talk_met[s_talk_met_count++] = id;
    return 0;
}

/* Over, whichever end ended it. The menus are the engine's and have already
 * taken themselves down; this drops what is ours. It never resumes the field,
 * every path that closes a menu resumes it in the engine's own exit, and the
 * two paths that do not have a menu resume it themselves. */
static void talk_close(int tell_server)
{
    if (tell_server && s_talk_client != NULL && s_talk_peer != 0)
        openmmo_client_ug_talk_send(s_talk_client, MMO_UG_TALK_END,
                                    (s64)s_talk_peer, NULL, 0);
    talk_close_common();
}

/*
 * The same, for a conversation this client will not have. The server's view of whether
 * somebody can be talked to is a report they sent, so it is a round trip old, and the person
 * who was faced may have opened a menu or started digging in the meantime.
 */
static void talk_refuse(void)
{
    u8 byte = MMO_UG_END_REFUSED;

    if (s_talk_client != NULL && s_talk_peer != 0)
        openmmo_client_ug_talk_send(s_talk_client, MMO_UG_TALK_END,
                                    (s64)s_talk_peer, &byte, 1);
    talk_close_common();
}

static void talk_close_common(void)
{
    if (s_talk_peer != 0)
        printf("openmmo: underground, the conversation with entity %u"
               " ended\n", (unsigned)s_talk_peer);
    s_talk_live = 0;
    s_talk_menus = 0;
    s_talk_told = 0;
    s_talk_wait = 0;
    s_talk_missing = 0;
    s_talk_peer = 0;
    talk_forget_info();
}

/*
 * The server's answer to an A press, turned back into the command the group's own server would
 * have broadcast.
 */
static void talk_result(const mmo_underground_talk *msg)
{
    FieldSystem *fs = pc_lab_field_system();
    CommPlayerManager *man = CommPlayerMan_Get();
    openmmo_event peer;
    TalkEvent ev;
    int result = msg->len > 0 ? msg->data[0] : 0;
    int role = msg->len > 1 ? msg->data[1] : MMO_UG_ROLE_INITIATOR;

    if (result != MMO_UG_TALK_SUCCESS) {
        /* A refusal is only ever sent to the one who pressed A. The official client prints
         * it, pauses and resumes on its own callback. */
        s_talk_wait = 0;
        s_talk_peer = 0;
        ev.result = (u8)result;
        ev.netID = (u8)openmmo_ug_net_id();
        ev.talkTargetNetID = UG_TALK_PEER;
        ug_queue(30, &ev, sizeof ev);
        printf("openmmo: underground, the player at entity %u is %s\n",
               (unsigned)msg->entity_id,
               result == MMO_UG_TALK_BUSY_MINING ? "digging" : "occupied");
        return;
    }
    if (s_talk_live)
        return;

    s_talk_peer = (u32)msg->entity_id;
    if (role == MMO_UG_ROLE_RESPONDER
        && talk_availability() != MMO_UG_STATE_FREE) {
        printf("openmmo: underground, entity %u asked, and this player is"
               " busy\n", (unsigned)s_talk_peer);
        talk_refuse();
        return;
    }
    if (!talk_peer_now(&peer)) {
        /* They were beside us when the press went out and are not on this
         * client's map now. Nothing has opened, so this is a clean refusal of
         * our own rather than a menu with nobody on the other side. */
        printf("openmmo: underground, entity %u is no longer here; the"
               " conversation is dropped\n", (unsigned)s_talk_peer);
        talk_close(1);
        CommPlayerMan_ResumeFieldSystem();
        return;
    }

    s_talk_live = 1;
    s_talk_told = 0;
    s_talk_menus = 0;
    s_talk_missing = 0;
    /* The same timer, now waiting on the menus rather than on the answer: a
     * seat whose command 30 never reaches a screen must not leave a paused
     * field behind it. `UndergroundTalk_IsActive` clears it. */
    s_talk_wait = UG_TALK_WAIT;
    talk_seat_info(fs, &peer);
    if (role == MMO_UG_ROLE_RESPONDER)
        talk_face_peer(fs, &peer);

    /* The official client counts a person met once per wireless session and keeps the tally
     * on the group's net id. Everyone here is net id 1, so the slot is set from
     * what this descent has actually met and the engine's own gate does the
     * rest (`UndergroundPlayer_ProcessTalkEvent`). */
    if (man != NULL)
        man->talkCount[UG_TALK_PEER] = (u8)talk_already_met(s_talk_peer);

    /* Nobody walks off mid-conversation. Already true on the side that pressed
     * A; this is the other one. */
    CommPlayerMan_PauseFieldSystem();

    ev.result = TALK_RESULT_SUCCESS;
    ev.netID = (u8)(role == MMO_UG_ROLE_INITIATOR ? openmmo_ug_net_id()
                                                  : UG_TALK_PEER);
    ev.talkTargetNetID = (u8)(role == MMO_UG_ROLE_INITIATOR
                                  ? UG_TALK_PEER
                                  : openmmo_ug_net_id());
    ug_queue(30, &ev, sizeof ev);
    printf("openmmo: underground, talking to \"%s\" (entity %u); we %s\n",
           peer.entity.name[0] != '\0' ? peer.entity.name : "?",
           (unsigned)s_talk_peer,
           role == MMO_UG_ROLE_INITIATOR ? "asked" : "were asked");
}

/* One frame of the second end of the pipe. Drained after the local queue, so
 * what arrives is delivered on the next frame, the round trip a DS ring
 * would have had, and the same rule the rest of this file works to. */
static void talk_pump(void)
{
    mmo_underground_talk msg;

    if (s_talk_client == NULL)
        return;
    while (openmmo_client_ug_talk_recv(s_talk_client, &msg)) {
        switch (msg.kind) {
        case MMO_UG_TALK_RESULT:
            talk_result(&msg);
            break;
        case MMO_UG_TALK_DATA:
            if (s_talk_live)
                talk_deliver(&msg);
            break;
        case MMO_UG_TALK_END:
            /* Not torn down from here. Saying the peer is gone is the official client's own
             * way out of both menus, they erase their windows, run their exit
             * callbacks and give the field back, and doing it any other way
             * would leave a message box on the screen. */
            if (s_talk_live) {
                s_talk_live = 0;
                s_talk_told = 1;
                printf("openmmo: underground, the other player left the"
                       " conversation\n");
            }
            break;
        default:
            break;
        }
    }
}

/*
 * What this player is, in the answers the group's server would have read off its own
 * CommPlayerMan before it paired anybody.
 */
static int talk_availability(void)
{
    FieldSystem *fs;

    if (!s_pipe || !s_down)
        return -1;
    if (s_talk_live || s_talk_wait > 0)
        return MMO_UG_STATE_BUSY;
    /* Asked before the field is, because the mining game takes the field map
     * down and puts it back while it runs, and it is the one state down here
     * with a line of its own to answer with. */
    if (Mining_IsPlayerMining(0))
        return MMO_UG_STATE_MINING;
    if (UndergroundPlayer_IsAffectedByTrap(0)
        || SecretBases_IsPlayerMidBaseTransition(0)
        || !CommPlayerMan_IsMovementEnabled(0)
        || !CommPlayerMan_IsFieldSystemActive())
        return MMO_UG_STATE_BUSY;
    /* And anything else that has the screen: a bag, a menu, a battle, a
     * transition. The engine has no single word for "busy", and this is the
     * same set openmmo_boot.c holds an A press to on the surface. */
    fs = pc_lab_field_system();
    if (fs == NULL || !FieldSystem_IsRunningFieldMap(fs) || fs->task != NULL
        || FieldSystem_HasChildProcess(fs))
        return MMO_UG_STATE_BUSY;
    return MMO_UG_STATE_FREE;
}

/*
 * The one condition the official client reads that is not reported: whether the target is halfway through a
 * tile.
 */

static void talk_report_availability(void)
{
    int now = talk_availability();
    u8 byte;

    if (now < 0 || now == s_talk_reported || s_talk_client == NULL)
        return;
    byte = (u8)now;
    if (openmmo_client_ug_talk_send(s_talk_client, MMO_UG_TALK_STATE, 0,
                                    &byte, 1) != 0)
        return;
    s_talk_reported = now;
}

/*
 * A press of A on the tile a player is standing on, asked by the patch on
 * `UndergroundMan_ProcessInteractEvent` at the point official asks its own comm slots the same
 * question.
 */
int openmmo_underground_talk_interact(int x, int z)
{
    u32 id;

    if (!s_pipe || !s_down || s_talk_client == NULL)
        return 0;
    if (s_talk_live || s_talk_wait > 0)
        return 0;
    id = openmmo_client_entity_at(s_talk_client, x, z);
    if (id == 0)
        return 0;
    if (openmmo_client_ug_talk_send(s_talk_client, MMO_UG_TALK_REQUEST,
                                    (s64)id, NULL, 0) != 0) {
        printf("openmmo: underground, A on the player at (%d,%d) and the"
               " request would not send\n", x, z);
        return 0;
    }
    s_talk_wait = UG_TALK_WAIT;
    s_talk_peer = id;
    /* The official client stops the sender on the frame the press is accepted, before it
     * knows the answer, and so does this. */
    CommPlayerMan_PauseFieldSystem();
    printf("openmmo: underground, A on the player at (%d,%d), entity %u\n",
           x, z, (unsigned)id);
    return 1;
}

/* The conversation's own frame. Called from the cavern's tick, inside the guard
 * that says the Underground's resources are up. */
static void talk_tick(void)
{
    openmmo_event peer;

    talk_report_availability();

    if (UndergroundTalk_IsActive()) {
        s_talk_menus = 1;
        s_talk_wait = 0;
        /* The peer is paused too, so an absence is a disconnect or a warp this
         * client has not been told about. The official client's own branch closes both
         * windows once they stop being connected. */
        if (s_talk_live) {
            if (talk_peer_now(&peer)) {
                s_talk_missing = 0;
            } else if (++s_talk_missing > UG_TALK_GONE) {
                printf("openmmo: underground, entity %u has gone; closing"
                       " the conversation\n", (unsigned)s_talk_peer);
                s_talk_live = 0;
            }
        }
        return;
    }
    if (s_talk_menus) {
        /* They were up and are not now: somebody said goodbye, or the peer went
         * and the official client's disconnect branch closed both windows. Only the first
         * needs telling. */
        talk_close(!s_talk_told);
        return;
    }
    if (s_talk_wait > 0 && --s_talk_wait == 0) {
        /* Either nothing came back about the press, or a seated conversation
         * never put a menu on the screen. Both leave a paused field that
         * nothing else is going to give back. */
        printf("openmmo: underground, nothing opened for entity %u; the"
               " field is yours again\n", (unsigned)s_talk_peer);
        talk_close(s_talk_live);
        CommPlayerMan_ResumeFieldSystem();
    }
}

/* Whether a net id is somebody. The official client asks this of every one of the eight
 * slots on the cavern's own frame, and both talk menus ask it of the person
 * they are talking to, which is what makes a peer who walks away, warps out
 * or disconnects close both windows the way the cartridge does. */
int openmmo_ug_player_connected(int netId)
{
    if (openmmo_trade_pipe_up())
        return openmmo_trade_player_connected(netId);
    if (!s_pipe)
        return 0;
    if (netId == openmmo_ug_net_id())
        return 1;
    return s_talk_live && netId == UG_TALK_PEER;
}

/* Where the local player is, in the two slots the Underground asks. */
int openmmo_comm_player_is_local_mirror(int netId)
{
    return s_pipe && netId == 0;
}

/* How many frames of the tile step the avatar is on are still to run. The
 * overworld walks the local player down here, so this is the only thing that
 * knows a step is in flight. */
static int s_move_hold;

/*
 * A tile step began (openmmo_boot.c, beside the step report). Sixteen frames a tile walking
 * and eight running, measured off this engine's own overworld, a 272-frame hold on LEFT
 * walks exactly seventeen tiles.
 */
void openmmo_underground_note_step(int running)
{
    s_move_hold = running ? 8 : 16;
}

static void mirror_local_player(FieldSystem *fs)
{
    CommPlayerManager *man = CommPlayerMan_Get();
    int x, z, dir, moving;

    if (man == NULL || fs == NULL || fs->playerAvatar == NULL)
        return;
    x = PlayerAvatar_GetXPos(fs->playerAvatar);
    z = PlayerAvatar_GetZPos(fs->playerAvatar);
    dir = PlayerAvatar_GetFacingDir(fs->playerAvatar);

    /*
     * And whether the step is still in flight. The slot's movement timer is not bookkeeping:
     * it is the Underground's answer to "is this player standing still", and every dwell down
     * there is counted off it.
     */
    /* MapObject_IsMoving answered no for the whole of a walk here, so the step
     * is counted instead, armed where the step is reported and run down a
     * frame at a time, which is the shape the official client's own timer has (set to
     * sub_020581E0(moveSpeed), decremented per frame in CommPlayer_Move). */
    if (s_move_hold > 0)
        s_move_hold--;
    moving = s_move_hold;

    man->moveTimer[0] = (u8)moving;
    man->moveTimerServer[0] = (u8)moving;

    man->playerLocation[0].x = (u16)x;
    man->playerLocation[0].z = (u16)z;
    man->playerLocation[0].dir = (u8)dir;
    man->playerLocationServer[0].x = (u16)x;
    man->playerLocationServer[0].z = (u16)z;
    man->playerLocationServer[0].dir = (u8)dir;
    man->playerAvatar[0] = fs->playerAvatar;
}

/*
 * The two heaps the official descent creates, and the resources that live in them. The official client carves
 * both inside CommManUnderground_InitUnderground, on the way through the wireless boot; the
 * sizes are its own (comm_manager.c: 0xF000, and HEAP_SIZE_UNDERGROUND).
 */
/* How many mining spots the save still holds. An empty slot is three zero
 * bytes (underground.c, Underground_TryAddMiningSpot). */
static int saved_mining_spots(FieldSystem *fs)
{
    Underground *ug = SaveData_GetUnderground(fs->saveData);
    int i, n = 0;

    for (i = 0; i < MAX_MINING_SPOTS; i++) {
        if (Underground_GetMiningSpotXCoordAtIndex(ug, i) != 0
            || Underground_GetMiningSpotZCoordAtIndex(ug, i) != 0)
            n++;
    }
    return n;
}

/* What the cavern buries, and how much of it. */
int openmmo_underground_spawn_iterations(int official)
{
    extern int openmmo_live_peer_count(void);
    int players = openmmo_live_peer_count() + 1;
    int n;

    if (official < 1)
        return official;
    n = official * (players + 2) / 2;
    printf("openmmo: underground, burying for %d player(s): %d rounds where"
           " official runs %d\n", players, n, official);
    return n;
}

/*
 * The save arrives from the server with the Underground block never initialised: no seed,
 * nothing buried, and nothing asking for anything to be.
 */
static void seed_underground_save(FieldSystem *fs)
{
    Underground *ug = SaveData_GetUnderground(fs->saveData);

    if (Underground_GetRandomSeed(ug) != 0)
        return;

    Underground_Init(ug);
    printf("openmmo: underground, the save's cavern was never initialised;"
           " it is a new one now (seed %#x)\n",
           (unsigned)Underground_GetRandomSeed(ug));
}

static int resources_up(FieldSystem *fs)
{
    u32 spare;

    if (s_pipe)
        return 1;

    /*
     * What APPLICATION has left with the field up, which is the budget both heaps below are
     * spent from and the number that decides their sizes.
     */
    spare = HeapExp_FndGetTotalFreeSize(HEAP_ID_APPLICATION);
    if (!Heap_CreateAtEnd(HEAP_ID_APPLICATION, HEAP_ID_COMMUNICATION,
                          UG_COMM_HEAP_SIZE)) {
        printf("openmmo: underground, no room for the communication heap"
               " (%#x wanted, APPLICATION had %#x)\n",
               (unsigned)UG_COMM_HEAP_SIZE, (unsigned)spare);
        return 0;
    }
    if (!Heap_Create(HEAP_ID_APPLICATION, HEAP_ID_UNDERGROUND,
                     UG_TEXT_HEAP_SIZE)) {
        printf("openmmo: underground, no room for the underground heap"
               " (%#x wanted, APPLICATION had %#x)\n",
               (unsigned)UG_TEXT_HEAP_SIZE, (unsigned)spare);
        Heap_Destroy(HEAP_ID_COMMUNICATION);
        return 0;
    }

    seed_underground_save(fs);
    ug_queue_clear();
    /* A fresh trip has greeted nobody and told the server nothing about
     * itself. */
    s_talk_met_count = 0;
    s_talk_reported = -1;
    s_scramble = UG_MOVE_NORMAL;
    s_scramble_key = 0;
    s_scramble_timer = 0;
    /* Set before the table is registered: UndergroundMan_InitAllResources
     * sends on its way up, and a send that is not queued is a state machine
     * that never starts. */
    s_pipe = 1;
    CommFieldCmd_Init(fs);
    UndergroundMan_InitAllResources(fs);

    printf("openmmo: underground, %d mining spot(s) buried\n",
           saved_mining_spots(fs));
    mirror_local_player(fs);
    CommPlayerMan_ResumeFieldSystem();
    printf("openmmo: underground, resources up (mining, spheres, traps,"
           " bases, records, menu); APPLICATION had %#x, comm heap %#x free,"
           " text heap %#x free\n",
           (unsigned)spare,
           (unsigned)HeapExp_FndGetTotalFreeSize(HEAP_ID_COMMUNICATION),
           (unsigned)HeapExp_FndGetTotalFreeSize(HEAP_ID_UNDERGROUND));
    return 1;
}

static void resources_down(void)
{
    if (!s_pipe)
        return;
    /* Before the heap the other player's trainer info was allocated from, and
     * while there is still a session to tell. */
    talk_close(1);
    s_pipe = 0;
    UndergroundMan_FreeAllResources();
    ug_queue_clear();
    Heap_Destroy(HEAP_ID_UNDERGROUND);
    Heap_Destroy(HEAP_ID_COMMUNICATION);
    printf("openmmo: underground, resources down\n");
}

/* ---- Arriving without a descent --------------------------------------- */
static void adopt_underground(FieldSystem *fs)
{
    if (s_down || s_running || fs == NULL || fs->location == NULL)
        return;
    if ((int)fs->location->mapHeaderID != MAP_HEADER_UNDERGROUND)
        return;
    if (fs->playerAvatar == NULL || fs->task != NULL)
        return;
    if (!FieldSystem_IsRunningFieldMap(fs) || FieldSystem_HasChildProcess(fs))
        return;

    printf("openmmo: underground, seated in the cavern without a descent;"
           " bringing it up\n");
    fs->mapLoadType = MAP_LOAD_TYPE_UNDERGROUND;
    /* The overlay is not loaded here the way the descent loads it: by the time
     * a seated arrival is settled the map is already drawing out of it, and
     * asking again is an engine assertion (CanOverlayBeLoaded) for no gain. */
    s_down = 1;
    if (!resources_up(fs)) {
        /* Without them the top screen has nothing to read and the cavern's own
         * frame has nothing to run, so the map is left as a place to stand in
         * and walk out of rather than half-built. */
        printf("openmmo: underground, adopted without its resources;"
               " the cavern is walkable and nothing else\n");
        return;
    }
    if (fs->ugTopScreenCtx == NULL)
        fs->ugTopScreenCtx = UndergroundTopScreen_StartTask(fs);
    BrightnessController_StartTransition(30, 0, -16,
                                         GX_BLEND_PLANEMASK_BG0
                                         | GX_BLEND_PLANEMASK_BG3
                                         | GX_BLEND_PLANEMASK_OBJ,
                                         BRIGHTNESS_SUB_SCREEN);
    SecretBases_SetEntranceGraphicsEnabled(TRUE);
}

/* ---- The connection icon ---------------------------------------------- */
#define UG_PING_GOOD_MS 60
#define UG_PING_FAIR_MS 120
#define UG_PING_POOR_MS 250

static int s_icon_up;
static int s_icon_bars = -1;

int openmmo_network_icon_wanted(void)
{
    return s_icon_up;
}

static int icon_bars_for(int latency_ms)
{
    if (latency_ms < 0)
        return 3;                       /* asked, nothing back yet */
    if (latency_ms <= UG_PING_GOOD_MS)
        return 0;
    if (latency_ms <= UG_PING_FAIR_MS)
        return 1;
    if (latency_ms <= UG_PING_POOR_MS)
        return 2;
    return 3;
}

/* Called every frame from openmmo_mod_frame with the client's own latency.
 * Owns the icon's whole life: it is created the frame the cavern is stood in
 * and destroyed the frame the climb begins, so nothing draws it on a map where
 * the engine would not have. */
void openmmo_underground_tick(int latency_ms)
{
    int bars;

    adopt_underground(pc_lab_field_system());

    if (!s_down) {
        if (s_icon_up) {
            s_icon_up = 0;
            s_icon_bars = -1;
            NetworkIcon_Destroy();
        }
        return;
    }

    {
        /*
         * The cavern's own frame. The official client runs these two out of the field communication
         * manager's task, the half of it that is not the wireless connection machine
         * (underground/comm_manager.c, CheckForConnectionsTask and MainTaskServer both open
         * with them).
         */
        FieldSystem *fs = pc_lab_field_system();

        if (s_pipe) {
            mirror_local_player(fs);
            ug_pump();
            /* The other end of the pipe, drained after this one so what
             * arrives waits a frame the way a received ring's contents do. */
            talk_pump();
            /* Neither of these guards its own manager, and both are the
             * Underground's, so a descent whose resources would not allocate
             * walks a cavern with nothing in it rather than crashing on the
             * first frame. */
            UndergroundMan_Process();
            sub_02059524();
            talk_tick();
        }
    }

    if (!s_icon_up) {
        /* Wanted before Init, because Init asks. */
        s_icon_up = 1;
        s_icon_bars = -1;
        NetworkIcon_Init();
        printf("openmmo: underground, connection icon up\n");
    }

    bars = icon_bars_for(latency_ms);

    /*
     * Set every frame, not only when it moves. The icon is not ours alone: the mining game
     * destroys it on its way in and calls NetworkIcon_Init again on its way out (mining.c,
     * MINING_STATE_FREE and MINING_STATE_FADE_IN_END), and a fresh icon starts at no bars.
     */
    NetworkIcon_SetStrength(bars);
    if (bars != s_icon_bars) {
        s_icon_bars = bars;
        printf("openmmo: underground, link %d ms, %d/4 bars\n",
               latency_ms, 4 - bars);
    }
}

/*
 * The field is going, and we are still down there: a disconnect, a warp the server drove, the
 * title.
 */
void openmmo_underground_field_leaving(void)
{
    if (s_down || s_running || s_pipe || s_icon_up)
        printf("openmmo: underground, the field is leaving with us in it\n");
    s_down = 0;
    s_running = 0;
    resources_down();
    if (s_icon_up) {
        s_icon_up = 0;
        s_icon_bars = -1;
        NetworkIcon_Destroy();
    }
}

/* The late half of the same, kept because it is what openmmo_boot.c calls when
 * the field heaps go and because it costs nothing to say so twice. By the time
 * this runs the FieldSystem has been freed, so it may only drop flags, the
 * hook above is the one that frees. */
void openmmo_underground_forget(void)
{
    if (s_down || s_running || s_pipe)
        printf("openmmo: underground, state dropped with the field heaps\n");
    s_down = 0;
    s_running = 0;
    s_pipe = 0;
    /* Dropped, not freed: the heap the other player's trainer info came from
     * has already ceased with the field's. */
    s_talk_live = 0;
    s_talk_menus = 0;
    s_talk_told = 0;
    s_talk_wait = 0;
    s_talk_missing = 0;
    s_talk_peer = 0;
    s_talk_info = NULL;
    s_talk_reported = -1;
    s_talk_met_count = 0;
    s_icon_up = 0;
    s_icon_bars = -1;
}
