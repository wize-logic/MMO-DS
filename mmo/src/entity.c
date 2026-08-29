/* Remote-entity reconcile, reimplemented from the engine's
 * CommPlayer_MoveClient over integer tiles. See entity.h for the why and the
 * source citations. No engine or transport dependency lives here. */
#include "entity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-direction tile deltas, mirroring map_object_move.c:818/832. */
static const int kDx[4] = { 0, 0, -1, 1 }; /* NORTH, SOUTH, WEST, EAST */
static const int kDz[4] = { -1, 1, 0, 0 };

/* Frames per tile by move speed, mirroring sub_020581E0 (comm_player_manager.c:677). */
static const int kSpeedFrames[5] = { 2, 4, 8, 16, 2 };

#define DEFAULT_SPEED 2 /* CommPlayerLocation.moveSpeed init value (:92) */

static openmmo_entity_slot *find(openmmo_entity_mgr *m, uint32_t id)
{
    for (int i = 0; i < m->cap; i++) {
        if (m->slots[i].used && m->slots[i].id == id) {
            return &m->slots[i];
        }
    }
    return NULL;
}

void openmmo_entity_mgr_init(openmmo_entity_mgr *m, int max_visible)
{
    memset(m, 0, sizeof(*m));
    if (max_visible < 1) {
        max_visible = 1;
    }
    if (max_visible > OPENMMO_ENTITY_NETID_CEIL) {
        max_visible = OPENMMO_ENTITY_NETID_CEIL;
    }
    m->cap = max_visible;
}

int openmmo_entity_spawn(openmmo_entity_mgr *m, uint32_t id, int x, int z, int dir,
                         openmmo_entity_appearance ap)
{
    if (find(m, id) != NULL) {
        return -1;
    }
    for (int i = 0; i < m->cap; i++) {
        openmmo_entity_slot *s = &m->slots[i];
        if (s->used) {
            continue;
        }
        memset(s, 0, sizeof(*s));
        s->used = 1;
        s->pending_spawn = 1;
        s->id = id;
        s->rx = s->tx = x;
        s->rz = s->tz = z;
        s->rdir = s->tdir = dir;
        s->speed = DEFAULT_SPEED;
        s->appearance = ap;
        return i;
    }
    fprintf(stderr, "openmmo: entity table full (cap %d), culling id %u\n", m->cap, id);
    return -1;
}

int openmmo_entity_set_target(openmmo_entity_mgr *m, uint32_t id, int x, int z, int dir,
                              int speed)
{
    openmmo_entity_slot *s = find(m, id);
    if (s == NULL) {
        return -1;
    }
    s->tx = x;
    s->tz = z;
    s->tdir = dir;
    if (speed < 0 || speed > 4) {
        speed = DEFAULT_SPEED;
    }
    s->speed = speed;
    return 0;
}

int openmmo_entity_face(openmmo_entity_mgr *m, uint32_t id, int dir)
{
    openmmo_entity_slot *s = find(m, id);
    if (s == NULL) {
        return -1;
    }
    s->tdir = dir;
    return 0;
}

int openmmo_entity_face_toward(openmmo_entity_mgr *m, uint32_t id, int px, int pz)
{
    openmmo_entity_slot *s = find(m, id);
    if (s == NULL) {
        return -1;
    }
    /* The game client resolves this the same way: a column difference wins over
     * a row difference, and an entity on the player's own column faces up only
     * when the player is above it. */
    if (s->tx < px) {
        s->tdir = OPENMMO_DIR_EAST;
    } else if (s->tx > px) {
        s->tdir = OPENMMO_DIR_WEST;
    } else {
        s->tdir = (s->tz > pz) ? OPENMMO_DIR_NORTH : OPENMMO_DIR_SOUTH;
    }
    return 0;
}

int openmmo_entity_despawn(openmmo_entity_mgr *m, uint32_t id)
{
    openmmo_entity_slot *s = find(m, id);
    if (s == NULL) {
        return -1;
    }
    s->pending_despawn = 1;
    return 0;
}

void openmmo_entity_reset(openmmo_entity_mgr *m)
{
    memset(m->slots, 0, sizeof(m->slots));
}

