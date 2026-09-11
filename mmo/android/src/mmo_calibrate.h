/* What this device held last time, kept between sessions. */
#ifndef MMO_CALIBRATE_H
#define MMO_CALIBRATE_H

#include "mmo_device_rules.h"

/* Read the file. Safe to call more than once; the first call reads. */
void mmo_calibrate_load(void);

/* The hd3d cap the file holds, 0 for none. */
int mmo_calibrate_cap(void);

/* The hd3d this session runs at, from wherever it was decided. */
void mmo_calibrate_session_level(int hd3d);

/* One line of the engine's output. Only pc-pace lines are read. Called on
 * the log pump's thread. */
void mmo_calibrate_note_line(const char *line);

/* Fold what has been counted so far into the file. Idempotent within a
 * session: the fold always starts from the file as it was loaded. Called
 * when the app pauses or ends, and every minute from the pump. */
void mmo_calibrate_flush(void);

#endif
