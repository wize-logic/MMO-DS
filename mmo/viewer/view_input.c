/* See view_input.h. */

#include "view_input.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"

/* ------------------------------------------------------------------ */
/* The maps                                                            */
/* ------------------------------------------------------------------ */

/* By SCANCODE, and read as a level. */
static const struct { SDL_Scancode sc; uint32_t pad; } VIEW_DEFAULT_MAP[] = {
    { SDL_SCANCODE_UP,        OPENMMO_VIEW_KEY_UP     },
    { SDL_SCANCODE_DOWN,      OPENMMO_VIEW_KEY_DOWN   },
    { SDL_SCANCODE_LEFT,      OPENMMO_VIEW_KEY_LEFT   },
    { SDL_SCANCODE_RIGHT,     OPENMMO_VIEW_KEY_RIGHT  },
    { SDL_SCANCODE_Z,         OPENMMO_VIEW_KEY_A      },
    { SDL_SCANCODE_X,         OPENMMO_VIEW_KEY_B      },
    { SDL_SCANCODE_S,         OPENMMO_VIEW_KEY_X      },
    { SDL_SCANCODE_A,         OPENMMO_VIEW_KEY_Y      },
    { SDL_SCANCODE_Q,         OPENMMO_VIEW_KEY_L      },
    { SDL_SCANCODE_W,         OPENMMO_VIEW_KEY_R      },
    { SDL_SCANCODE_RETURN,    OPENMMO_VIEW_KEY_START  },
    { SDL_SCANCODE_BACKSPACE, OPENMMO_VIEW_KEY_SELECT },
};

/*
 * The gamepad, POSITIONALLY. A DS has X at the top and the Xbox layout SDL names buttons for
 * has Y there; matching letters would put confirm where cancel is, so the mapping matches
 * thumb positions instead.
 */
static const struct { SDL_GameControllerButton button; uint32_t pad; } PADMAP[] = {
    { SDL_CONTROLLER_BUTTON_A,             OPENMMO_VIEW_KEY_B      },
    { SDL_CONTROLLER_BUTTON_B,             OPENMMO_VIEW_KEY_A      },
    { SDL_CONTROLLER_BUTTON_X,             OPENMMO_VIEW_KEY_Y      },
    { SDL_CONTROLLER_BUTTON_Y,             OPENMMO_VIEW_KEY_X      },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  OPENMMO_VIEW_KEY_L      },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, OPENMMO_VIEW_KEY_R      },
    { SDL_CONTROLLER_BUTTON_START,         OPENMMO_VIEW_KEY_START  },
    { SDL_CONTROLLER_BUTTON_BACK,          OPENMMO_VIEW_KEY_SELECT },
    { SDL_CONTROLLER_BUTTON_DPAD_UP,       OPENMMO_VIEW_KEY_UP     },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN,     OPENMMO_VIEW_KEY_DOWN   },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT,     OPENMMO_VIEW_KEY_LEFT   },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    OPENMMO_VIEW_KEY_RIGHT  },
};

#define PADMAP_N ((int)(sizeof PADMAP / sizeof PADMAP[0]))

/* A left stick reported as the d-pad past a deadzone. No oracle exists, a DS
 * has no stick, so the number is a preference, written once here. */
#define VIEW_STICK_DEADZONE 12000

/* The pad bits by name, for --bind. The DS's own button order rather than
 * alphabetical, because that is how a player thinks about them. */
static const struct { const char *name; uint32_t pad; } PADNAMES[] = {
    { "a", OPENMMO_VIEW_KEY_A }, { "b", OPENMMO_VIEW_KEY_B },
    { "x", OPENMMO_VIEW_KEY_X }, { "y", OPENMMO_VIEW_KEY_Y },
    { "l", OPENMMO_VIEW_KEY_L }, { "r", OPENMMO_VIEW_KEY_R },
    { "start", OPENMMO_VIEW_KEY_START }, { "select", OPENMMO_VIEW_KEY_SELECT },
    { "up", OPENMMO_VIEW_KEY_UP }, { "down", OPENMMO_VIEW_KEY_DOWN },
    { "left", OPENMMO_VIEW_KEY_LEFT }, { "right", OPENMMO_VIEW_KEY_RIGHT },
};

#define PADNAMES_N ((int)(sizeof PADNAMES / sizeof PADNAMES[0]))

void view_input_init(struct view_input *in)
{
    memset(in, 0, sizeof *in);
    memcpy(in->map, VIEW_DEFAULT_MAP, sizeof in->map);
    in->text_page.fd = -1;
}

void view_input_attach_present(struct view_input *in)
{
    int i, n;

    if (in->gc != NULL)
        return;
    n = SDL_NumJoysticks();
    for (i = 0; i < n; i++) {
        if (!SDL_IsGameController(i))
            continue;
        in->gc = SDL_GameControllerOpen(i);
        if (in->gc != NULL) {
            fprintf(stderr, "openmmo-view: gamepad: %s\n",
                    SDL_GameControllerName(in->gc));
            return;
        }
    }
}

