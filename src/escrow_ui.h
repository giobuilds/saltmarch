#ifndef ESCROW_UI_H
#define ESCROW_UI_H

/* =========================================================
 * escrow_ui.h  --  The Harbor escrow panel (MMO_PLAN Phase 5)
 *
 * The owner's side of the inter-player airlock. Clicking a placed
 * Harbor on an island YOU OWN opens this overlay: one row per resource
 * showing what visitors have deposited in escrow, with [Take] (accept
 * the whole deposit into your stockpile) and [Put 10] (stage goods for
 * a visitor to collect — leaving payment is how a trade completes).
 * The footer holds the docking-permission toggle — closing the harbor
 * to foreign ships is the blockade lever — and Close.
 *
 * Same overlay pattern as trade_ui/build_confirm (sizing #defines,
 * panel_rect helpers, pure draw/hit_test pair, a *Hit enum), and like
 * them every action emits a Command via the game_* wrappers from
 * main.c's click dispatch — nothing here mutates anything.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "island.h"
#include "resource.h"

#define ESCROW_W        460
#define ESCROW_TITLE_H   34
#define ESCROW_ROW_H     30
#define ESCROW_MARGIN    20
#define ESCROW_BTN_W     76
#define ESCROW_BTN_H     24
#define ESCROW_BTN_GAP    8
#define ESCROW_FOOT_H    40

#define ESCROW_H (ESCROW_TITLE_H + RES_COUNT * ESCROW_ROW_H \
                  + ESCROW_FOOT_H + ESCROW_MARGIN * 2)

typedef enum {
    ESCROW_HIT_NONE    = 0,   /* click outside the panel — close      */
    ESCROW_HIT_CLOSE   = 1,
    ESCROW_HIT_TAKE    = 2,   /* accept a deposit — *out_res is set   */
    ESCROW_HIT_PUT     = 3,   /* stage 10 units    — *out_res is set  */
    ESCROW_HIT_DOCKING = 4    /* toggle foreign-ship permission       */
} EscrowHit;

/* Draw the panel for `isl` (escrow contents, stockpile amounts for the
 * Put buttons' greying, docking state, island name in the title). */
void escrow_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                    const Island *isl, int mouse_x, int mouse_y);

/* Hit-test a click. On TAKE/PUT, *out_res is the row's resource. */
EscrowHit escrow_ui_hit_test(int screen_w, int screen_h,
                             int mouse_x, int mouse_y,
                             ResourceType *out_res);

#endif /* ESCROW_UI_H */
