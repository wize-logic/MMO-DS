/*
 * The display's clock, on a page of its own beside the frame one, so the
 * game can read it from a different process.
 */

#ifndef OPENMMO_VSYNC_CHANNEL_H
#define OPENMMO_VSYNC_CHANNEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENMMO_VSYNC_MAGIC   0x4F4D5653u /* 'OMVS' */

/* Ours to bump, like the text channel's and unlike the frame channel's. */
#define OPENMMO_VSYNC_VERSION 1u

/* The page's name, given the frame channel's. One window, one game, one pair. */
#define OPENMMO_VSYNC_SUFFIX ".vsync"

struct openmmo_vsync_shm {
    uint32_t magic;
    uint32_t version;
    /* The window that presents into this page, so a game can tell a live
     * one from a page left behind by a window that crashed. */
    volatile uint32_t writer;
    /* Odd while the four words below are being written. */
    volatile uint32_t seq;
    /* When the most recent refresh landed, on mmo_plat_mono_ns()'s clock, low word first. */
    volatile uint32_t last_lo;
    volatile uint32_t last_hi;
    /* The interval between refreshes, in nanoseconds, once the writer has
     * measured one it believes. Zero until then. A whole 32-bit word holds
     * every rate anyone has a panel for: 4 ms is 250 Hz and 40 ms is 25 Hz. */
    volatile uint32_t period_ns;
    /* Refreshes seen. Zero means nothing has presented yet, which is not the
     * same as a period of zero and is worth being able to tell apart. */
    volatile uint32_t marks;
};

static inline uint64_t openmmo_vsync_join(uint32_t lo, uint32_t hi)
{
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VSYNC_CHANNEL_H */
