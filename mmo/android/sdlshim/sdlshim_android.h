/* The frontend's half of the shim. */

#ifndef OPENMMO_SDLSHIM_ANDROID_H
#define OPENMMO_SDLSHIM_ANDROID_H

#include <android/native_window.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The surface, as the framework grants and revokes it. set_window(NULL)
 * BLOCKS until the render thread has released the EGL surface, because the
 * window is invalid the moment APP_CMD_TERM_WINDOW returns. */
void mmo_sdlshim_set_window(ANativeWindow *win);

/* A touch, in device pixels. action: 0 down, 1 move, 2 up/cancel.
 * `fingers` is the live pointer count; the second finger turns the gesture
 * into a wheel and the shim says so by ending the left drag. */
void mmo_sdlshim_touch(int action, float x, float y, int fingers, float y2);

/*
 * Every pointer on the glass, which the single-mouse feeder above cannot carry and the on-
 * screen pad cannot do without: holding a direction while pressing A is two fingers, and a
 * shim that only ever knows about pointer zero can express one of them.
 */
#define MMO_SDLSHIM_PT_MAX 8    /* more fingers than a pad and a pen need */

struct mmo_sdlshim_pt { float x, y; };

void mmo_sdlshim_pointers(int action, const struct mmo_sdlshim_pt *pts, int n,
                          int act_index);

/*
 * A REAL PAD ARRIVED, so the drawn one is clutter and hides itself. Called the first time a
 * gamepad keycode or a joystick axis reaches the frontend: an RG556 has its buttons under the
 * player's thumbs already, and a phone never sends this.
 */
void mmo_sdlshim_pad_saw_gamepad(void);

/*
 * The pad belongs to the game, not to every screen. It starts disabled and the frontend turns
 * it on when the viewer proper takes the renderer over.
 */
void mmo_sdlshim_pad_enable(int on);

/* One controller button (SDL_CONTROLLER_BUTTON_*), as a level. */
void mmo_sdlshim_button(int button, int down);

/* The left stick / hat, -1..1 per axis. */
void mmo_sdlshim_axis(float lx, float ly);

/* A keyboard key by SDL scancode (adb / a developer's keyboard). */
void mmo_sdlshim_scancode(int scancode, int down);

/*
 * What the system keyboard typed, as the bytes the activity wrote down its pipe and the
 * frontend read off its looper: UTF-8 text with a few control bytes for the edits and one
 * record for the rows the keyboard covers (the values are in OpenMMOActivity.java and
 * sdlshim.c, "The keyboard").
 */
void mmo_sdlshim_ime_bytes(const unsigned char *bytes, size_t n);

/* App went to / came back from the background: silence and resume AAudio. */
void mmo_sdlshim_background(int paused);

/* Nonzero once SDL_CreateRenderer succeeded, the frontend uses it to know
 * the viewer owns the screen and the fallback path must stay away. */
int mmo_sdlshim_active(void);

/* Ask the viewer loop to end (SDL_QUIT), for the app's exit hook. */
void mmo_sdlshim_push_quit(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_SDLSHIM_ANDROID_H */
