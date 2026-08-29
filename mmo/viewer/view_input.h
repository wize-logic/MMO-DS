/* What the player does, on its way to the game. */

#ifndef OPENMMO_VIEW_INPUT_H
#define OPENMMO_VIEW_INPUT_H

#include <SDL.h>

#include "platform.h"
#include "text_channel.h"
#include "view_channel.h"
#include "view_geom.h"

/* The twelve, and no more: a keymap row per button the channel carries. */
#define VIEW_INPUT_KEYS 12

struct view_input {
    struct { SDL_Scancode sc; uint32_t pad; } map[VIEW_INPUT_KEYS];
    SDL_GameController *gc;
    int mouse_down;

    /* What was last published, so an idle window writes nothing. */
    uint32_t sent_keys;
    int sent_touch, sent_tx, sent_ty;

    /* The text page: ours to create, the client's to ask for. */
    struct openmmo_text_shm *text;
    mmo_shm text_page;
    char text_name[128];
    int typing;              /* SDL_StartTextInput is on and keys are letters */
    unsigned long units;     /* code units sent this session */
};

/* The defaults, the empty state, no page. Also the initializer. */
void view_input_init(struct view_input *in);

/* Open a pad that was already plugged in when the window started. SDL's
 * CONTROLLERDEVICEADDED is how a hot-plug arrives; a device that was there
 * first is just a joystick index, and waiting for an event that never comes
 * leaves it dark. */
void view_input_attach_present(struct view_input *in);

/* `--bind a=k,start=return`, SDL's own key names. Returns 0, or -1 having said
 * what it did not understand, a binding silently ignored is a key that does
 * nothing and a player with no way to find out why. */
int view_input_bind(struct view_input *in, const char *spec);

/* Create the text page for a frame channel of this name, so a client that opens
 * "<name>.text" finds it. Never fatal: a window with no text page is a window
 * that cannot type, which is what every session was until now. */
void view_input_open_text(struct view_input *in, const char *channel);
void view_input_close_text(struct view_input *in);

/* Nonzero while the client says a text field is open. */
int view_input_typing(const struct view_input *in);

/* One SDL event. Returns nonzero if it was consumed as text, the window's own
 * chords (fullscreen, screenshot) are not, and Escape is only consumed while a
 * field is open, where it cancels the field rather than ending the session. */
int view_input_event(struct view_input *in, const SDL_Event *ev);

/* Read the keyboard, the gamepad and the mouse as levels and publish them to
 * the frame channel if anything changed. `g` and the sizes are what the window
 * is currently drawing, which is how a mouse position becomes a pen pixel. */
void view_input_publish(struct view_input *in, struct openmmo_view_shm *shm,
                        const struct openmmo_view_geom *g, int src_w, int src_h,
                        int sec, int win_w, int win_h);

#endif /* OPENMMO_VIEW_INPUT_H */
