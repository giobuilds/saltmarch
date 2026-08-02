#ifndef CONFIRM_UI_H
#define CONFIRM_UI_H

/* confirm_ui.h  --  Painting the one confirmation
 * (UI_PLAN Phase 6) */

#include <SDL3/SDL.h>
#include "confirm_view.h"
#include "ui_kit.h"

void confirm_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                     const UiList *list, const ConfirmView *view,
                     int mouse_x, int mouse_y);

#endif /* CONFIRM_UI_H */
