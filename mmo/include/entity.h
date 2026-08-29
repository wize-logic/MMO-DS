#ifndef OPENMMO_ENTITY_H
#define OPENMMO_ENTITY_H
/* The model for other players walking around the shared world. */

#include <stdint.h>

#include "game.h"

/* Facing directions, mirror the engine's DIR_* (constants/map_object.h:5). */
enum {
    OPENMMO_DIR_NORTH = 0,
    OPENMMO_DIR_SOUTH = 1,
    OPENMMO_DIR_WEST = 2,
    OPENMMO_DIR_EAST = 3,
    OPENMMO_DIR_NONE = -1,
};

/* Ceilings measured in the engine tree (settled §8/§9). */
#define OPENMMO_ENTITY_NETID_CEIL 12
#define OPENMMO_ENTITY_LOCALID_BASE 0x100 /* 0xff + slot + 1, the remote-avatar local-id band */

/* Bytes kept for a peer's display name, terminator included. */
#define OPENMMO_ENTITY_NAME_MAX 48

/* Per-entity appearance the glue draws from. Gender picks one of the two
 * trainer models; has_body/gfx seat an object-event sprite on top when the
 * kept SkinSet named one. The SkinSet itself is carried so a later screen
 * can still read the cosmetics the body did not consume. */
typedef struct {
    uint8_t gender;  /* passed to PlayerAvatar_New as TrainerInfo_Gender would be */
    uint8_t version; /* 0 or 1, the game-version split CommPlayer_Add derives */
    /*
     * The peer's display name, exactly as LoadEntity carried it: the codec keeps the server's
     * UTF-16 name as the low byte of each code unit (codec.c, mmo_get_utf16_nt), so this is
     * Latin-1 and NUL-terminated.
     */
    char name[OPENMMO_ENTITY_NAME_MAX];
    uint8_t has_body; /* 1 when gfx is an explicit catalog body, not the gender default */
    int     gfx;      /* object-event graphics id to seat; meaningful when has_body */
    mmo_skin_set skins; /* kept verbatim from LoadEntity */
} openmmo_entity_appearance;

/* What the model asks the glue to do to a real avatar this frame. */
typedef enum {
    OPENMMO_ENTITY_EV_SPAWN = 1, /* create the avatar at (x,z) facing dir */
    OPENMMO_ENTITY_EV_STEP,      /* commit one tile: (x,z) is the new tile, dir the heading */
    OPENMMO_ENTITY_EV_TURN,      /* face dir without moving */
    OPENMMO_ENTITY_EV_DESPAWN,   /* delete the avatar */
} openmmo_entity_ev_kind;

typedef struct {
    int kind;        /* openmmo_entity_ev_kind */
    int slot;        /* 0 .. cap-1 */
    int localid;     /* OPENMMO_ENTITY_LOCALID_BASE + slot */
    int x, z;        /* tile the event refers to */
    int dir;         /* OPENMMO_DIR_* */
    openmmo_entity_appearance appearance; /* meaningful on SPAWN */
} openmmo_entity_event;

typedef struct {
    uint32_t id;      /* server entity id occupying this slot */
    int used;         /* 0 free, 1 live */
    int pending_spawn;
    int pending_despawn;
    int rx, rz, rdir; /* rendered (client-visible) tile */
    int tx, tz, tdir; /* server-target tile */
    int speed;        /* 0..4, index into the cadence table */
    int move_timer;   /* frames until the next step is allowed */
    openmmo_entity_appearance appearance;
} openmmo_entity_slot;

typedef struct {
    int cap; /* live slot ceiling: min(max_visible, OPENMMO_ENTITY_NETID_CEIL) */
    openmmo_entity_slot slots[OPENMMO_ENTITY_NETID_CEIL];
} openmmo_entity_mgr;

/* Reset the manager. max_visible is the caller's texture budget (2.5.9); it is
 * clamped to [1, OPENMMO_ENTITY_NETID_CEIL]. */
void openmmo_entity_mgr_init(openmmo_entity_mgr *m, int max_visible);

/* Register a newly-seen entity at (x,z) facing dir. Returns its slot, or -1 if
 * the id is already present or the table is full (a full table is culled loudly
 * to stderr). The rendered and target tiles both start at (x,z,dir); a SPAWN
 * event is emitted on the next _tick(). */
int openmmo_entity_spawn(openmmo_entity_mgr *m, uint32_t id, int x, int z, int dir,
                         openmmo_entity_appearance ap);

/* Update an entity's server-target tile and move speed (a GbaMove). Returns 0,
 * or -1 if no slot holds id, the model never auto-spawns from a move. */
int openmmo_entity_set_target(openmmo_entity_mgr *m, uint32_t id, int x, int z, int dir,
                              int speed);

/* Turn an entity in place to face dir, without moving it (an EntityFaceTurn).
 * Leaves the target tile untouched and only updates the target facing, so the
 * next _tick() emits a TURN once the rendered tile has caught up to the target.
 * Returns 0, or -1 if no slot holds id. */
int openmmo_entity_face(openmmo_entity_mgr *m, uint32_t id, int dir);

/* Turn an entity in place to look at the tile (px,pz), what an EntityFaceTurn
 * carrying -1 asks for. Same slot rules as openmmo_entity_face. */
int openmmo_entity_face_toward(openmmo_entity_mgr *m, uint32_t id, int px, int pz);

/* Mark an entity for removal. Returns 0, or -1 if no slot holds id. A DESPAWN
 * event is emitted and the slot freed on the next _tick(). */
int openmmo_entity_despawn(openmmo_entity_mgr *m, uint32_t id);

/* Drop every entity at once, without emitting any events. */
void openmmo_entity_reset(openmmo_entity_mgr *m);

/* Advance every live entity one frame: emit pending spawns and despawns, then
 * step each rendered tile toward its target. Writes up to max_out events into
 * out (at most one per slot) and returns the count. */
int openmmo_entity_tick(openmmo_entity_mgr *m, openmmo_entity_event *out, int max_out);

/* The live table as SPAWN-shaped events at each slot's current rendered tile
 * and appearance, without advancing timers or consuming pending_spawn. A slot
 * marked to leave is omitted. The fused glue uses this to seat peers whose
 * spawn event arrived before the field was safe to touch. */
int openmmo_entity_snapshot(const openmmo_entity_mgr *m, openmmo_entity_event *out,
                            int max_out);

/* Number of live slots. */
int openmmo_entity_count(const openmmo_entity_mgr *m);

/* Slot holding id, or -1. */
int openmmo_entity_slot_of(const openmmo_entity_mgr *m, uint32_t id);

#endif /* OPENMMO_ENTITY_H */
