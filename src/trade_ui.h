#ifndef TRADE_UI_H
#define TRADE_UI_H

/* trade_ui.h  --  Drawing the exchange screen (UI_PLAN Phase 1) */

#include <SDL3/SDL.h>
#include "exchange_view.h"
#include "ui_kit.h"

/* Draw the exchange overlay: dimmed world, panel, column headings, the
 * per-row numbers taken from `view`, and every widget in `list`.
 * mouse_x/y highlight whatever is hovered. */
void trade_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const UiList *list, const ExchangeView *view,
                   int mouse_x, int mouse_y);

#endif /* TRADE_UI_H */
