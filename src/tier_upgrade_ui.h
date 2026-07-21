#ifndef TIER_UPGRADE_UI_H
#define TIER_UPGRADE_UI_H

/* =========================================================
 * tier_upgrade_ui.h  --  Population tier upgrade confirmation
 *
 * Structurally identical to demolish_confirm_ui.c/.h (dim backdrop,
 * centered panel, title, one message line, two buttons, independent
 * draw/hit-test) — just different text, and the confirm button gets
 * an affirmative color rather than demolish's danger-red, since
 * upgrading is a good outcome for the player, not a destructive one.
 *
 * Opened when the player clicks an active, connected BUILDING_HOUSE
 * with nothing selected and the demolish tool off (see main.c, right
 * next to the Marketplace-click-opens-trade check). Confirming calls
 * game_upgrade_house() (game.c/h), which deducts TIER_UPGRADE_COST_GOLD
 * and mutates the building's type in place.
 * ========================================================= */

#include <SDL3/SDL.h>

#define TU_W        380
#define TU_TITLE_H   34
#define TU_MSG_H     50
#define TU_MARGIN    20
#define TU_BTN_W    100
#define TU_BTN_H     36
#define TU_BTN_GAP   16

#define TU_H (TU_TITLE_H + TU_MSG_H + TU_BTN_H + TU_MARGIN * 3)

typedef enum {
    TU_HIT_NONE   = 0,   /* click outside the panel — cancels */
    TU_HIT_OK     = 1,   /* "Upgrade" — the confirm action */
    TU_HIT_CANCEL = 2
} TierUpgradeHit;

/* Draw the popup. `gold_cost` is shown in the confirmation message;
 * `can_afford` greys out/tints the confirm button when Gold on hand
 * is insufficient; mouse_x/y highlight the hovered button. */
void tier_upgrade_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                         int gold_cost, int can_afford,
                         int mouse_x, int mouse_y);

/* Hit-test a click against the popup. */
TierUpgradeHit tier_upgrade_ui_hit_test(int screen_w, int screen_h,
                                       int mouse_x, int mouse_y);

#endif /* TIER_UPGRADE_UI_H */
