#ifndef TRADE_UI_H
#define TRADE_UI_H

/* =========================================================
 * trade_ui.h  --  Drawing the exchange screen (UI_PLAN Phase 1)
 *
 * All that is left in this file is paint. Layout, pagination,
 * hit-testing and the decision about which buttons are pressable live
 * in exchange_view.c, which links no SDL and is therefore testable
 * headlessly; this turns the resulting UiList into pixels.
 *
 * The split matters more than it looks. Draw and hit-test used to be
 * two functions computing the same geometry from the same constants and
 * hoping to agree — the classic way a button ends up a few pixels from
 * where it appears, and the reason the old file carried four rect
 * helpers that both halves had to call identically. Now the drawer
 * renders exactly the rects the hit-test will query, because they are
 * the same rects.
 *
 * Callers: build the view (exchange_view_market), build the list
 * (exchange_build), draw it here, hit-test it with exchange_hit.
 * ========================================================= */

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
