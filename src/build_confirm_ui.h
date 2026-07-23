#ifndef BUILD_CONFIRM_UI_H
#define BUILD_CONFIRM_UI_H

/* =========================================================
 * build_confirm_ui.h  --  Build-confirmation popup  (fix pass)
 *
 * Same "dim background + centred panel" pattern as trade_ui.c/ui.c's
 * menu overlay, kept in its own file for the same reason trade_ui.c
 * is separate from ui.c: a conceptually distinct overlay.
 *
 * Every building placement except Road goes through this instead of
 * placing instantly (Road is exempt — see game_try_place_road()'s
 * doc comment in game.h: it's also placeable by dragging, and a
 * per-tile popup would make that gesture unusable). Two payment
 * options are offered — pay the normal resource+Gold cost, or pay a
 * pure-Gold equivalent at a markup (building_gold_equivalent_cost,
 * building.h) — since some islands generate with no forest at all,
 * so Wood may never be available and anything costing Wood
 * (Warehouse/House/Marketplace) would otherwise be permanently
 * unbuildable. For a Gold-only building type both options show the
 * same number — an accepted consequence of applying the popup
 * uniformly rather than special-casing which buildings need it.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "building.h"
#include "resource.h"

#define BC_W          420
#define BC_TITLE_H     34
#define BC_OPT_H       56
#define BC_OPT_PAD     12
#define BC_MARGIN      20
#define BC_BTN_W      100
#define BC_BTN_H       36
#define BC_BTN_GAP     16

/* The two payment options: 0 = pay resources, 1 = pay Gold. */
#define BC_OPT_COUNT    2

#define BC_H (BC_TITLE_H + BC_OPT_COUNT * (BC_OPT_H + BC_OPT_PAD) \
              + BC_BTN_H + BC_MARGIN * 2)

typedef enum {
    BC_HIT_NONE          = 0,   /* click outside the panel — cancels */
    BC_HIT_PAY_RESOURCES = 1,
    BC_HIT_PAY_GOLD       = 2,
    BC_HIT_OK             = 3,
    BC_HIT_CANCEL         = 4
} BuildConfirmHit;

/* Draw the popup for placing `type`. `s` is the live stockpile (read-
 * only, used to grey out an unaffordable option); `fac` supplies the
 * live market ask used to price the Gold-payment option; payment_selected
 * is which option (0 or 1) currently has the highlighted selection
 * border; mouse_x/y highlight the hovered button. */
void build_confirm_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                           BuildingType type, const Stockpile *s,
                           const Faction *fac,
                           int payment_selected, int mouse_x, int mouse_y);

/* Hit-test a click against the popup. */
BuildConfirmHit build_confirm_ui_hit_test(int screen_w, int screen_h,
                                          int mouse_x, int mouse_y);

#endif /* BUILD_CONFIRM_UI_H */
