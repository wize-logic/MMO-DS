/* The other end of the window, for a machine with nobody at it. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <SDL.h>

#include "text_channel.h"
#include "view_channel.h"
#include "view_geom.h"
#include "view_input.h"

/* The window this harness pretends to be: the smart layout at scale 2,
 * which is what viewer_test.sh asks openmmo-view for and what the shot
 * check below measures. fill / --scale auto are the window's own defaults
 * and are a different picture. */
#define DRIVE_WIN_W 768
#define DRIVE_WIN_H 384

static int failures;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok   %s\n", msg);                                        \
        } else {                                                               \
            printf("  FAIL %s\n", msg);                                        \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------ */
/* The page                                                            */
/* ------------------------------------------------------------------ */

static struct openmmo_view_shm *page_map(const char *name, int create)
{
    struct openmmo_view_shm *v;
    int fd = create ? shm_open(name, O_RDWR | O_CREAT, 0600)
                    : shm_open(name, O_RDWR, 0600);

    if (fd < 0) {
        fprintf(stderr, "view-drive: shm_open(%s): %s\n", name, strerror(errno));
        return NULL;
    }
    if (create && ftruncate(fd, (off_t)sizeof *v) != 0) {
        fprintf(stderr, "view-drive: ftruncate: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }
    v = mmap(NULL, sizeof *v, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (v == MAP_FAILED) {
        fprintf(stderr, "view-drive: mmap: %s\n", strerror(errno));
        return NULL;
    }
    return v;
}

/*
 * Two solid screens, and nothing else in the picture: the top screen pure red and the touch
 * screen pure blue, so the composed shot is countable, every pixel is one of three exact
 * colours and each count is an area the layout decided.
 */
static void page_fill(struct openmmo_view_shm *v, unsigned version, int magic)
{
    unsigned i, n = OPENMMO_VIEW_W * OPENMMO_VIEW_H;

    v->width = OPENMMO_VIEW_W;
    v->height = OPENMMO_VIEW_H;
    v->upper_engine = 0;
    v->touch_wanted = 0;
    for (i = 0; i < n; i++) {
        v->pix[0][i] = 0x00FF0000u;
        v->pix[1][i] = 0x000000FFu;
    }
    v->in_seq = 0;
    v->in_keys = 0;
    v->in_touch = 0;
    v->in_turbo = 0;
    v->frame_lo = 1;
    v->seq = 2; /* even: a stable frame, and >= 2, which is "a frame exists" */
    v->version = version;
    v->publisher = (uint32_t)getpid();
    __sync_synchronize();
    /* Last, and only if asked: `publish --no-magic` leaves the page in the
     * state the publisher's own page is in between ftruncate and this store. */
    if (magic) v->magic = OPENMMO_VIEW_MAGIC;
}

static int cmd_publish(int argc, char **argv)
{
    struct openmmo_view_shm *v;
    unsigned version = OPENMMO_VIEW_VERSION;
    const char *name = NULL;
    int magic = 1, i;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 && i + 1 < argc)
            version = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--no-magic") == 0)
            magic = 0;
        else
            name = argv[i];
    }
    if (name == NULL) return 2;

    shm_unlink(name);
    v = page_map(name, 1);
    if (v == NULL) return 1;
    page_fill(v, version, magic);
    return 0;
}

/* The publisher's last store, on its own, so a viewer can be watching while it
 * happens. */
static int cmd_stamp(const char *name)
{
    struct openmmo_view_shm *v = page_map(name, 0);

    if (v == NULL) return 1;
    __sync_synchronize();
    v->magic = OPENMMO_VIEW_MAGIC;
    return 0;
}

static int cmd_unlink(const char *name)
{
    char text[160];

    snprintf(text, sizeof text, "%s%s", name, OPENMMO_TEXT_SUFFIX);
    shm_unlink(name);
    shm_unlink(text);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The picture                                                         */
/* ------------------------------------------------------------------ */

/*
 * What the window drew, counted rather than looked at. `live` is the picture of the page
 * above: the two screens the layout placed, at the sizes it placed them, over the background
 * and nothing else.
 */
static int cmd_shotcheck(const char *path, const char *want)
{
    unsigned char *px;
    long n, i;
    unsigned red = 0, blue = 0, black = 0, other = 0;
    int w = 0, h = 0, maxv = 0;
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        printf("  FAIL %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (fscanf(f, "P6 %d %d %d", &w, &h, &maxv) != 3 || maxv != 255) {
        printf("  FAIL %s is not a binary PPM\n", path);
        fclose(f);
        return 1;
    }
    fgetc(f); /* the single whitespace byte before the pixels */
    n = (long)w * h;
    px = malloc((size_t)n * 3);
    if (px == NULL || fread(px, 3, (size_t)n, f) != (size_t)n) {
        printf("  FAIL %s is shorter than its own header says\n", path);
        free(px);
        fclose(f);
        return 1;
    }
    fclose(f);

    for (i = 0; i < n; i++) {
        unsigned char *p = px + i * 3;

        if (p[0] == 0xFF && p[1] == 0 && p[2] == 0) red++;
        else if (p[0] == 0 && p[1] == 0 && p[2] == 0xFF) blue++;
        else if (p[0] == 0 && p[1] == 0 && p[2] == 0) black++;
        else other++;
    }
    free(px);

    CHECK(w == DRIVE_WIN_W && h == DRIVE_WIN_H,
          "the shot is the window's own size");
    if (strcmp(want, "live") == 0) {
        /* The smart layout at scale 2: the game at 2x (512x384), the touch
         * screen at 1x beside it (256x192), the rest background. Exact,
         * because nothing here is filtered or blended, a count that has
         * drifted is a layout that moved, which is worth being told about. */
        CHECK(red == 512u * 384u, "the top screen is presented at 2x, whole");
        CHECK(blue == 256u * 192u, "the touch screen is presented at 1x, whole");
        CHECK(other == 0, "every pixel is a published colour or the background");
        if (red != 512u * 384u || blue != 256u * 192u || other != 0)
            printf("       red=%u blue=%u black=%u other=%u\n",
                   red, blue, black, other);
    } else {
        CHECK(red == 0 && blue == 0,
              "a refused page is not drawn: no frame pixels in the shot");
        CHECK(other > 0, "the refusal is a picture, not a blank window");
    }
    return failures != 0;
}

/* ------------------------------------------------------------------ */
/* The player                                                          */
/* ------------------------------------------------------------------ */

/*
 * SDL's key state, set from here. There is no public call for it: the dummy video driver
 * produces no key events, and pushing an SDL_KEYDOWN event does not touch the array
 * SDL_GetKeyboardState returns.
 */
static void key_hold(SDL_Scancode sc, int down)
{
    int n = 0;
    const Uint8 *state = SDL_GetKeyboardState(&n);

    if ((int)sc < n) ((Uint8 *)state)[sc] = (Uint8)(down != 0);
}

static void keys_clear(void)
{
    int n = 0;
    const Uint8 *state = SDL_GetKeyboardState(&n);

    memset((Uint8 *)state, 0, (size_t)n);
    SDL_SetModState(KMOD_NONE);
}

static void drain(struct view_input *in)
{
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) view_input_event(in, &ev);
}

static void send_event(struct view_input *in, SDL_Event *ev)
{
    view_input_event(in, ev);
}

static SDL_Joystick *pad_attach(struct view_input *in)
{
    int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                          6, 15, 1);

