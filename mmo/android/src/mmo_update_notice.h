/* "there is a newer build", on the app's front door. */

#ifndef OPENMMO_UPDATE_NOTICE_H
#define OPENMMO_UPDATE_NOTICE_H

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

/* 1 while the panel should be on the screen: the answer came back, it was
 * ahead of this build, and the player has not answered it yet. */
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
 * Hand the player's browser a URL, the frontend's, because the activity belongs to it and
 * this is an Intent. mmo_android_main.c implements it; anything that links this file without
 * one gets the weak definition below, which reports that it could not.
 */
int mmo_android_open_url(const char *url);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_UPDATE_NOTICE_H */
