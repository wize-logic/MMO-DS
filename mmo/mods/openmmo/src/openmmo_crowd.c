/*
 * How many people the overworld may hold, and what happens when it is asked
 * for one more than that.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../include/endpoint.h"
#include "../../../include/platform.h"

/* What this build uses. The engine's own numbers are 64, 32 and 80. */
#define OPENMMO_MAP_OBJECTS_DEFAULT 80
#define OPENMMO_TEXTURE_SLOTS_DEFAULT 32
#define OPENMMO_OVERWORLD_ANIMS_DEFAULT 88

/*
 * A failed engine assertion is reported and counted; OPENMMO_ASSERT=fatal stops on the first
 * one instead.
 */
enum {
    ASSERT_FATAL = 1,
    ASSERT_WARN = 0,
};

/* A ceiling out of the environment. Every caller of this is a door, the
 * three below resize the engine's own map-object, texture and animation
 * tables, so the read goes through openmmo_dev_env (endpoint.h) and a
 * release keeps the numbers it was built with whatever the environment says. */
static int env_int(const char *name, int fallback, int lo, int hi)
{
    const char *v = openmmo_dev_env(name);
    char *end;
    long n;

    if (v == NULL || v[0] == '\0') {
        return fallback;
    }

    n = strtol(v, &end, 0);
    if (*end != '\0' || n < lo || n > hi) {
        fprintf(stderr,
                "openmmo: %s=%s is not a count in [%d,%d]; using %d\n",
                name, v, lo, hi, fallback);
        fflush(stderr);
        return fallback;
    }

    return (int)n;
}

/*
 * How many map objects one map may hold at once: the local player, every scripted NPC the map
 * file lists, and every remote player drawn on it.
 */
int openmmo_map_object_capacity(void)
{
    static int cached;

    if (cached == 0) {
        cached = env_int("OPENMMO_MAP_OBJECTS", OPENMMO_MAP_OBJECTS_DEFAULT, 16, 1024);
    }

    return cached;
}

/*
 * How many distinct overworld appearances may be resident. Keyed by graphics id, not by
 * object: a crowd wearing one of two trainer models costs two slots however many people are
 * standing in it (ov5_021ED334 returns early when the id is already loaded).
 */
int openmmo_texture_slot_capacity(void)
{
    static int cached;

    if (cached == 0) {
        cached = env_int("OPENMMO_TEXTURE_SLOTS", OPENMMO_TEXTURE_SLOTS_DEFAULT, 4, 256);
    }

    return cached;
}

/*
 * How many overworld animation managers a map may have running. Every map object that is
 * animating, which is every remote player who is walking, holds one, and
 * FieldEffectManager_InitAnimManager asserts on the NULL it gets when the list is empty.
 */
int openmmo_overworld_anim_capacity(void)
{
    static int cached;

    if (cached == 0) {
        cached = env_int("OPENMMO_OVERWORLD_ANIMS", OPENMMO_OVERWORLD_ANIMS_DEFAULT, 16, 512);
    }

    return cached;
}

/* Shared by the traps below. Frame 0 is this function, frame 1 is the trap that
 * called it and frame 2 is the engine function that failed; the frames past that
 * say which screen or task it was reached from. `addr2line -e
 * mmo/build/fused/pokeplatinum <addr>...` resolves the lot. */
static void report(const char *what, long count)
{
    void *frames[10];
    int depth, i;

    depth = mmo_plat_backtrace(frames, (int)(sizeof frames / sizeof frames[0]));

    fprintf(stderr, "openmmo: %s (%ld so far), at", what, count);
    for (i = 2; i < depth; i++) {
        fprintf(stderr, " %p", frames[i]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

static int assert_mode(void)
{
    static int mode = -1;

    if (mode < 0) {
        const char *v = openmmo_dev_env("OPENMMO_ASSERT");
        mode = (v != NULL && strcmp(v, "fatal") == 0) ? ASSERT_FATAL : ASSERT_WARN;
    }

    return mode;
}

/* Stop the process with the reason already printed above. */
static void stop(void)
{
    fprintf(stderr,
            "openmmo: this build stops here, on a console the game would have"
            " shown an error screen and reset, and on a host it used to carry on"
            " into the state the check ruled out.\n"
            "openmmo: addr2line -e mmo/build/fused/pokeplatinum resolves the"
            " addresses above.\n");
    fflush(stderr);
    abort();
}

/* The texture pool had no free slot. The caller is about to write through the
 * NULL it was handed, so there is nothing to return to and nothing to survive:
 * say which pool, how big it is and how to make it bigger, and stop here rather
 * than in the memcpy. */
void openmmo_texture_pool_full(int id)
{
    fprintf(stderr,
            "openmmo: the overworld texture pool is full, %d slots, all taken,"
            " and graphics id %d wanted one more.\n"
            "openmmo: raise OPENMMO_TEXTURE_SLOTS (or the default in"
            " mods/openmmo/src/openmmo_crowd.c) or draw fewer distinct"
            " appearances at once.\n",
            openmmo_texture_slot_capacity(), id);
    fflush(stderr);
    stop();
}

/* The engine's assertions, which on this port were being thrown away.
 * ErrorHandling_AssertFail's own body only speaks when CommManager is up, so a
 * failed GF_ASSERT on a host returned quietly and the caller carried on into
 * whatever the assertion existed to prevent. This is called before that body. */
void openmmo_engine_assert_failed(const void *caller)
{
    static long count;
    extern void ErrorHandling_AssertFail(void);

    report("engine assertion failed", ++count);
    /*
     * The official client's assert carries no file or line, so the one thing that names the asserter is
     * where it will return to.
     */
    fprintf(stderr, "openmmo:   the asserter returns to ErrorHandling_AssertFail%+ld (%p)\n",
            (long)((const char *)caller - (const char *)ErrorHandling_AssertFail),
            caller);
    fflush(stderr);
    if (assert_mode() == ASSERT_FATAL) {
        stop();
    }
}

/*
 * A heap had no room. The engine's own AllocFail() is the same silence as the assertion above,
 * it speaks only with the DS comm stack up, and Heap_Alloc then returns NULL to a caller
 * that mostly asserts on it and carries on.
 */
void openmmo_engine_heap_full(void)
{
    static long count;

    report("a heap is full", ++count);
    stop();
}
