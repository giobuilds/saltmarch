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

/* HUD dimensions */
#define HUD_HEIGHT      80    /* pixels tall                  */
#define HUD_SLOT_SIZE   64    /* width and height of one slot */
#define HUD_SLOT_PAD    12    /* gap between slots            */
#define HUD_MARGIN_LEFT 20    /* left edge inset              */

/* Menu overlay dimensions */
#define MENU_W           260
#define MENU_H           284
#define MENU_BTN_H        48
#define MENU_BTN_PAD      16
#define MENU_BTN_MARGIN   20
#define MENU_BTN_COUNT     4

/* ---- Building HUD --------------------------------------
 * Draw the entire HUD bar.
 * selected        – currently selected BuildingType (or BUILDING_NONE)
 * mouse_x/y       – current cursor position in screen pixels
 *                   (used to highlight the hovered slot)
 * demolish_active – 1 if the demolish tool is currently active
 *                   (mutually exclusive with `selected`) */
void ui_draw(SDL_Renderer *renderer,
             int screen_w, int screen_h,
             BuildingType selected,
             int mouse_x, int mouse_y, int menu_open,
             int demolish_active, int world_open);

/* Hit-test: given a screen coordinate, return the BuildingType
 * whose HUD slot contains that point, or BUILDING_NONE. */
BuildingType ui_hit_test(int screen_w, int screen_h,
                         int mouse_x, int mouse_y);

/* Returns 1 if the cog button was clicked. */
int          ui_cog_hit_test(int screen_w, int screen_h,
                             int mouse_x, int mouse_y);

/* Returns 1 if the demolish tool button was clicked. */
int          ui_demolish_hit_test(int screen_w, int screen_h,
                                  int mouse_x, int mouse_y);

/* Returns 1 if the world/archipelago button was clicked. */
int          ui_world_hit_test(int screen_w, int screen_h,
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
