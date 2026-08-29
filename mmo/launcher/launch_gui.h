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
