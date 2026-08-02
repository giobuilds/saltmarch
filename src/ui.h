#ifndef UI_H
#define UI_H

/* ========================================================= */

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
 * and hit-testing live in hud_view.c (SDL-free, headlessly testable). */
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
