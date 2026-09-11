#ifndef OPENMMO_LAUNCH_GUI_H
#define OPENMMO_LAUNCH_GUI_H

/*
 * The person-facing window: raylib + raygui, laid out as official logingui. Headless paths
 * (--shot, --script, --play) never call this; they stay on launch_menu.c.
 */

#include "launch_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

struct launch_gui_host {
    mmo_launch_settings *set;
    char                *status;
    int                 *status_bad;
    char                *conn;
    unsigned             conn_colour;
    /* Return 0 if the session ran, -1 if it would not start. */
    int                (*play)(void *ctx);
    /* The second row: the same, with no server, no update and no feed gate. */
    int                (*play_offline)(void *ctx);
    /* The earlier saved games kept beside the offline one, newest first, and
     * putting one of them back. Both NULL in a host that has no save folder. */
    int                (*saves)(void *ctx, char out[][MMO_LAUNCH_STAMP],
                                int max);
    int                (*restore)(void *ctx, const char *stamp);
    /*
     * The offline save, when it is newer than the copy the server was last handed. `offer`
     * fills in the two play times and returns 1 when there is one worth taking online; the
     * window then draws the box that asks, and `take` carries the answer back.
     */
    int                (*offer)(void *ctx, mmo_launch_import *out);
    void               (*take)(void *ctx, int yes);
    /*
     * Which of this install's saved games every row above is about, and the press that changes
     * it.
     */
    int                (*slot)(void *ctx);
    void               (*set_slot)(void *ctx, int slot);
    /*
     * The whole of a slot, out to a file and back: the save, the report beside it and the
     * sessions behind it, so the game can be carried to another machine with the play that
     * produced it.
     */
    int                (*save_export)(void *ctx);
    int                (*save_import)(void *ctx);
    void                 *ctx;
};

/* Drop the login window once the game window is mapped, so the compositor
 * has one focused surface. No-op if this process never opened a window. */
void launch_gui_hide_for_session(void);

/* One repaint from inside a blocking update: the current progress line on
 * the login art, throttled to 20 Hz. A no-op with no window open, so the
 * CLI paths can share the callers that call it. */
void launch_gui_progress(const char *line);

int launch_gui_run(struct launch_gui_host *host);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_LAUNCH_GUI_H */
