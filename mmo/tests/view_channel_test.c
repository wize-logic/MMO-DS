/* The frame channel's layout, from the game's side of it. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "view_channel.h"

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

#define OFF(f) offsetof(struct openmmo_view_shm, f)

static void test_layout_is_abi_invariant(void)
{
    CHECK(sizeof(struct openmmo_view_shm) == 16941144u, "page is 16941144 bytes");

    CHECK(OFF(magic) == 0 && OFF(version) == 4 && OFF(publisher) == 8,
        "header words sit at 0, 4, 8");
    CHECK(OFF(seq) == 12 && OFF(frame_lo) == 16 && OFF(frame_hi) == 20,
        "seqlock and frame counter follow at 12");
    CHECK(OFF(upper_engine) == 24 && OFF(width) == 28 && OFF(height) == 32
            && OFF(touch_wanted) == 36,
        "frame shape words end at 36");
    CHECK(OFF(pix) == 40, "pixels start at 40");

    /* The input words start where the pixels end: 40 + two screens of the
     * widest frame at the highest internal resolution. */
    CHECK(OFF(in_seq) == 16810024, "input channel starts at 16810024");
    CHECK(OFF(in_keys) == 16810028 && OFF(in_touch) == 16810032
            && OFF(in_touch_x) == 16810036 && OFF(in_touch_y) == 16810040,
        "keys and pen follow the input sequence");
    CHECK(OFF(in_aspect_n) == 16810044 && OFF(in_aspect_d) == 16810048,
        "the window's shape is a ratio at 16810044");
    CHECK(OFF(in_viewer_pid) == 16810052 && OFF(in_quit) == 16810056,
        "who is watching, and whether they left");
    CHECK(OFF(in_turbo) == 16810060,
        "the retired speed word still closes the input, so the page keeps its shape");
    CHECK(OFF(audio_rate) == 16810064 && OFF(audio_head) == 16810068
            && OFF(audio) == 16810072,
        "the audio ring closes the page at 16810072");
}

static void test_pinned_constants(void)
{
    CHECK(OPENMMO_VIEW_MAGIC == 0x50504C56u, "magic is 'PPLV'");
    CHECK(OPENMMO_VIEW_VERSION == 1007u,
        "channel version is pinned at 1007: engine page 7, wider arrays");
    CHECK(OPENMMO_VIEW_FRAME_WORDS == 2101248u,
        "a screen is the widest frame at the highest internal resolution");
    CHECK(OPENMMO_VIEW_AUDIO_RATE == 32728u,
        "the ring runs at the guest's own rate, not a round 32768");
}

static void test_aspect_width(void)
{
    /* Never narrower than the DS, never wider than the cap, always even, and
     * "no viewer has said" is native rather than degenerate. */
    CHECK(openmmo_view_aspect_width(0, 0) == OPENMMO_VIEW_W,
        "an unstated window shape is native width");
    CHECK(openmmo_view_aspect_width(16, 0) == OPENMMO_VIEW_W,
        "a zero denominator is native width, not a divide");
    CHECK(openmmo_view_aspect_width(4, 3) == 256, "4:3 is the DS's own width");
    CHECK(openmmo_view_aspect_width(1, 2) == 256,
        "a window taller than it is wide never goes below native");
    CHECK(openmmo_view_aspect_width(16, 9) == 340, "16:9 asks for 340 columns");
    CHECK(openmmo_view_aspect_width(1920, 1080) == 340,
        "the same shape in pixels asks for the same columns");
    CHECK(openmmo_view_aspect_width(21, 9) == 448,
        "21:9 asks for 448 columns instead of stopping at 16:9");
    CHECK(openmmo_view_aspect_width(40, 9) == 684,
        "wider than the cap is the cap");
    CHECK((openmmo_view_aspect_width(1000, 291) & 1) == 0,
        "the width is even, so the two margins match");
}

