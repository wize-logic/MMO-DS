/* The app's updater, on its front door. */

#ifndef OPENMMO_UPDATE_NOTICE_H
#define OPENMMO_UPDATE_NOTICE_H

#include <stddef.h>

#include "SDL.h"

struct view_ui_gpu;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ask the channel, on a thread of its own. Returns at once: a fetch waits up to thirty seconds
 * for a server that is not answering, and a login screen that freezes for thirty seconds is a
 * worse bug than a missed update.
 */
void mmo_update_notice_ask(void);

/* The build's own revision, the commit count the APK's versionCode is, or 0
 * for a tree with no git. The plan library writes it into every session
 * record and export anchor the device makes. */
int mmo_update_notice_revision(void);

/* 1 when this build has a channel to ask and a revision to compare: the
 * rail button exists. 0 is every working-tree build, and the door draws no
 * button for it. */
int mmo_update_notice_available(void);

/* Once a frame, from the door's own loop: hears the installer's verdict
 * (which arrives as a file) and moves the state on. */
void mmo_update_notice_tick(void);

/* The rail button's label for the current state, check for updates, and
 * then what the check, the download or the install is doing. */
void mmo_update_notice_label(char *out, size_t cap);

/* The rail button pressed: a check when there is nothing else going on, the
 * panel back when there is. */
void mmo_update_notice_press(void);

/* 1 while the panel should be on the screen. */
int mmo_update_notice_up(void);

/* A tap, in the front door's own coordinates. Returns 1 when the panel took
 * it, which is every tap while it is up, including the ones that land
 * outside it: a modal that lets the door take clicks behind it is a player
 * pressing login through a panel they cannot see past. */
int mmo_update_notice_tap(int x, int y, int sw, int sh);

/* The panel, over whatever the door has already drawn. */
void mmo_update_notice_draw(SDL_Renderer *ren, struct view_ui_gpu *gpu,
                            int sw, int sh);

/*
 * The three doors into the framework, all the frontend's, because the activity belongs to it
 * and each is an Intent or a session.
 */

/* Hand the player's browser a URL. 0 when the intent went out. */
int mmo_android_open_url(const char *url);

/* Hand the package installer a downloaded, proven APK. 0 when the ask
 * reached the activity; the verdict arrives later, below. */
int mmo_android_install_apk(const char *path);

/* The installer's verdict, if one is waiting: 1 with the line in `out`,
 * 0 while nothing has been said. Lines: "pending" (the system dialog is
 * up), "denied" (the permission page opened), "done", "failed <reason>". */
int mmo_android_install_result(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_UPDATE_NOTICE_H */