int view_input_bind(struct view_input *in, const char *spec)
{
    char buf[512], *p;

    snprintf(buf, sizeof buf, "%s", spec);
    for (p = strtok(buf, ","); p != NULL; p = strtok(NULL, ",")) {
        char *eq = strchr(p, '=');
        SDL_Scancode sc;
        int i, found = -1;

        if (eq == NULL) {
            fprintf(stderr, "openmmo-view: --bind wants PAD=KEY, got '%s'\n", p);
            return -1;
        }
        *eq = '\0';
        for (i = 0; i < PADNAMES_N; i++)
            if (SDL_strcasecmp(p, PADNAMES[i].name) == 0) found = i;
        if (found < 0) {
            fprintf(stderr, "openmmo-view: --bind: no button named '%s'\n", p);
            return -1;
        }
        sc = SDL_GetScancodeFromName(eq + 1);
        if (sc == SDL_SCANCODE_UNKNOWN) {
            fprintf(stderr, "openmmo-view: --bind: no key named '%s' (SDL's "
                            "names: a..z, Return, Space, Left Shift, "
                            "Keypad 5)\n", eq + 1);
            return -1;
        }
        for (i = 0; i < VIEW_INPUT_KEYS; i++)
            if (in->map[i].pad == PADNAMES[found].pad) in->map[i].sc = sc;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The text page                                                       */
/* ------------------------------------------------------------------ */

/*
 * Created by the window and named after the frame channel, so a client that wants characters
 * opens "<channel>.text" and one that does not never notices.
 */
void view_input_open_text(struct view_input *in, const char *channel)
{
    struct openmmo_text_shm *t = NULL;

    if (in->text != NULL) return;
    snprintf(in->text_name, sizeof in->text_name, "%s%s", channel,
             OPENMMO_TEXT_SUFFIX);
    if (mmo_shm_create(&in->text_page, in->text_name, sizeof *t) != 0) {
        fprintf(stderr, "openmmo-view: no text channel '%s' (%s); the buttons "
                        "still work\n", in->text_name, mmo_plat_error());
        in->text_name[0] = '\0';
        return;
    }
    t = (struct openmmo_text_shm *)in->text_page.addr;
    memset(t, 0, sizeof *t);
    t->version = OPENMMO_TEXT_VERSION;
    t->writer = (uint32_t)mmo_plat_pid();
    __atomic_store_n(&t->magic, OPENMMO_TEXT_MAGIC, __ATOMIC_RELEASE);
    in->text = t;
}

void view_input_close_text(struct view_input *in)
{
    if (in->text == NULL) return;
    /* The window owns this page: nothing else may be typing into it, so it goes
     * away with the window rather than being left for the next one to find with
     * a dead pid in it. */
    in->text->magic = 0;
    in->text->writer = 0;
    mmo_shm_close(&in->text_page);
    if (in->text_name[0] != '\0') mmo_shm_unlink(in->text_name);
    in->text = NULL;
}

int view_input_typing(const struct view_input *in)
{
    return in->text != NULL && in->text->want != 0;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

static void view_input_push(struct view_input *in, uint32_t kind, uint32_t unit)
{
    openmmo_text_push(in->text, kind, unit);
    if (kind == OPENMMO_TEXT_UNIT) in->units++;
}

int view_input_event(struct view_input *in, const SDL_Event *ev)
{
    int typing = view_input_typing(in);

    switch (ev->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (ev->button.button == SDL_BUTTON_LEFT) in->mouse_down = 1;
        return 0;
    case SDL_MOUSEBUTTONUP:
        if (ev->button.button == SDL_BUTTON_LEFT) in->mouse_down = 0;
        return 0;
    case SDL_CONTROLLERDEVICEADDED:
        if (in->gc == NULL) {
            in->gc = SDL_GameControllerOpen(ev->cdevice.which);
            if (in->gc != NULL)
                fprintf(stderr, "openmmo-view: gamepad: %s\n",
                        SDL_GameControllerName(in->gc));
        }
        return 0;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (in->gc != NULL &&
            ev->cdevice.which ==
                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(in->gc))) {
            SDL_GameControllerClose(in->gc);
            in->gc = NULL;
            fprintf(stderr, "openmmo-view: gamepad removed\n");
        }
        return 0;
    case SDL_TEXTINPUT: {
        /*
         * The characters come from SDL_TEXTINPUT and not from KEYCODES, which is the whole
         * reason this path exists: a keycode is a key on a us keyboard, while this is what the
         * player's own layout, dead keys and input method actually produced.
         */
        const unsigned char *s = (const unsigned char *)ev->text.text;
        unsigned len = (unsigned)strlen(ev->text.text), i = 0;

        if (!typing) return 0;
        while (i < len) {
            uint16_t u[2];
            unsigned used = 1, n;

            n = openmmo_text_utf8_to_utf16(s + i, len - i, u, &used);
            for (unsigned k = 0; k < n; k++)
                view_input_push(in, OPENMMO_TEXT_UNIT, u[k]);
            i += used ? used : 1;
        }
        return 1;
    }
    case SDL_KEYDOWN: {
        /* The edits a character stream cannot express. Only while a field is
         * open: outside one these keys are the pad (Backspace is Select) and
         * Escape closes the window, which the caller still owns. */
        uint32_t kind = 0;

        if (!typing) return 0;
        switch (ev->key.keysym.sym) {
        case SDLK_BACKSPACE: kind = OPENMMO_TEXT_BACKSPACE; break;
        case SDLK_DELETE:    kind = OPENMMO_TEXT_DELETE;    break;
        case SDLK_LEFT:      kind = OPENMMO_TEXT_LEFT;      break;
        case SDLK_RIGHT:     kind = OPENMMO_TEXT_RIGHT;     break;
        case SDLK_HOME:      kind = OPENMMO_TEXT_HOME;      break;
        case SDLK_END:       kind = OPENMMO_TEXT_END;       break;
        case SDLK_ESCAPE:    kind = OPENMMO_TEXT_CANCEL;    break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            /* Alt+Enter is the fullscreen chord and must not commit a line. */
            if ((ev->key.keysym.mod & KMOD_ALT) != 0) return 0;
            kind = OPENMMO_TEXT_COMMIT;
            break;
        default: return 0;
        }
        view_input_push(in, kind, 0);
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Publishing                                                          */
/* ------------------------------------------------------------------ */

static uint32_t view_keys_to_pad(const struct view_input *in,
                                 const Uint8 *state, int nkeys, int alt_held)
{
    uint32_t m = 0;
    int i;

    if (state == NULL) return 0;
    for (i = 0; i < VIEW_INPUT_KEYS; i++) {
        if ((int)in->map[i].sc >= nkeys || !state[in->map[i].sc]) continue;
        /* Alt+Enter is the fullscreen chord; it must not leak Start. */
        if (alt_held && in->map[i].sc == SDL_SCANCODE_RETURN) continue;
        m |= in->map[i].pad;
    }
    return m;
}

static uint32_t view_axes_to_pad(int lx, int ly)
{
    uint32_t m = 0;

    if (lx <= -VIEW_STICK_DEADZONE) m |= OPENMMO_VIEW_KEY_LEFT;
    if (lx >=  VIEW_STICK_DEADZONE) m |= OPENMMO_VIEW_KEY_RIGHT;
    if (ly <= -VIEW_STICK_DEADZONE) m |= OPENMMO_VIEW_KEY_UP;
    if (ly >=  VIEW_STICK_DEADZONE) m |= OPENMMO_VIEW_KEY_DOWN;
    return m;
}

static uint32_t view_controller_to_pad(SDL_GameController *gc)
{
    uint32_t m = 0;
    int i;

    if (gc == NULL) return 0;
    for (i = 0; i < PADMAP_N; i++)
        if (SDL_GameControllerGetButton(gc, PADMAP[i].button)) m |= PADMAP[i].pad;
    return m | view_axes_to_pad(
        SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX),
        SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
}

void view_input_publish(struct view_input *in, struct openmmo_view_shm *shm,
                        const struct openmmo_view_geom *g, int src_w, int src_h,
                        int sec, int win_w, int win_h)
{
    int nkeys = 0, alt, touch_on, tx, ty, typing;
    const Uint8 *state;
    uint32_t keys;

    if (shm == NULL) return;
    typing = view_input_typing(in);

    /*
     * Entering a text field releases the PAD. A player holding a direction when a chat box
     * opens would otherwise walk until they pressed and released it again, because the level
     * that would clear it is no longer being read.
     */
    if (typing != in->typing) {
        in->typing = typing;
        if (typing) SDL_StartTextInput();
        else        SDL_StopTextInput();
    }

    state = SDL_GetKeyboardState(&nkeys);
    alt = (SDL_GetModState() & KMOD_ALT) != 0;
    /* The keyboard is letters while a field is open; the gamepad is never
     * letters, so it keeps driving the game underneath one. */
    keys = (typing ? 0u : view_keys_to_pad(in, state, nkeys, alt))
         | view_controller_to_pad(in->gc);

    /* NOTHING PUBLISHES `in_turbo`. */

    touch_on = 0;
    tx = in->sent_tx;
    ty = in->sent_ty;
    if (in->mouse_down) {
        int mx = 0, my = 0;

        SDL_GetMouseState(&mx, &my);
        if (openmmo_view_window_to_touch(g, src_w, src_h, sec, win_w, win_h,
                                         mx, my, &tx, &ty) == 0)
            touch_on = 1;
    }

    /*
     * Published only on change, and in_seq bumped after the words it describes: the game reads
     * that counter to tell a live player from a script, so a bump with nothing behind it would
     * silence a scripted run for nothing.
     */
    if (keys != in->sent_keys || touch_on != in->sent_touch ||
        (touch_on && (tx != in->sent_tx || ty != in->sent_ty))) {
        shm->in_keys = keys;
        shm->in_touch = (uint32_t)touch_on;
        shm->in_touch_x = (uint32_t)tx;
        shm->in_touch_y = (uint32_t)ty;
        __sync_synchronize();
        shm->in_seq++;
        in->sent_keys = keys;
        in->sent_touch = touch_on;
        in->sent_tx = tx;
        in->sent_ty = ty;
    }
}
