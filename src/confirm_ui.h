#ifndef CONFIRM_UI_H
#define CONFIRM_UI_H

/* =========================================================
 * confirm_ui.h  --  Painting the one confirmation
 *                   (UI_PLAN Phase 6)
 *
 * Replaces build_confirm_ui.c, demolish_confirm_ui.c and
 * tier_upgrade_ui.c, which were the same panel three times with
 * different words. Geometry and hit-testing live in confirm_view.c.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "confirm_view.h"
#include "ui_kit.h"

void confirm_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                     const UiList *list, const ConfirmView *view,
                     int mouse_x, int mouse_y);

#endif /* CONFIRM_UI_H */
