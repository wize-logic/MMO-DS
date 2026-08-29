/*
 * How much memory the field is allowed, on a machine that is not a
 * cartridge.
 */

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/heap.h"
#include "heap.h"

/* How much bigger than the engine's own each field heap is built, in bytes. */
#define OPENMMO_HEAP_FIELD1_EXTRA_DEFAULT 0x14000
#define OPENMMO_HEAP_FIELD2_EXTRA_DEFAULT 0x4000
#define OPENMMO_HEAP_FIELD3_EXTRA_DEFAULT 0

/* And how much bigger APPLICATION itself is, which is where the two above are spent from. */
#define OPENMMO_HEAP_APPLICATION_EXTRA_DEFAULT 0x1A000

/* What is left of the arena for the allocations InitSystem makes after the heaps
 * four task managers and the filesystem table, plus room to spare. The raise
 * is clamped to keep this much back, and says so rather than silently taking a
 * heap the port then cannot boot with. */
#define OPENMMO_ARENA_RESERVE 0x10000

/* An adjustment past this is a typo rather than an intent: every field heap
 * lives inside APPLICATION (0x10D800) and none of them has any business moving
 * by half of it. The floor is what a heap is clamped to if an adjustment would
 * take it below one. */
#define OPENMMO_HEAP_EXTRA_LIMIT 0x80000
#define OPENMMO_HEAP_FLOOR 0x1000

int openmmo_heap_report_enabled(void);

static long env_delta(const char *name, long fallback)
{
    const char *v = getenv(name);
    char *end;
    long n;

    if (v == NULL || v[0] == '\0') {
        return fallback;
    }

    n = strtol(v, &end, 0);
    if (*end != '\0' || n < -OPENMMO_HEAP_EXTRA_LIMIT || n > OPENMMO_HEAP_EXTRA_LIMIT) {
        fprintf(stderr,
                "openmmo: %s=%s is not a byte adjustment in [-%#x,%#x]; using %#lx\n",
                name, v, OPENMMO_HEAP_EXTRA_LIMIT, OPENMMO_HEAP_EXTRA_LIMIT, fallback);
        fflush(stderr);
        return fallback;
    }

    return n;
}

static long field_extra(int which, const char *name, long fallback)
{
    static long cached[3];
    static int have[3];

    if (!have[which]) {
        cached[which] = env_delta(name, fallback);
        have[which] = 1;
    }

    return cached[which];
}

/* The size a field heap is created at, given the size the engine asked for.
 * Called from the sites that used to pass their own number straight to
 * Heap_Create: fieldmap.c for FIELD1, field_system.c for FIELD2 and FIELD3. A
 * heap this does not know keeps the engine's number untouched. */
u32 openmmo_heap_size(int heapID, u32 engineSize)
{
    static u32 said[3];
    long extra;
    u32 built;
    int which;
    const char *name;

    switch (heapID) {
    case HEAP_ID_FIELD1:
        which = 0;
        name = "FIELD1";
        extra = field_extra(0, "OPENMMO_HEAP_FIELD1_EXTRA", OPENMMO_HEAP_FIELD1_EXTRA_DEFAULT);
        break;
    case HEAP_ID_FIELD2:
        which = 1;
        name = "FIELD2";
        extra = field_extra(1, "OPENMMO_HEAP_FIELD2_EXTRA", OPENMMO_HEAP_FIELD2_EXTRA_DEFAULT);
        break;
    case HEAP_ID_FIELD3:
        which = 2;
        name = "FIELD3";
        extra = field_extra(2, "OPENMMO_HEAP_FIELD3_EXTRA", OPENMMO_HEAP_FIELD3_EXTRA_DEFAULT);
        break;
    default:
        return engineSize;
    }

    if (extra < 0 && (u32)(-extra) + OPENMMO_HEAP_FLOOR > engineSize) {
        built = OPENMMO_HEAP_FLOOR;
    } else {
        built = (u32)((long)engineSize + extra);
    }

    /* Say what this heap was actually built at, beside the number the engine asked for. */
    if (openmmo_heap_report_enabled() && said[which] != engineSize) {
        said[which] = engineSize;
        fprintf(stderr,
                "openmmo: field heap %s, engine %#x, built %#x (%s%#lx)\n",
                name, engineSize, built, extra < 0 ? "-" : "+",
                extra < 0 ? -extra : extra);
        fflush(stderr);
    }

    return built;
}