    if (index < 0) return NULL;
    drain(in); /* the CONTROLLERDEVICEADDED that opens it, through view_input */
    if (in->gc == NULL) return NULL;
    return SDL_GameControllerGetJoystick(in->gc);
}

static void pad_button(SDL_Joystick *js, SDL_GameControllerButton b, int down)
{
    SDL_JoystickSetVirtualButton(js, (int)b, (Uint8)(down != 0));
    SDL_GameControllerUpdate();
}

static int cmd_input(const char *name)
{
    struct openmmo_view_geom g = { OPENMMO_LAYOUT_SMART, OPENMMO_ASPECT_NATIVE,
                                   0, 0 };
    const int sec = OPENMMO_VIEW_SEC_MIN;
    struct openmmo_rect rects[2];
    struct openmmo_view_shm *v;
    struct openmmo_text_shm *text;
    struct view_input in;
    SDL_Joystick *js;
    SDL_Window *win;
    SDL_Event ev;
    uint32_t seq;
    char text_name[160];
    int fd, mx, my;

    shm_unlink(name);
    v = page_map(name, 1);
    if (v == NULL) return 1;
    page_fill(v, OPENMMO_VIEW_VERSION, 1);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("  FAIL SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("view-drive", 0, 0, DRIVE_WIN_W, DRIVE_WIN_H, 0);
    if (win == NULL) {
        printf("  FAIL SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    view_input_init(&in);
    keys_clear();
    drain(&in);

    printf("the window's input reaches the page the game reads:\n");

    /* NOTHING SAID YET. in_seq == 0 is the game's way of telling a scripted run
     * from a played one, so an idle window that has bumped it has already
     * silenced a script. */
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    CHECK(v->in_seq == 0 && v->in_keys == 0,
          "an untouched window has not spoken: in_seq is still 0");

    /* A key becomes the button the console had. Z is confirm, the official
     * client's default rather than the base port's. */
    key_hold(SDL_SCANCODE_Z, 1);
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    CHECK(v->in_keys == OPENMMO_VIEW_KEY_A && v->in_seq == 1,
          "a held key is published as its pad bit, once");

    /* Published on change only: the same state again says nothing. */
    seq = v->in_seq;
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    CHECK(v->in_seq == seq, "an unchanged window bumps nothing");

    /* Two at once, and the release of the first. */
    key_hold(SDL_SCANCODE_Z, 0);
    key_hold(SDL_SCANCODE_UP, 1);
    key_hold(SDL_SCANCODE_X, 1);
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    CHECK(v->in_keys == (OPENMMO_VIEW_KEY_UP | OPENMMO_VIEW_KEY_B)
              && v->in_seq == seq + 1,
          "two keys at once, and a released one is gone");

    /* Alt+Enter is the window's fullscreen chord and must not also press
     * Start, the one keyboard combination the pad has an opinion about. */
    keys_clear();
    key_hold(SDL_SCANCODE_RETURN, 1);
    SDL_SetModState(KMOD_LALT);
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    CHECK(v->in_keys == 0, "Alt+Enter does not leak Start");
    keys_clear();

    /* Fast-forward is gone: Tab holds nothing open any more, and the word it
     * used to travel on stays at zero however long the key is held. */
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    seq = v->in_seq;
    key_hold(SDL_SCANCODE_TAB, 1);
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    CHECK(v->in_turbo == 0 && v->in_seq == seq,
          "Tab publishes nothing at all, no speed word, no input");
    key_hold(SDL_SCANCODE_TAB, 0);

    /* A rebound key, and the default it replaced. */
    CHECK(view_input_bind(&in, "a=k") == 0, "--bind a=k is understood");
    key_hold(SDL_SCANCODE_Z, 1);
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    CHECK(v->in_keys == 0, "the key A used to be bound to no longer presses it");
    key_hold(SDL_SCANCODE_Z, 0);
    key_hold(SDL_SCANCODE_K, 1);
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    CHECK(v->in_keys == OPENMMO_VIEW_KEY_A, "the key it was rebound to does");
    keys_clear();
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);

    /* A pad that was already a device when the window started: attach it,
     * throw away the ADDED event SDL queued, then scan. Waiting for an
     * event that never comes is how a pad plugged in before the window
     * stayed dark. */
    {
        int already = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                                6, 15, 1);
        SDL_Event drop;

        while (SDL_PollEvent(&drop))
            ;
        CHECK(in.gc == NULL, "discarding the plug event left no pad open");
        view_input_attach_present(&in);
        CHECK(already >= 0 && in.gc != NULL,
              "a pad already plugged in is opened without an event");
        if (in.gc != NULL) {
            SDL_GameControllerClose(in.gc);
            in.gc = NULL;
        }
        if (already >= 0)
            SDL_JoystickDetachVirtual(already);
    }

    /* The gamepad, positionally: SDL's B is where the console's A is. */
    js = pad_attach(&in);
    if (js == NULL) {
        printf("  FAIL no virtual gamepad: %s\n", SDL_GetError());
        failures++;
    } else {
        pad_button(js, SDL_CONTROLLER_BUTTON_B, 1);
        view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                           DRIVE_WIN_W, DRIVE_WIN_H);
        CHECK(v->in_keys == OPENMMO_VIEW_KEY_A,
              "a gamepad button is published in the same word as a key");
        pad_button(js, SDL_CONTROLLER_BUTTON_B, 0);

        SDL_JoystickSetVirtualAxis(js, SDL_CONTROLLER_AXIS_LEFTX, -30000);
        SDL_GameControllerUpdate();
        view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                           DRIVE_WIN_W, DRIVE_WIN_H);
        CHECK(v->in_keys == OPENMMO_VIEW_KEY_LEFT,
              "the stick past its deadzone is a direction");
        SDL_JoystickSetVirtualAxis(js, SDL_CONTROLLER_AXIS_LEFTX, 0);
        SDL_GameControllerUpdate();
        view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                           DRIVE_WIN_W, DRIVE_WIN_H);
    }

    /* The pen, from where the pointer is. The centre of the touch screen's
     * rectangle on screen is the centre of the touch screen in the game, which
     * is an expectation arithmetic here did not produce. */
    openmmo_view_screen_rects(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                              DRIVE_WIN_W, DRIVE_WIN_H, rects);
    mx = rects[1].x + rects[1].w / 2;
    my = rects[1].y + rects[1].h / 2;
    SDL_WarpMouseInWindow(win, mx, my);
    SDL_PumpEvents();
    memset(&ev, 0, sizeof ev);
    ev.type = SDL_MOUSEBUTTONDOWN;
    ev.button.button = SDL_BUTTON_LEFT;
    send_event(&in, &ev);
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    CHECK(v->in_touch == 1
              && v->in_touch_x >= OPENMMO_VIEW_W / 2 - 1
              && v->in_touch_x <= OPENMMO_VIEW_W / 2 + 1
              && v->in_touch_y >= OPENMMO_VIEW_H / 2 - 1
              && v->in_touch_y <= OPENMMO_VIEW_H / 2 + 1,
          "a click in the middle of the touch screen is a pen in the middle");
    if (v->in_touch != 1)
        printf("       touch=%u at %u,%u from window %d,%d\n", v->in_touch,
               v->in_touch_x, v->in_touch_y, mx, my);

    memset(&ev, 0, sizeof ev);
    ev.type = SDL_MOUSEBUTTONUP;
    ev.button.button = SDL_BUTTON_LEFT;
    send_event(&in, &ev);
    view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                       DRIVE_WIN_W, DRIVE_WIN_H);
    CHECK(v->in_touch == 0, "and the pen is up when the button is");

    /*
     * Typing, from the client's side of it: the client opens the field by setting `want` on
     * the text page, and while it is set the keyboard is letters instead of buttons, 
     * otherwise typing "start" walks the player north.
     */
    view_input_open_text(&in, name);
    snprintf(text_name, sizeof text_name, "%s%s", name, OPENMMO_TEXT_SUFFIX);
    fd = shm_open(text_name, O_RDWR, 0600);
    text = fd < 0 ? NULL
                  : mmap(NULL, sizeof *text, PROT_READ | PROT_WRITE, MAP_SHARED,
                         fd, 0);
    if (fd >= 0) close(fd);
    if (text == NULL || text == MAP_FAILED) {
        printf("  FAIL the window published no text page at %s\n", text_name);
        failures++;
    } else {
        uint32_t tail = text->head, got[4];

        memset(&ev, 0, sizeof ev);
        ev.type = SDL_TEXTINPUT;
        snprintf(ev.text.text, sizeof ev.text.text, "a");
        CHECK(view_input_event(&in, &ev) == 0 && text->head == tail,
              "a character with no field open is not a character");

        text->want = 1; /* the client says a field is open */
        key_hold(SDL_SCANCODE_X, 1);
        if (js != NULL) pad_button(js, SDL_CONTROLLER_BUTTON_START, 1);
        view_input_publish(&in, v, &g, OPENMMO_VIEW_W, OPENMMO_VIEW_H, sec,
                           DRIVE_WIN_W, DRIVE_WIN_H);
        CHECK(v->in_keys == (js != NULL ? OPENMMO_VIEW_KEY_START : 0u),
              "with a field open the keyboard is letters and the pad is not");

        CHECK(view_input_event(&in, &ev) == 1, "and a character is taken");
        CHECK(openmmo_text_read(text, &tail, got, 4, NULL) == 1
                  && got[0] == ((OPENMMO_TEXT_UNIT << 24) | 'a'),
              "the character arrives on the page the client reads");

        if (js != NULL) pad_button(js, SDL_CONTROLLER_BUTTON_START, 0);
        text->want = 0;
        keys_clear();
    }

    view_input_close_text(&in);
    SDL_DestroyWindow(win);
    SDL_Quit();
    cmd_unlink(name);
    return failures != 0;
}