/* One reconcile step for a live, spawned slot. Returns the event kind (STEP or
 * TURN) or 0 for no change, filling *dir/*x/*z with the result. */
static int reconcile(openmmo_entity_slot *s, int *dir, int *x, int *z)
{
    if (s->move_timer != 0) {
        s->move_timer--;
        return 0;
    }
    int dx = s->rx - s->tx;
    int dz = s->rz - s->tz;
    if (dx == 0 && dz == 0) {
        if (s->tdir != OPENMMO_DIR_NONE && s->rdir != s->tdir) {
            s->rdir = s->tdir;
            *dir = s->rdir;
            *x = s->rx;
            *z = s->rz;
            return OPENMMO_ENTITY_EV_TURN;
        }
        return 0;
    }
    int step;
    if (abs(dx) > abs(dz)) {
        step = (dx > 0) ? OPENMMO_DIR_WEST : OPENMMO_DIR_EAST;
    } else {
        step = (dz > 0) ? OPENMMO_DIR_NORTH : OPENMMO_DIR_SOUTH;
    }
    s->rx += kDx[step];
    s->rz += kDz[step];
    s->rdir = step;
    s->move_timer = kSpeedFrames[s->speed];
    if (s->move_timer != 0) {
        s->move_timer--;
    }
    *dir = step;
    *x = s->rx;
    *z = s->rz;
    return OPENMMO_ENTITY_EV_STEP;
}

int openmmo_entity_tick(openmmo_entity_mgr *m, openmmo_entity_event *out, int max_out)
{
    int n = 0;
    for (int i = 0; i < m->cap; i++) {
        openmmo_entity_slot *s = &m->slots[i];
        if (!s->used) {
            continue;
        }
        if (n >= max_out) {
            break;
        }
        openmmo_entity_event *e = &out[n];
        e->slot = i;
        e->localid = OPENMMO_ENTITY_LOCALID_BASE + i;
        e->appearance = s->appearance;

        if (s->pending_despawn) {
            e->kind = OPENMMO_ENTITY_EV_DESPAWN;
            e->x = s->rx;
            e->z = s->rz;
            e->dir = s->rdir;
            memset(s, 0, sizeof(*s));
            n++;
            continue;
        }
        if (s->pending_spawn) {
            s->pending_spawn = 0;
            e->kind = OPENMMO_ENTITY_EV_SPAWN;
            e->x = s->rx;
            e->z = s->rz;
            e->dir = s->rdir;
            n++;
            continue;
        }
        int dir, x, z;
        int kind = reconcile(s, &dir, &x, &z);
        if (kind != 0) {
            e->kind = kind;
            e->x = x;
            e->z = z;
            e->dir = dir;
            n++;
        }
    }
    return n;
}

int openmmo_entity_snapshot(const openmmo_entity_mgr *m, openmmo_entity_event *out,
                            int max_out)
{
    int n = 0;

    if (m == NULL || out == NULL || max_out <= 0) {
        return 0;
    }
    for (int i = 0; i < m->cap && n < max_out; i++) {
        const openmmo_entity_slot *s = &m->slots[i];

        if (!s->used || s->pending_despawn) {
            continue;
        }
        out[n].kind = OPENMMO_ENTITY_EV_SPAWN;
        out[n].slot = i;
        out[n].localid = OPENMMO_ENTITY_LOCALID_BASE + i;
        out[n].x = s->rx;
        out[n].z = s->rz;
        out[n].dir = s->rdir;
        out[n].appearance = s->appearance;
        n++;
    }
    return n;
}

int openmmo_entity_count(const openmmo_entity_mgr *m)
{
    int c = 0;
    for (int i = 0; i < m->cap; i++) {
        if (m->slots[i].used) {
            c++;
        }
    }
    return c;
}

int openmmo_entity_slot_of(const openmmo_entity_mgr *m, uint32_t id)
{
    for (int i = 0; i < m->cap; i++) {
        if (m->slots[i].used && m->slots[i].id == id) {
            return i;
        }
    }
    return -1;
}