/* Adjust the top-level heap sizes before Heap_InitSystem carves them out of the main arena. */
void openmmo_heap_resize_templates(HeapParam *params, u32 count)
{
    long extra = env_delta("OPENMMO_HEAP_APPLICATION_EXTRA",
                           OPENMMO_HEAP_APPLICATION_EXTRA_DEFAULT);
    u32 arenaLo = (u32)(uintptr_t)OS_GetArenaLo(OS_ARENA_MAIN);
    u32 arenaHi = (u32)(uintptr_t)OS_GetArenaHi(OS_ARENA_MAIN);
    u32 wanted = 0;
    u32 room;
    u32 i;
    int found = 0;

    for (i = 0; i < count; i++) {
        wanted += params[i].size;
    }

    room = arenaHi - arenaLo;
    if (room < wanted + OPENMMO_ARENA_RESERVE) {
        room = 0;
    } else {
        room -= wanted + OPENMMO_ARENA_RESERVE;
    }

    if (extra > 0 && (u32)extra > room) {
        fprintf(stderr,
                "openmmo: APPLICATION asked for %#lx more than the engine's, the"
                " arena has %#x to give; taking that instead\n",
                extra, room);
        fflush(stderr);
        extra = (long)room;
    }

    for (i = 0; i < count; i++) {
        if (params[i].size == HEAP_SIZE_APPLICATION) {
            params[i].size = (u32)((long)params[i].size + extra);
            found = 1;
        }
    }

    if (!found) {
        fprintf(stderr,
                "openmmo: no APPLICATION heap in the engine's %u templates --"
                " the field heaps are on the engine's own budget this run\n",
                (unsigned)count);
        fflush(stderr);
    }

    if (openmmo_heap_report_enabled()) {
        fprintf(stderr,
                "openmmo: heaps at init, arena %#x, templates %#x, APPLICATION"
                " +%#lx, arena left %#x\n",
                arenaHi - arenaLo, wanted, extra,
                (unsigned)(arenaHi - arenaLo - wanted - (u32)extra));
        fflush(stderr);
    }
}

/* What a real map load actually costs, printed under OPENMMO_HEAP_REPORT=1. */
void openmmo_heap_report(const char *when)
{
    static const struct {
        int id;
        const char *name;
    } watched[] = {
        { HEAP_ID_APPLICATION, "APPLICATION" },
        { HEAP_ID_FIELD1, "FIELD1" },
        { HEAP_ID_FIELD2, "FIELD2" },
        { HEAP_ID_FIELD3, "FIELD3" },
    };
    unsigned i;
    u32 arenaLo = (u32)(uintptr_t)OS_GetArenaLo(OS_ARENA_MAIN);
    u32 arenaHi = (u32)(uintptr_t)OS_GetArenaHi(OS_ARENA_MAIN);

    fprintf(stderr, "openmmo: heaps at %s, arena free %u", when,
            (unsigned)(arenaHi - arenaLo));
    for (i = 0; i < sizeof watched / sizeof watched[0]; i++) {
        fprintf(stderr, ", %s free %u", watched[i].name,
                (unsigned)HeapExp_FndGetTotalFreeSize((u32)watched[i].id));
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

int openmmo_heap_report_enabled(void)
{
    static int on = -1;

    if (on < 0) {
        const char *v = getenv("OPENMMO_HEAP_REPORT");
        on = (v != NULL && v[0] != '\0' && v[0] != '0');
    }

    return on;
}
