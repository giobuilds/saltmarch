#ifndef UI_H
#define UI_H

/* =========================================================
 * ui.h  --  HUD / user interface
 *
 * Phase 3 UI is a single horizontal bar pinned to the bottom
 * of the screen.  It contains one slot per building type.
 *
 * Layout (1920 × 1080 screen):
 *
 *   ┌──────────────────────────────────────────────────────┐
 *   │                  game world                          │
 *   ├──────────────────────────────────────────────────────┤
 *   │  [Fishers][Warehouse][Farm][Lumberjack]  ← HUD bar   │
 *   └──────────────────────────────────────────────────────┘
 *
 * Each slot is a rectangle.  The selected slot gets a bright
 * border.  Hovering shows the building name above the bar.
 * 
 * Layout:
 *   Left side  – building slots (one per BuildingType)
 *   Right side – demolish (destroy) tool button, then the cog
 *                button that opens the game menu overlay
 *
 * Menu overlay (centred on screen):
 *   [ New Game ]   ← game_new():  fresh map seed, world cleared
 *   [ Load     ]   ← game_load(SAVE_FILE_PATH)
 *   [ Save     ]   ← game_save(SAVE_FILE_PATH)
 *   [ Quit     ]   ← calls SDL_APP_SUCCESS
 * ========================================================= */

#include <SDL3/SDL.h>
#include "building.h"
#include "hud_view.h"   /* HUD metrics and the bar's layout/hit-test */
#include "ui_kit.h"

/* Menu overlay dimensions */
#define MENU_W           260
#define MENU_H           284
#define MENU_BTN_H        48
#define MENU_BTN_PAD      16
#define MENU_BTN_MARGIN   20
#define MENU_BTN_COUNT     4

/* ---- Building HUD --------------------------------------
 * Since UI_PLAN Phase 3 the bar's layout, tabs, affordability greying
 * and hit-testing live in hud_view.c (SDL-free, headlessly testable).
 * What is left here is drawing: hand it the UiList that hud_build()
 * produced and the view it was built from.
 *
 * Hit-testing is hud_hit() on that same list — there is deliberately no
 * ui_hit_test/ui_cog_hit_test/... any more. Four separate hit-test
 * functions recomputing geometry the drawer also recomputed was how a
 * button could end up a few pixels from where it appeared. */
void ui_draw(SDL_Renderer *renderer,
             const UiList *list, const HudView *view,
             int mouse_x, int mouse_y);

/* ---- Menu overlay -------------------------------------- */
 
typedef enum {
    MENU_HIT_NONE     = 0,
    MENU_HIT_NEWGAME  = 1,
    MENU_HIT_LOAD     = 2,
    MENU_HIT_SAVE     = 3,
    MENU_HIT_QUIT     = 4
} MenuHit;
 
/* Draw the menu overlay panel. Only called when menu_open == 1. */
void    ui_menu_draw(SDL_Renderer *renderer,
                     int screen_w, int screen_h,
                     int mouse_x, int mouse_y);
 
/* Hit-test the menu buttons. Returns MENU_HIT_NONE if no button hit. */
MenuHit ui_menu_hit_test(int screen_w, int screen_h,
                         int mouse_x, int mouse_y);

#endif /* UI_H */
