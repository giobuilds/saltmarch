#ifndef YARD_UI_H
#define YARD_UI_H

/* yard_ui.h  --  Drawing the yard and the fleet (UI_PLAN N6) */

#include <SDL3/SDL.h>
#include "yard_view.h"
#include "ui_kit.h"

void yard_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                  const UiList *list, const YardView *view,
                  int mouse_x, int mouse_y);

#endif /* YARD_UI_H */
