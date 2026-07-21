#ifndef DEMOLISH_CONFIRM_UI_H
#define DEMOLISH_CONFIRM_UI_H

/* =========================================================
 * demolish_confirm_ui.h  --  Bulldozer confirmation popup
 *
 * Same "dim background + centred panel" pattern as build_confirm_ui.c/
 * trade_ui.c, kept in its own file for the same reason those are
 * separate from ui.c: a conceptually distinct overlay. Much simpler
 * than those two — just a message and two buttons, no payment
 * options or per-resource rows — so it doesn't reuse either file's
 * geometry directly, but follows the same panel/dim/button drawing
 * conventions.
 *
 * Opened instead of demolishing immediately when the demolish tool
 * (ui.c's HUD icon) is active and the player clicks a building.
 * Unlike build_confirm_ui's Ok (a safe default there), the
 * destructive button here — "Destroy" — gets the red danger tint,
 * matching ui_menu_draw's Quit button convention: whichever action
 * is irreversible gets the warning color, not whichever button is
 * semantically "Ok".
 * ========================================================= */

#include <SDL3/SDL.h>

#define DC_W        380
#define DC_TITLE_H   34
#define DC_MSG_H     50
#define DC_MARGIN    20
#define DC_BTN_W    100
#define DC_BTN_H     36
#define DC_BTN_GAP   16

#define DC_H (DC_TITLE_H + DC_MSG_H + DC_BTN_H + DC_MARGIN * 3)

typedef enum {
    DC_HIT_NONE   = 0,   /* click outside the panel — cancels */
    DC_HIT_OK     = 1,   /* "Destroy" — the destructive action */
    DC_HIT_CANCEL = 2
} DemolishConfirmHit;

/* Draw the popup. `building_name` is shown in the confirmation
 * message (e.g. BUILDING_DEFS[type].name); mouse_x/y highlight the
 * hovered button. */
void demolish_confirm_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                              const char *building_name,
                              int mouse_x, int mouse_y);

/* Hit-test a click against the popup. */
DemolishConfirmHit demolish_confirm_ui_hit_test(int screen_w, int screen_h,
                                                int mouse_x, int mouse_y);

#endif /* DEMOLISH_CONFIRM_UI_H */
