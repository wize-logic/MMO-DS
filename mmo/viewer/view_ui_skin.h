/* view_ui_skin.h, this client's own art for the UI layer: painted, not read. */

#ifndef OPENMMO_VIEW_UI_SKIN_H
#define OPENMMO_VIEW_UI_SKIN_H

#include "view_ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fill `t` with our own art for every surface the region table names, on sheets the size the
 * table was measured against. Nonzero and `ready` on success; nothing is left allocated on
 * failure.
 */
int view_ui_skin_make(struct view_ui_theme *t);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_SKIN_H */
