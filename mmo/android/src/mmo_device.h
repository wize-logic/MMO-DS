/* The device the app woke up on, asked once. */
#ifndef MMO_DEVICE_H
#define MMO_DEVICE_H

#include "mmo_device_rules.h"

struct android_app;

/* Read what can be read before any window exists: the configuration's
 * density and screen size, the cores and their ceilings, the memory. Logs
 * one `device:` line. Safe to call more than once. */
void mmo_device_probe(struct android_app *app);

/* The surface's real size, once there is one. Replaces the configuration's
 * dp estimate. */
void mmo_device_note_window(int width_px, int height_px);

/* The renderer's name, from the shim when the context comes up. */
void mmo_device_note_gl(const char *renderer);

/* The facts as they stand. */
const struct mmo_device_facts *mmo_device_facts(void);
/* The same, for mmo_calibrate.c to ask the rules' uncapped answer. */
const struct mmo_device_facts *mmo_device_facts_for_rules(void);

/* The defaults that follow from them, recomputed from the current facts. */
void mmo_device_defaults(struct mmo_device_defaults *out);

#endif
