#ifndef INVENTORY_UI_H
#define INVENTORY_UI_H

/* inventory_ui.h  --  Drawing the stores overlay and the vitals
 * strip (UI_PLAN Phase 4) */

#include <SDL3/SDL.h>
#include "inventory_view.h"
#include "island_bar.h"
#include "ui_kit.h"
#include "vitals.h"

/* Draw the stores overlay from the list inventory_build() produced. */
void inventory_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                       const UiList *list, const InventoryView *view,
                       int mouse_x, int mouse_y);

/* Draw the alert strip, top-right under the population readout. Not an
 * overlay: it is always on, never takes a click, and says nothing when
 * there is nothing to say. */
void vitals_ui_draw(SDL_Renderer *renderer, int screen_w,
                    const VitalsView *v);

/* Draw the ‹ island › header from the list island_bar_build() made.
 * `snap` supplies the island's hue and whether it is settled. */
void island_bar_draw(SDL_Renderer *renderer, const UiList *list,
                     const UiSnapshot *snap, int mouse_x, int mouse_y);

#endif /* INVENTORY_UI_H */