static void test_audio_ring(void)
{
    static struct openmmo_view_shm page;
    int16_t dst[256];
    uint32_t tail, dropped;
    unsigned n, i;

    page.magic = OPENMMO_VIEW_MAGIC;
    page.version = OPENMMO_VIEW_VERSION;
    for (i = 0; i < OPENMMO_VIEW_AUDIO_FRAMES; i++)
        page.audio[i] = (i * 2654435761u) ^ 0x5bd1e995u;

    /* Nothing published yet: a reader takes nothing and asks for silence. */
    page.audio_head = 0;
    tail = 0;
    dropped = 99;
    n = openmmo_view_audio_read(&page, &tail, dst, 128, &dropped);
    CHECK(n == 0 && tail == 0 && dropped == 0,
        "an empty ring yields nothing and drops nothing");

    /* Fewer frames than asked for: take what is there, leave the tail on it. */
    page.audio_head = 7;
    tail = 0;
    n = openmmo_view_audio_read(&page, &tail, dst, 128, &dropped);
    CHECK(n == 7 && tail == 7 && dropped == 0,
        "a partly filled ring yields exactly what was published");
    CHECK(dst[0] == (int16_t)(uint16_t)(page.audio[0] & 0xFFFFu)
            && dst[1] == (int16_t)(uint16_t)(page.audio[0] >> 16),
        "left is the low half of the word, right the high");

    /* A reader that fell a full ring behind skips itself forward rather than
     * replaying a ring's worth of stale sound. */
    page.audio_head = OPENMMO_VIEW_AUDIO_FRAMES + 11;
    tail = 0;
    n = openmmo_view_audio_read(&page, &tail, dst, 128, &dropped);
    CHECK(n == 128 && tail == 139 && dropped == 11,
        "falling behind drops the overrun and resumes at the ring's start");

    /* The head is a running count and wraps at 2^32; the subtraction that finds
     * how much is available has to stay unsigned across that. */
    page.audio_head = 4;
    tail = 0xFFFFFFFCu;
    dropped = 99;
    n = openmmo_view_audio_read(&page, &tail, dst, 128, &dropped);
    CHECK(n == 8 && tail == 4 && dropped == 0,
        "a head that has wrapped past 2^32 is still eight frames ahead");

    /* A page whose version is not ours is not one we can read: the pixels would
     * be wrong too, and silence is the honest answer for the sound. */
    page.version = OPENMMO_VIEW_VERSION + 1;
    page.audio_head = 64;
    tail = 0;
    n = openmmo_view_audio_read(&page, &tail, dst, 128, &dropped);
    CHECK(n == 0 && tail == 0, "a foreign version yields no audio at all");
}

/* The page, through the platform seam, both ways. */
static void page_semantics(const char *how)
{
    char msg[160], name[64];
    mmo_shm pub = MMO_SHM_INIT, sub = MMO_SHM_INIT;
    unsigned char *p, *q;

    snprintf(name, sizeof name, "openmmo-test-%u-page", mmo_plat_pid());
    mmo_shm_unlink(name);   /* a previous run that died hard */

#define PCHECK(cond, what)                                                     \
    do {                                                                       \
        snprintf(msg, sizeof msg, "%s: %s", how, what);                        \
        CHECK(cond, msg);                                                      \
    } while (0)

    PCHECK(!mmo_shm_exists(name), "no page of this name before one is made");
    PCHECK(mmo_shm_create(&pub, name, 4096) == 0, "a page is published");
    PCHECK(mmo_shm_exists(name), "and is findable by name");
    PCHECK(mmo_shm_size_matches(&pub, 4096), "at the size it was asked for");

    PCHECK(mmo_shm_attach(&sub, name, 4096, 0) == 0, "a reader attaches");
    p = pub.addr;
    q = sub.addr;
    PCHECK(p != NULL && q != NULL, "and both ends have a mapping");
    if (p != NULL && q != NULL) {
        memset(p, 0, 4096);
        p[0] = 0xA5;
        p[4095] = 0x5A;
        PCHECK(q[0] == 0xA5 && q[4095] == 0x5A,
               "what the publisher writes is what the reader reads");
    }

    PCHECK(!mmo_shm_orphaned(&sub), "the reader is not orphaned while the name stands");
    mmo_shm_unlink(name);
    PCHECK(!mmo_shm_exists(name), "unlink takes the name down");
    PCHECK(mmo_shm_orphaned(&sub), "and the reader is told, which is how a clean end reads");
    if (q != NULL)
        PCHECK(q[0] == 0xA5 && q[4095] == 0x5A,
               "while the mapping it already has stays valid");

    mmo_shm_close(&sub);
    mmo_shm_close(&pub);
    PCHECK(sub.addr == NULL && pub.addr == NULL, "closing twice over is safe");
    mmo_shm_close(&sub);
    mmo_shm_close(&pub);

    /* Nothing left behind: the same name is free for the next session, which
     * is what a leaked row or a leaked object would break on the second run. */
    PCHECK(mmo_shm_create(&pub, name, 4096) == 0, "and the name is free again");
    mmo_shm_unlink(name);
    mmo_shm_close(&pub);

#undef PCHECK
}

static void test_page_both_ways(void)
{
    mmo_shm_one_process(0);
    page_semantics("three processes");
    mmo_shm_one_process(1);
    page_semantics("one process");
    /* Back to the environment's answer, so nothing after this suite inherits
     * a setting this suite chose. */
    mmo_shm_one_process(-1);
    CHECK(mmo_shm_is_one_process() == 0,
          "and the default is still the three-process page");
}

int view_channel_tests_run(void)
{
    printf("view_channel:\n");
    failures = 0;
    test_layout_is_abi_invariant();
    test_pinned_constants();
    test_aspect_width();
    test_audio_ring();
    test_page_both_ways();
    return failures;
}
