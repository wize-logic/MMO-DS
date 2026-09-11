/* The remote-entity reconcile model. */
#include <stdio.h>
#include <string.h>

#include "entity.h"

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

static const openmmo_entity_appearance kAp = { 0, 0 };

/* Run one tick; return the number of events and copy them into out. */
static int step(openmmo_entity_mgr *m, openmmo_entity_event *out)
{
    return openmmo_entity_tick(m, out, OPENMMO_ENTITY_NETID_CEIL);
}

/* Tick until the next STEP/TURN event on a single-entity manager, or give up
 * after `limit` ticks. Returns the gap in ticks (0 = event on the first tick),
 * or -1 if none arrived. Fills *ev with the event. */
static int next_event(openmmo_entity_mgr *m, openmmo_entity_event *ev, int limit)
{
    openmmo_entity_event out[OPENMMO_ENTITY_NETID_CEIL];
    for (int t = 0; t < limit; t++) {
        int n = step(m, out);
        if (n > 0) {
            *ev = out[0];
            return t;
        }
    }
    return -1;
}

int entity_tests_run(void)
{
    openmmo_entity_mgr m;
    openmmo_entity_event out[OPENMMO_ENTITY_NETID_CEIL];
    openmmo_entity_event ev;
    int n;

    failures = 0;
    printf("remote-entity model:\n");

    printf("the slot allocator packs, rejects duplicates and culls at the cap:\n");
    openmmo_entity_mgr_init(&m, 4);
    CHECK(openmmo_entity_count(&m) == 0, "an empty manager holds nobody");
    CHECK(openmmo_entity_spawn(&m, 100, 5, 5, OPENMMO_DIR_SOUTH, kAp) == 0, "first id lands in slot 0");
    CHECK(openmmo_entity_spawn(&m, 200, 1, 1, OPENMMO_DIR_SOUTH, kAp) == 1, "second id lands in slot 1");
    CHECK(openmmo_entity_spawn(&m, 300, 2, 2, OPENMMO_DIR_SOUTH, kAp) == 2, "third id lands in slot 2");
    CHECK(openmmo_entity_spawn(&m, 100, 9, 9, OPENMMO_DIR_SOUTH, kAp) == -1, "a duplicate id is rejected");
    CHECK(openmmo_entity_slot_of(&m, 200) == 1, "slot_of finds a live id");
    CHECK(openmmo_entity_slot_of(&m, 999) == -1, "slot_of returns -1 for an absent id");
    CHECK(openmmo_entity_count(&m) == 3, "three ids are live");
    CHECK(openmmo_entity_spawn(&m, 400, 3, 3, OPENMMO_DIR_SOUTH, kAp) == 3, "the cap-th id fills the table");
    CHECK(openmmo_entity_spawn(&m, 500, 4, 4, OPENMMO_DIR_SOUTH, kAp) == -1, "a spawn past the cap is culled");
    CHECK(openmmo_entity_count(&m) == 4, "the cull did not grow the table");

    printf("max_visible is clamped to the engine's 8-slot ceiling:\n");
    openmmo_entity_mgr_init(&m, 100);
    CHECK(m.cap == OPENMMO_ENTITY_NETID_CEIL, "an over-budget request clamps to 8");
    openmmo_entity_mgr_init(&m, 0);
    CHECK(m.cap == 1, "a zero request clamps up to 1");

    printf("spawn emits one SPAWN event in the 0x100+slot local-id band:\n");
    openmmo_entity_mgr_init(&m, 2);
    openmmo_entity_appearance ap = { 1, 0, "Wanderer" };
    openmmo_entity_spawn(&m, 7, 5, 5, OPENMMO_DIR_EAST, ap);
    n = step(&m, out);
    CHECK(n == 1 && out[0].kind == OPENMMO_ENTITY_EV_SPAWN, "the first tick spawns the avatar");
    CHECK(out[0].x == 5 && out[0].z == 5 && out[0].dir == OPENMMO_DIR_EAST, "it spawns at the given tile and heading");
    CHECK(out[0].localid == OPENMMO_ENTITY_LOCALID_BASE + out[0].slot, "its local id is 0x100 + slot");
    CHECK(out[0].localid >= OPENMMO_ENTITY_LOCALID_BASE, "its local id sits above the map-object band");
    CHECK(out[0].appearance.gender == 1, "the spawn carries the appearance");
    /* The name is what the host draws over the avatar's head, so it has to
     * survive the slot the model packs the entity into. */
    CHECK(strcmp(out[0].appearance.name, "Wanderer") == 0,
          "the spawn carries the peer's name");
    CHECK(step(&m, out) == 0, "a settled entity emits nothing further");

    printf("a straight walk steps one tile at a time toward the target, facing it:\n");
    openmmo_entity_mgr_init(&m, 1);
    openmmo_entity_spawn(&m, 1, 5, 5, OPENMMO_DIR_EAST, kAp);
    step(&m, out); /* consume SPAWN */
    /* speed 0 -> 2-frame cadence: a step, then one idle tick, then a step. */
    openmmo_entity_set_target(&m, 1, 8, 5, OPENMMO_DIR_EAST, 0);
    int walk_x[3];
    int gaps_ok = 1;
    for (int i = 0; i < 3; i++) {
        int gap = next_event(&m, &ev, 8);
        walk_x[i] = (gap >= 0) ? ev.x : -1;
        if (ev.kind != OPENMMO_ENTITY_EV_STEP || ev.dir != OPENMMO_DIR_EAST) {
            gaps_ok = 0;
        }
        if (i > 0 && gap != 1) {
            gaps_ok = 0; /* one idle tick between steps at speed 0 */
        }
    }
    CHECK(walk_x[0] == 6 && walk_x[1] == 7 && walk_x[2] == 8, "the rendered tile walks 6,7,8 toward x=8");
    CHECK(gaps_ok, "each step heads east and honours the cadence gap");
    CHECK(next_event(&m, &ev, 4) == -1, "no further events once the target is reached");

    printf("the cadence table gates by move speed (speed 2 -> 8 frames per tile):\n");
    openmmo_entity_mgr_init(&m, 1);
    openmmo_entity_spawn(&m, 1, 0, 0, OPENMMO_DIR_EAST, kAp);
    step(&m, out); /* consume SPAWN */
    /* One tile at a time, which is what a walking peer's steps look like as
     * they arrive: a target further out is the catching-up case below and is
     * deliberately not paced at a walk. */
    openmmo_entity_set_target(&m, 1, 1, 0, OPENMMO_DIR_EAST, 2);
    next_event(&m, &ev, 16); /* first step */
    openmmo_entity_set_target(&m, 1, 2, 0, OPENMMO_DIR_EAST, 2);
    int gap = next_event(&m, &ev, 16); /* second step */
    CHECK(gap == 7, "speed 2 leaves 7 idle ticks between steps (period 8)");

    printf("the dominant axis wins, ties go to z, and it turns on arrival:\n");
    openmmo_entity_mgr_init(&m, 1);
    openmmo_entity_spawn(&m, 1, 0, 0, OPENMMO_DIR_SOUTH, kAp);
    step(&m, out); /* consume SPAWN */
    openmmo_entity_set_target(&m, 1, 2, 2, OPENMMO_DIR_NORTH, 0);
    int dirs[4];
    int last_x = -1, last_z = -1;
    for (int i = 0; i < 4; i++) {
        next_event(&m, &ev, 8);
        dirs[i] = ev.dir;
        last_x = ev.x;
        last_z = ev.z;
    }
    CHECK(dirs[0] == OPENMMO_DIR_SOUTH, "a tie steps the z axis first (south)");
    CHECK(dirs[1] == OPENMMO_DIR_EAST, "then the larger x delta (east)");
    CHECK(dirs[2] == OPENMMO_DIR_SOUTH, "then the tie again (south)");
    CHECK(dirs[3] == OPENMMO_DIR_EAST, "then the last x step (east)");
    CHECK(last_x == 2 && last_z == 2, "it arrives at the target tile");
    int gap2 = next_event(&m, &ev, 8);
    CHECK(gap2 >= 0 && ev.kind == OPENMMO_ENTITY_EV_TURN && ev.dir == OPENMMO_DIR_NORTH,
        "on arrival it turns to the server's facing");

    printf("a same-tile target turns in place without moving:\n");
    openmmo_entity_mgr_init(&m, 1);
    openmmo_entity_spawn(&m, 1, 5, 5, OPENMMO_DIR_NORTH, kAp);
    step(&m, out); /* consume SPAWN */
    openmmo_entity_set_target(&m, 1, 5, 5, OPENMMO_DIR_WEST, 0);
    n = step(&m, out);
    CHECK(n == 1 && out[0].kind == OPENMMO_ENTITY_EV_TURN, "the settled entity turns");
    CHECK(out[0].x == 5 && out[0].z == 5 && out[0].dir == OPENMMO_DIR_WEST, "it stays put and faces west");

    printf("face_toward resolves the -1 turn against the player's tile:\n");
    openmmo_entity_mgr_init(&m, 1);
    openmmo_entity_spawn(&m, 1, 5, 5, OPENMMO_DIR_NORTH, kAp);
    step(&m, out); /* consume SPAWN */
    CHECK(openmmo_entity_face_toward(&m, 1, 9, 5) == 0
              && step(&m, out) == 1 && out[0].dir == OPENMMO_DIR_EAST,
          "a player to the east is faced east");
    CHECK(openmmo_entity_face_toward(&m, 1, 2, 5) == 0
              && step(&m, out) == 1 && out[0].dir == OPENMMO_DIR_WEST,
          "a player to the west is faced west");
    CHECK(openmmo_entity_face_toward(&m, 1, 5, 9) == 0
              && step(&m, out) == 1 && out[0].dir == OPENMMO_DIR_SOUTH,
          "a player below on the same column is faced south");
    CHECK(openmmo_entity_face_toward(&m, 1, 5, 1) == 0
              && step(&m, out) == 1 && out[0].dir == OPENMMO_DIR_NORTH,
          "a player above on the same column is faced north");
    CHECK(openmmo_entity_face_toward(&m, 1, 5, 5) == 0
              && step(&m, out) == 1 && out[0].dir == OPENMMO_DIR_SOUTH,
          "a player on the same tile is faced south");
    CHECK(openmmo_entity_face_toward(&m, 999, 0, 0) == -1,
          "a turn for an absent id is rejected");

    printf("despawn frees the slot and a later spawn reuses it:\n");
    openmmo_entity_mgr_init(&m, 2);
    openmmo_entity_spawn(&m, 1, 0, 0, OPENMMO_DIR_SOUTH, kAp);
    openmmo_entity_spawn(&m, 2, 1, 1, OPENMMO_DIR_SOUTH, kAp);
    step(&m, out); /* consume the two SPAWNs */
    CHECK(openmmo_entity_despawn(&m, 1) == 0, "despawn of a live id succeeds");
    n = step(&m, out);
    int saw_despawn = 0;
    for (int i = 0; i < n; i++) {
        if (out[i].kind == OPENMMO_ENTITY_EV_DESPAWN && out[i].slot == 0) {
            saw_despawn = 1;
        }
    }
    CHECK(saw_despawn, "the next tick emits a DESPAWN for the freed slot");
    CHECK(openmmo_entity_count(&m) == 1, "the manager drops to one live entity");
    CHECK(openmmo_entity_slot_of(&m, 1) == -1, "the despawned id is gone");
    CHECK(openmmo_entity_spawn(&m, 3, 4, 4, OPENMMO_DIR_SOUTH, kAp) == 0, "a new id reuses slot 0");

    printf("moves and despawns for unknown ids fail rather than inventing state:\n");
    CHECK(openmmo_entity_set_target(&m, 999, 1, 1, OPENMMO_DIR_SOUTH, 0) == -1, "a move for an absent id is rejected");
    CHECK(openmmo_entity_despawn(&m, 999) == -1, "a despawn of an absent id is rejected");

    printf("a map crossing resets the model so the new map's ids spawn clean:\n");
    openmmo_entity_mgr_init(&m, 4);
    openmmo_entity_spawn(&m, 10, 0, 0, OPENMMO_DIR_SOUTH, kAp);
    openmmo_entity_spawn(&m, 11, 1, 1, OPENMMO_DIR_SOUTH, kAp);
    step(&m, out); /* consume the two SPAWNs */
    CHECK(openmmo_entity_count(&m) == 2, "two entities are live before the crossing");
    /* Without a reset the server's re-send of an id already in the model is
     * rejected as a duplicate, the very collision the map-change reset avoids. */
    CHECK(openmmo_entity_spawn(&m, 10, 5, 5, OPENMMO_DIR_NORTH, kAp) == -1,
        "re-spawning a still-tracked id is rejected");
    openmmo_entity_reset(&m);
    CHECK(openmmo_entity_count(&m) == 0, "reset drops every entity at once");
    n = step(&m, out);
    CHECK(n == 0, "reset emits no events, the avatars left with the old table");
    CHECK(openmmo_entity_slot_of(&m, 10) == -1 && openmmo_entity_slot_of(&m, 11) == -1,
        "both old-map ids are gone after the reset");
    CHECK(openmmo_entity_spawn(&m, 10, 5, 5, OPENMMO_DIR_NORTH, kAp) == 0,
        "the same id re-spawns cleanly on the new map");
    n = step(&m, out);
    CHECK(n == 1 && out[0].kind == OPENMMO_ENTITY_EV_SPAWN && out[0].x == 5 && out[0].z == 5,
        "and it spawns fresh at the new-map tile");

    printf("a snapshot names every live slot at its current tile:\n");
    {
        openmmo_entity_appearance named = kAp;

        snprintf(named.name, sizeof named.name, "Dawn");
        named.has_body = 1;
        named.gfx = 20;
        openmmo_entity_mgr_init(&m, 4);
        CHECK(openmmo_entity_spawn(&m, 10, 4, 6, OPENMMO_DIR_WEST, named) == 0,
            "a named body lands in slot 0");
        CHECK(openmmo_entity_spawn(&m, 11, 5, 7, OPENMMO_DIR_SOUTH, kAp) == 1,
            "a second id lands in slot 1");
        step(&m, out); /* consume the two SPAWNs so pending_spawn is clear */
        n = openmmo_entity_snapshot(&m, out, OPENMMO_ENTITY_NETID_CEIL);
        CHECK(n == 2, "two live slots snapshot");
        CHECK(out[0].kind == OPENMMO_ENTITY_EV_SPAWN && out[0].x == 4 && out[0].z == 6
              && out[0].dir == OPENMMO_DIR_WEST,
            "slot 0 snapshots at the tile it was spawned on");
        CHECK(strcmp(out[0].appearance.name, "Dawn") == 0
              && out[0].appearance.has_body == 1 && out[0].appearance.gfx == 20,
            "the snapshot carries the name and the catalog body");
        CHECK(out[1].slot == 1 && out[1].x == 5 && out[1].z == 7,
            "slot 1 snapshots at its own tile");
        CHECK(openmmo_entity_despawn(&m, 10) == 0, "mark slot 0 to leave");
        n = openmmo_entity_snapshot(&m, out, OPENMMO_ENTITY_NETID_CEIL);
        CHECK(n == 1 && out[0].slot == 1,
            "a slot marked to leave is not in the snapshot");
    }

    printf("a peer whose tiles outrun a walk is stepped and drawn as running:\n");
    {
        int t, steps = 0, ran = 0, walked = 0;

        openmmo_entity_mgr_init(&m, 4);
        openmmo_entity_spawn(&m, 1, 0, 0, OPENMMO_DIR_EAST, kAp);
        step(&m, out); /* the SPAWN */

        /* One tile owed is a walking peer and stays a walk. */
        openmmo_entity_set_target(&m, 1, 1, 0, OPENMMO_DIR_EAST, -1);
        CHECK(next_event(&m, &ev, 4) == 0
              && ev.kind == OPENMMO_ENTITY_EV_STEP
              && ev.speed == OPENMMO_ENTITY_SPEED_WALK,
            "one tile behind is a walk");
        CHECK(next_event(&m, &ev, 8) == -1,
            "and the walk's eight frames are still gated");

        /* Two tiles owed is a peer moving faster than a walk draws them: the
         * step is timed and reported at the run cadence, so the gap closes. */
        openmmo_entity_set_target(&m, 1, 3, 0, OPENMMO_DIR_EAST, -1);
        CHECK(next_event(&m, &ev, 8) >= 0
              && ev.kind == OPENMMO_ENTITY_EV_STEP
              && ev.speed == OPENMMO_ENTITY_SPEED_RUN,
            "two tiles behind runs");
        CHECK(next_event(&m, &ev, 4) == 3,
            "a run leaves 3 idle ticks between steps (period 4)");

        /* A peer given a running pace at the wire's own word is not slowed to
         * a walk once it has caught up, and one given a faster pace than a run
         * keeps it. */
        openmmo_entity_mgr_init(&m, 4);
        openmmo_entity_spawn(&m, 2, 0, 0, OPENMMO_DIR_EAST, kAp);
        step(&m, out);
        openmmo_entity_set_target(&m, 2, 4, 0, OPENMMO_DIR_EAST,
                                  OPENMMO_ENTITY_SPEED_FASTEST);
        for (t = 0; t < 40 && steps < 4; t++) {
            n = step(&m, out);
            if (n > 0 && out[0].kind == OPENMMO_ENTITY_EV_STEP) {
                steps++;
                if (out[0].speed == OPENMMO_ENTITY_SPEED_FASTEST) ran++;
                if (out[0].speed == OPENMMO_ENTITY_SPEED_WALK) walked++;
            }
        }
        CHECK(steps == 4 && ran == 4 && walked == 0,
            "a pace the wire called faster than a run is left alone");
    }

    printf("a whole pose is set down, not walked to:\n");
    {
        openmmo_entity_mgr_init(&m, 4);
        openmmo_entity_spawn(&m, 1, 2, 2, OPENMMO_DIR_SOUTH, kAp);
        step(&m, out);
        CHECK(openmmo_entity_place(&m, 1, 20, 30, OPENMMO_DIR_WEST) == 0,
            "a pose lands on a live slot");
        n = step(&m, out);
        CHECK(n == 1 && out[0].kind == OPENMMO_ENTITY_EV_PLACE
              && out[0].x == 20 && out[0].z == 30
              && out[0].dir == OPENMMO_DIR_WEST,
            "the next tick places the peer on the tile, facing the pose");
        CHECK(step(&m, out) == 0, "and nothing follows it");

        /* A pose that beats the spawn out is simply where the spawn happens. */
        openmmo_entity_mgr_init(&m, 4);
        openmmo_entity_spawn(&m, 1, 2, 2, OPENMMO_DIR_SOUTH, kAp);
        openmmo_entity_place(&m, 1, 9, 9, OPENMMO_DIR_NORTH);
        n = step(&m, out);
        CHECK(n == 1 && out[0].kind == OPENMMO_ENTITY_EV_SPAWN
              && out[0].x == 9 && out[0].z == 9,
            "a pose before the spawn moves the spawn");

        /* A step target further out than any burst of steps explains is set
         * down rather than paraded across the map. */
        openmmo_entity_mgr_init(&m, 4);
        openmmo_entity_spawn(&m, 1, 0, 0, OPENMMO_DIR_SOUTH, kAp);
        step(&m, out);
        openmmo_entity_set_target(&m, 1, 40, 0, OPENMMO_DIR_EAST, -1);
        CHECK(next_event(&m, &ev, 4) == 0
              && ev.kind == OPENMMO_ENTITY_EV_PLACE && ev.x == 40,
            "a target forty tiles out is placed");

        /* ...and one a run of steps does explain is still walked. */
        openmmo_entity_mgr_init(&m, 4);
        openmmo_entity_spawn(&m, 1, 0, 0, OPENMMO_DIR_SOUTH, kAp);
        step(&m, out);
        openmmo_entity_set_target(&m, 1, 6, 0, OPENMMO_DIR_EAST, -1);
        CHECK(next_event(&m, &ev, 4) == 0
              && ev.kind == OPENMMO_ENTITY_EV_STEP && ev.x == 1,
            "a target six tiles out is walked");
    }

    if (failures) {
        printf("entity: %d check(s) FAILED\n", failures);
    } else {
        printf("entity: all checks passed\n");
    }
    return failures;
}