/* ------------------------------------------------------------------ */

/*
 * Keep writing audio into an existing page so a window that attached with tail == head can
 * grow a cushion and open a device. Square wave, not silence: a callback that only copies
 * zeroes is indistinguishable from a device that never started.
 */
static int cmd_feed_audio(const char *name, int ms)
{
    struct openmmo_view_shm *v;
    uint32_t head;
    int i, frames;

    v = page_map(name, 0);
    if (v == NULL) return 1;
    v->audio_rate = OPENMMO_VIEW_AUDIO_RATE;
    head = v->audio_head;
    if (ms < 100) ms = 100;
    frames = (int)(OPENMMO_VIEW_AUDIO_RATE * (unsigned)ms / 1000u);
    for (i = 0; i < frames; i++) {
        int16_t s = (int16_t)(((i / 16) & 1) ? 8000 : -8000);
        v->audio[head % OPENMMO_VIEW_AUDIO_FRAMES] =
            (uint32_t)(uint16_t)s | ((uint32_t)(uint16_t)s << 16);
        head++;
        if ((i & 255) == 0) {
            __atomic_store_n(&v->audio_head, head, __ATOMIC_RELEASE);
            usleep(2000);
        }
    }
    __atomic_store_n(&v->audio_head, head, __ATOMIC_RELEASE);
    munmap(v, sizeof *v);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: view-drive publish [--version N] [--no-magic] <channel>\n"
        "       view-drive stamp <channel>     write the magic, as the game\n"
        "                                      writes it last\n"
        "       view-drive unlink <channel>    the page and its text page\n"
        "       view-drive shotcheck <ppm> live|refused\n"
        "       view-drive input <channel>\n"
        "       view-drive feed-audio <channel> [ms]\n");
}

int main(int argc, char **argv)
{
    int rc;

    if (argc < 3) {
        usage();
        return 2;
    }
    if (strcmp(argv[1], "publish") == 0)
        return cmd_publish(argc - 2, argv + 2);
    if (strcmp(argv[1], "stamp") == 0)
        return cmd_stamp(argv[2]);
    if (strcmp(argv[1], "unlink") == 0)
        return cmd_unlink(argv[2]);
    if (strcmp(argv[1], "shotcheck") == 0) {
        if (argc < 4) {
            usage();
            return 2;
        }
        return cmd_shotcheck(argv[2], argv[3]);
    }
    if (strcmp(argv[1], "input") == 0) {
        rc = cmd_input(argv[2]);
        return rc;
    }
    if (strcmp(argv[1], "feed-audio") == 0) {
        int ms = (argc > 3) ? atoi(argv[3]) : 3000;

        return cmd_feed_audio(argv[2], ms);
    }
    usage();
    return 2;
}
